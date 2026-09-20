#include "ranging.h"
#include "stats.h"
#include "dw3000.h"
#include <SPI.h>
#include <Preferences.h>
#include <math.h>

extern SPISettings _fastSPI;
extern dwt_txconfig_t txconfig_options;

static Preferences prefs;
static TaskHandle_t task_h = nullptr;
static volatile bool radio_ok = false;
static volatile bool suspended = false;

static volatile Role     cur_role     = ROLE_INITIATOR;
static volatile uint16_t cur_antdly   = DEFAULT_ANT_DLY;
static volatile uint16_t cur_interval = DEFAULT_INTERVAL_MS;
// The OTP crystal trim is unprogrammed on these boards (XTRIM OTP READ FAIL),
// so the driver defaults to 0x2E. Made settable so the two boards can be
// trimmed to agree with each other.
static volatile uint8_t cur_xtrim = 0x2E;
static volatile Phy cur_phy = PHY_SHORT;
// Set by the web task, consumed by the radio task at a safe point in the loop.
static volatile bool pending_reconfig = false;

static dwt_config_t phy_config[PHY_COUNT] = {
  { 5, DWT_PLEN_128,  DWT_PAC8,  9, 9, 1, DWT_BR_6M8,
    DWT_PHRMODE_STD, DWT_PHRRATE_STD, (129 + 8 - 8),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0 },
  { 5, DWT_PLEN_1024, DWT_PAC32, 9, 9, 2, DWT_BR_850K,
    DWT_PHRMODE_STD, DWT_PHRRATE_STD, (1025 + 16 - 32),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0 },
  { 5, DWT_PLEN_2048, DWT_PAC32, 9, 9, 1, DWT_BR_850K,
    DWT_PHRMODE_STD, DWT_PHRRATE_STD, (2049 + 64 - 32),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0 },
};

static uint8_t tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
static uint8_t rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8_t rx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
static uint8_t tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static uint8_t frame_seq_nb = 0;
static uint8_t rx_buffer[24];
static uint32_t status_reg = 0;

Role     ranging_role()     { return cur_role; }
uint16_t ranging_antdly()   { return cur_antdly; }
uint16_t ranging_interval() { return cur_interval; }
bool     ranging_radio_ok() { return radio_ok; }

void ranging_set_role(Role r)        { cur_role = r;     prefs.putUChar("role", (uint8_t)r); pending_reconfig = true; }
void ranging_set_antdly(uint16_t d)  { cur_antdly = d;   prefs.putUShort("antdly", d);       pending_reconfig = true; }
void ranging_set_interval(uint16_t m){ cur_interval = m; prefs.putUShort("interval", m); }
uint8_t ranging_xtrim() { return cur_xtrim; }
void ranging_set_xtrim(uint8_t v) { cur_xtrim = v & 0x7F; prefs.putUChar("xtrim", cur_xtrim); pending_reconfig = true; }
Phy  ranging_phy() { return cur_phy; }
void ranging_set_phy(Phy p) { cur_phy = p; prefs.putUChar("phy", (uint8_t)p); pending_reconfig = true; }

void ranging_suspend() {
  if (task_h && !suspended) { suspended = true; vTaskSuspend(task_h); }
}
void ranging_resume() {
  if (task_h && suspended) { suspended = false; vTaskResume(task_h); }
}

// ---- Link quality -----------------------------------------------------
// Estimates from the Ipatov CIR diagnostics, per the DW3000 user manual.
// A = 121.7 dB for the 64 MHz PRF. These are uncalibrated estimates useful
// for comparing one exchange with another, not absolute power measurements.
static const float RSSI_A = 121.7f;

// Raw CIR diagnostics from the most recent good frame, exposed for debugging
// the power calculation itself.
volatile uint32_t dbg_acc, dbg_cir, dbg_f1, dbg_f2, dbg_f3;

static void link_quality(float *rssi, float *fp) {
  dwt_rxdiag_t d;
  dwt_readdiagnostics(&d);
  dbg_acc = d.ipatovAccumCount; dbg_cir = d.ipatovPower;
  dbg_f1 = d.ipatovF1; dbg_f2 = d.ipatovF2; dbg_f3 = d.ipatovF3;
  float N = (float)d.ipatovAccumCount;
  if (N <= 0) { *rssi = NAN; *fp = NAN; return; }
  float N2 = N * N;

  float C = (float)d.ipatovPower;
  *rssi = (C > 0) ? 10.0f * log10f((C * 2097152.0f) / N2) - RSSI_A : NAN;

  float f1 = (float)d.ipatovF1, f2 = (float)d.ipatovF2, f3 = (float)d.ipatovF3;
  float fsum = f1 * f1 + f2 * f2 + f3 * f3;
  *fp = (fsum > 0) ? 10.0f * log10f(fsum / N2) - RSSI_A : NAN;
}

// ---- Radio bring-up ---------------------------------------------------
static bool radio_configure() {
  radio_ok = false;
  _fastSPI = SPISettings(7000000L, MSBFIRST, SPI_MODE0);
  spiBegin(PIN_IRQ, PIN_RST);
  spiSelect(PIN_SS);
  delay(2);

  dwt_softreset();
  delay(2);

  uint32_t guard = millis() + 1000;
  while (!dwt_checkidlerc()) {
    if (millis() > guard) { Serial.println("[uwb] IDLE_RC timeout"); return false; }
  }
  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) { Serial.println("[uwb] init failed"); return false; }
  if (dwt_configure(&phy_config[cur_phy]))      { Serial.println("[uwb] configure failed"); return false; }

  dwt_configuretxrf(&txconfig_options);
  // Without this the CIA leaves the diagnostic registers unpopulated and the
  // power figures read as saturated garbage.
  dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);
  dwt_setxtaltrim(cur_xtrim);
  dwt_setrxantennadelay(cur_antdly);
  dwt_settxantennadelay(cur_antdly);

  const PhyTiming &t = PHY_TIMING[cur_phy];
  if (cur_role == ROLE_INITIATOR) {
    dwt_setrxaftertxdelay(t.poll_tx_to_resp_rx_dly);
    dwt_setrxtimeout(t.resp_rx_timeout);
  } else {
    // Bounded window; the loop re-arms RX so the task yields periodically.
    dwt_setrxtimeout(RESPONDER_RX_WINDOW_UUS);
  }
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  radio_ok = true;
  Serial.printf("[uwb] configured as %s, antenna delay %u\n",
                cur_role == ROLE_INITIATOR ? "INITIATOR" : "RESPONDER", cur_antdly);
  return true;
}

// ---- Initiator: one SS-TWR exchange -----------------------------------
static void do_initiator() {
  tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

  // Bounded by the configured RX timeout; the wall-clock guard is a backstop
  // in case the radio never raises a status bit at all.
  uint32_t guard = millis() + 100;
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
    if (millis() > guard) {
      dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
      stats_add_fail(FAIL_TIMEOUT);
      return;
    }
  }
  frame_seq_nb++;

  if (!(status_reg & SYS_STATUS_RXFCG_BIT_MASK)) {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    stats_add_fail((status_reg & SYS_STATUS_ALL_RX_TO) ? FAIL_TIMEOUT : FAIL_RX_ERROR);
    return;
  }

  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
  uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
  if (frame_len > sizeof(rx_buffer)) { stats_add_fail(FAIL_BAD_FRAME); return; }

  dwt_readrxdata(rx_buffer, frame_len, 0);
  rx_buffer[ALL_MSG_SN_IDX] = 0;
  if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) != 0) { stats_add_fail(FAIL_BAD_FRAME); return; }

  uint32_t poll_tx_ts = dwt_readtxtimestamplo32();
  uint32_t resp_rx_ts = dwt_readrxtimestamplo32();
  int16_t raw_off = dwt_readclockoffset();
  float clockOffsetRatio = ((float)raw_off) / (uint32_t)(1 << 26);
  float ppm = clockOffsetRatio * 1e6f;   // carrier offset between the two boards

  uint32_t poll_rx_ts, resp_tx_ts;
  resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
  resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

  int32_t rtd_init = resp_rx_ts - poll_tx_ts;
  int32_t rtd_resp = resp_tx_ts - poll_rx_ts;
  double tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
  float dist = (float)(tof * SPEED_OF_LIGHT);

  // A negative or absurd range is an implementation/timing failure, not a
  // measurement. Record it as such rather than letting it skew the stats.
  if (!isfinite(dist) || dist < -1.0f || dist > 1000.0f) { stats_add_fail(FAIL_BAD_FRAME); return; }

  float rssi, fp;
  link_quality(&rssi, &fp);
  stats_add_ok(dist, rssi, fp, ppm);
#ifdef NO_WIFI
  Serial.printf("D %.3f %.1f %.1f\n", dist, rssi, fp);
#endif
}

// ---- Responder: listen for a poll, reply with timestamps ---------------
static void do_responder() {
  dwt_rxenable(DWT_START_RX_IMMEDIATE);

  uint32_t guard = millis() + 200;
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
    if (millis() > guard) return;   // window expired with nothing heard
  }

  if (!(status_reg & SYS_STATUS_RXFCG_BIT_MASK)) {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    return;                          // idle silence is not a failed exchange
  }

  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
  uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
  if (frame_len > sizeof(rx_buffer)) { stats_add_fail(FAIL_BAD_FRAME); return; }

  dwt_readrxdata(rx_buffer, frame_len, 0);
  rx_buffer[ALL_MSG_SN_IDX] = 0;
  if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) != 0) { stats_add_fail(FAIL_BAD_FRAME); return; }

  uint64_t poll_rx_ts = get_rx_timestamp_u64();
  uint32_t resp_tx_time =
      (poll_rx_ts + (PHY_TIMING[cur_phy].poll_rx_to_resp_tx_dly * UUS_TO_DWT_TIME)) >> 8;
  dwt_setdelayedtrxtime(resp_tx_time);
  uint64_t resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + cur_antdly;

  resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
  resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);

  tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
  dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);

  if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
    // The delayed slot was missed; never transmit late with a stale timestamp.
    stats_add_fail(FAIL_TIMEOUT);
    return;
  }

  uint32_t tguard = millis() + 50;
  while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) {
    if (millis() > tguard) { stats_add_fail(FAIL_TIMEOUT); return; }
  }
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  frame_seq_nb++;

  // Read diagnostics only after the reply is away. Reading the full CIA set is
  // a ~232 byte SPI transfer; doing it before the delayed transmit made the
  // responder miss its 900 uus slot on every exchange.
  float rssi, fp;
  link_quality(&rssi, &fp);
  float ppm = (((float)dwt_readclockoffset()) / (uint32_t)(1 << 26)) * 1e6f;
  stats_add_ok(NAN, rssi, fp, ppm);   // no distance at this end, but a good exchange
}

static void radio_task(void *) {
  for (;;) {
    if (pending_reconfig) {
      pending_reconfig = false;
      stats_reset();
      radio_configure();
    }
    if (!radio_ok) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      radio_configure();
      continue;
    }
    if (cur_role == ROLE_INITIATOR) {
      do_initiator();
      vTaskDelay(pdMS_TO_TICKS(cur_interval));
    } else {
      do_responder();
      vTaskDelay(pdMS_TO_TICKS(1));   // brief yield; RX is re-armed each pass
    }
  }
}

void ranging_init() {
  prefs.begin("uwb", false);
  cur_role     = (Role)prefs.getUChar("role", ROLE_INITIATOR);
  cur_antdly   = prefs.getUShort("antdly", DEFAULT_ANT_DLY);
  cur_interval = prefs.getUShort("interval", DEFAULT_INTERVAL_MS);
  cur_xtrim    = prefs.getUChar("xtrim", 0x2E);
  cur_phy      = (Phy)prefs.getUChar("phy", PHY_SHORT);
  radio_configure();
}

void ranging_start() {
  // Pinned to core 1; Wi-Fi, the web server and OTA all run on core 0.
  xTaskCreatePinnedToCore(radio_task, "uwb", 8192, nullptr, 3, &task_h, 1);
}
