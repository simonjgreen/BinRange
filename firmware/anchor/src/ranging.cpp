#include "ranging.h"
#include "stats.h"
#include "dw3000.h"
#include "core/ranging_frame.h"
#include <SPI.h>
#include <Preferences.h>
#include <freertos/queue.h>
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

// DS-TWR. The anchor is the responder: POLL in, RESP out, FINAL in.
// Address bytes are rewritten per exchange to answer the specific tag.
static uint8_t tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V','E', 'W','A', FN_RESP, 0x02, 0, 0, 0};

// Bounded, non-blocking. A slow consumer must never stall the radio.
static QueueHandle_t evt_q;
static volatile uint32_t q_dropped;

bool ranging_pop(RangeEvent *out) {
    if (!evt_q) return false;
    return xQueueReceive(evt_q, out, 0) == pdTRUE;
}
uint32_t ranging_dropped() { return q_dropped; }

static uint8_t frame_seq_nb = 0;
static uint8_t rx_buffer[33];
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

  // Bounded window; the loop re-arms RX so the task yields periodically.
  dwt_setrxtimeout(RESPONDER_RX_WINDOW_UUS);
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  radio_ok = true;
  Serial.printf("[uwb] configured as %s, antenna delay %u\n",
                cur_role == ROLE_INITIATOR ? "INITIATOR" : "RESPONDER", cur_antdly);
  return true;
}

// ---- Responder: one DS-TWR exchange ------------------------------------
// The anchor holds poll-RX, response-TX and final-RX. FINAL brings the tag's
// three timestamps, so the range is computed here — which is the whole reason
// the protocol is double-sided.
static void do_responder() {
  dwt_rxenable(DWT_START_RX_IMMEDIATE);

  uint32_t guard = millis() + 200;
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
    if (millis() > guard) return;      // quiet window, not a failure
  }
  if (!(status_reg & SYS_STATUS_RXFCG_BIT_MASK)) {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    return;
  }

  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
  uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
  if (!ranging_poll_length_valid(frame_len) || frame_len > sizeof(rx_buffer)) {
    stats_add_fail(FAIL_BAD_FRAME);
    return;
  }
  dwt_readrxdata(rx_buffer, frame_len, 0);

  // FC(2) seq(1) PAN(2) dest(2) src(2) fn(1). Only the fixed prefix can be
  // compared; the addresses vary per tag.
  if (rx_buffer[0] != 0x41 || rx_buffer[1] != 0x88 ||
      rx_buffer[3] != 0xCA || rx_buffer[4] != 0xDE ||
      rx_buffer[9] != FN_POLL) {
    stats_add_fail(FAIL_BAD_FRAME);
    return;
  }
  uint16_t dst = (uint16_t)rx_buffer[5] | ((uint16_t)rx_buffer[6] << 8);
  uint16_t src = (uint16_t)rx_buffer[7] | ((uint16_t)rx_buffer[8] << 8);
  if (dst != ANCHOR_ADDR) { stats_add_fail(FAIL_BAD_FRAME); return; }

  uint64_t poll_rx_ts = get_rx_timestamp_u64();

  // Reply to this specific tag.
  tx_resp_msg[5] = rx_buffer[7];
  tx_resp_msg[6] = rx_buffer[8];
  tx_resp_msg[7] = (uint8_t)(ANCHOR_ADDR & 0xFF);
  tx_resp_msg[8] = (uint8_t)(ANCHOR_ADDR >> 8);

  uint32_t resp_tx_time =
      (poll_rx_ts + (PHY_TIMING[cur_phy].poll_rx_to_resp_tx_dly * UUS_TO_DWT_TIME)) >> 8;
  dwt_setdelayedtrxtime(resp_tx_time);

  // Receive FINAL automatically after this transmission.
  dwt_setrxaftertxdelay(RESP_TX_TO_FINAL_RX_DLY_UUS);
  dwt_setrxtimeout(FINAL_RX_TIMEOUT_UUS);

  tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
  dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);
  if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
    stats_add_fail(FAIL_TIMEOUT);      // slot missed; never send late
    dwt_setrxtimeout(RESPONDER_RX_WINDOW_UUS);
    return;
  }

  uint32_t fguard = millis() + 100;
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
    if (millis() > fguard) {
      stats_add_fail(FAIL_TIMEOUT);
      dwt_setrxtimeout(RESPONDER_RX_WINDOW_UUS);
      return;
    }
  }
  frame_seq_nb++;

  if (!(status_reg & SYS_STATUS_RXFCG_BIT_MASK)) {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    stats_add_fail((status_reg & SYS_STATUS_ALL_RX_TO) ? FAIL_TIMEOUT : FAIL_RX_ERROR);
    dwt_setrxtimeout(RESPONDER_RX_WINDOW_UUS);
    return;
  }

  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
  frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
  if (!ranging_final_length_valid(frame_len) || frame_len > sizeof(rx_buffer)) {
    stats_add_fail(FAIL_BAD_FRAME);
    goto restore;
  }
  dwt_readrxdata(rx_buffer, frame_len, 0);
  if (rx_buffer[9] != FN_FINAL) { stats_add_fail(FAIL_BAD_FRAME); goto restore; }
  {
    uint16_t fsrc = (uint16_t)rx_buffer[7] | ((uint16_t)rx_buffer[8] << 8);
    if (fsrc != src) { stats_add_fail(FAIL_BAD_FRAME); goto restore; }  // interleaved tag

    uint32_t poll_tx_ts, resp_rx_ts, final_tx_ts;
    final_msg_get_ts(&rx_buffer[FINAL_MSG_POLL_TX_TS_IDX],  &poll_tx_ts);
    final_msg_get_ts(&rx_buffer[FINAL_MSG_RESP_RX_TS_IDX],  &resp_rx_ts);
    final_msg_get_ts(&rx_buffer[FINAL_MSG_FINAL_TX_TS_IDX], &final_tx_ts);

    // Asymmetric DS-TWR. Every difference stays inside one device's own clock,
    // so the two oscillators are never subtracted from one another. The 32-bit
    // differences wrap correctly over these intervals.
    uint32_t poll_rx_32  = (uint32_t)poll_rx_ts;
    uint32_t resp_tx_32  =
        (uint32_t)((((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + cur_antdly);
    uint32_t final_rx_32 = (uint32_t)get_rx_timestamp_u64();

    double Ra = (double)(uint32_t)(resp_rx_ts  - poll_tx_ts);
    double Rb = (double)(uint32_t)(final_rx_32 - resp_tx_32);
    double Da = (double)(uint32_t)(final_tx_ts - resp_rx_ts);
    double Db = (double)(uint32_t)(resp_tx_32  - poll_rx_32);

    double denom = Ra + Rb + Da + Db;
    if (denom <= 0) { stats_add_fail(FAIL_BAD_FRAME); goto restore; }
    double tof = ((Ra * Rb - Da * Db) / denom) * DWT_TIME_UNITS;
    float dist = (float)(tof * SPEED_OF_LIGHT);

    // A negative or absurd range is an implementation failure, not a
    // measurement. Never let it into the statistics.
    if (!isfinite(dist) || dist < -1.0f || dist > 1000.0f) {
      stats_add_fail(FAIL_BAD_FRAME);
      goto restore;
    }

    // Diagnostics only after the exchange is complete: the ~232 byte SPI read
    // inside the deadline path made the responder miss its slot every time.
    float rssi, fp;
    link_quality(&rssi, &fp);
    // DS-TWR cancels clock error rather than correcting for it, so this is
    // reported for diagnosis only - a large offset points at crystal trim.
    float ppm = (((float)dwt_readclockoffset()) / (uint32_t)(1 << 26)) * 1e6f;
    stats_add_ok(dist, rssi, fp, ppm);

    RangeEvent ev;
    ev.addr = src;
    ev.sample = {dist, rssi, fp, ppm};
    ev.at_ms = millis();
    ev.batt_mv = 0;
    ev.misses = 0;
    ev.wake_count = 0;
    ev.moving = false;
    ev.sensor_fault = false;
    // 24-byte FINALs have no telemetry.  Keep accepting the deployed 29-byte
    // layout; 33-byte FINALs add the wake counter.
    ev.has_battery = frame_len >= FINAL_LEN_WITH_BATT;
    ev.has_telemetry = frame_len >= FINAL_LEN_WITH_TELEMETRY;
    ev.has_wake_count = frame_len >= FINAL_LEN_WITH_WAKE_COUNT;
    if (ev.has_battery)
      ev.batt_mv = (uint16_t)rx_buffer[FINAL_MSG_BATT_MV_IDX] |
                   ((uint16_t)rx_buffer[FINAL_MSG_BATT_MV_IDX + 1] << 8);
    if (ev.has_telemetry) {
      ev.moving = (rx_buffer[FINAL_MSG_FLAGS_IDX] & FINAL_FLAG_MOVING) != 0;
      ev.sensor_fault =
          (rx_buffer[FINAL_MSG_FLAGS_IDX] & FINAL_FLAG_SENSOR_FAULT) != 0;
      ev.misses = (uint16_t)rx_buffer[FINAL_MSG_MISSES_IDX] |
                  ((uint16_t)rx_buffer[FINAL_MSG_MISSES_IDX + 1] << 8);
    }
    if (ev.has_wake_count)
      ev.wake_count = (uint32_t)rx_buffer[FINAL_MSG_WAKE_COUNT_IDX] |
                      ((uint32_t)rx_buffer[FINAL_MSG_WAKE_COUNT_IDX + 1] << 8) |
                      ((uint32_t)rx_buffer[FINAL_MSG_WAKE_COUNT_IDX + 2] << 16) |
                      ((uint32_t)rx_buffer[FINAL_MSG_WAKE_COUNT_IDX + 3] << 24);
    if (xQueueSend(evt_q, &ev, 0) != pdTRUE) q_dropped++;
  }

restore:
  // Back to the plain listening window for the next tag.
  dwt_setrxtimeout(RESPONDER_RX_WINDOW_UUS);
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
    do_responder();
    vTaskDelay(pdMS_TO_TICKS(1));   // brief yield; RX is re-armed each pass
  }
}

void ranging_init() {
  prefs.begin("uwb", false);
  cur_role     = (Role)prefs.getUChar("role", ROLE_INITIATOR);
  cur_antdly   = prefs.getUShort("antdly", DEFAULT_ANT_DLY);
  cur_interval = prefs.getUShort("interval", DEFAULT_INTERVAL_MS);
  cur_xtrim    = prefs.getUChar("xtrim", 0x2E);
  cur_phy      = (Phy)prefs.getUChar("phy", PHY_LONG);
  cur_role     = ROLE_RESPONDER;   // an anchor is always the responder
  evt_q = xQueueCreate(32, sizeof(RangeEvent));
  radio_configure();
}

void ranging_start() {
  // Pinned to core 1; Wi-Fi, the web server and OTA all run on core 0.
  xTaskCreatePinnedToCore(radio_task, "uwb", 8192, nullptr, 3, &task_h, 1);
}
