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
static volatile Phy cur_phy = PHY_LONG;
static volatile uint16_t cur_tag_addr = DEFAULT_TAG_ADDR;
static uint16_t tag_ids[MAX_TAG_IDS] = {DEFAULT_TAG_ADDR};
static volatile uint8_t tag_id_count = 1;
static volatile uint8_t tag_idx;

// Per-identity motion state.
static uint32_t tag_last_tick[MAX_TAG_IDS];
static uint32_t tag_motion_until[MAX_TAG_IDS];
static uint32_t tag_wakes[MAX_TAG_IDS];
static float tag_uah_used[MAX_TAG_IDS];
static uint16_t tag_misses[MAX_TAG_IDS];   // failed polls since the last success
static volatile uint32_t drain_mult = DEFAULT_DRAIN_MULT;
static volatile uint32_t motion_tick_ms = DEFAULT_MOTION_TICK_MS;
static volatile uint32_t idle_tick_ms   = DEFAULT_IDLE_TICK_MS;
static volatile uint32_t motion_hold_ms = DEFAULT_MOTION_HOLD_MS;
static void apply_tag_addr(uint16_t addr);
static volatile uint16_t cur_burst    = DEFAULT_BURST_SIZE;
static volatile uint32_t cur_event_ms = DEFAULT_EVENT_MS;
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

// DS-TWR. Bytes: FC(2) seq(1) PAN(2) dest(2) src(2) fn(1) [payload].
// The tag is the initiator: POLL out, RESP in, FINAL out carrying our three
// timestamps so the anchor can compute the range.
static uint8_t tx_poll_msg[]  = {0x41, 0x88, 0, 0xCA, 0xDE, 'W','A', 'V','E', FN_POLL, 0, 0};
static uint8_t rx_resp_msg[]  = {0x41, 0x88, 0, 0xCA, 0xDE, 'V','E', 'W','A', FN_RESP, 0x02, 0, 0, 0};
// 10 byte header, 12 bytes of timestamps, 2 battery, 1 flags, 2 misses,
// 2 FCS placeholders.
static uint8_t tx_final_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W','A', 'V','E', FN_FINAL,
                                 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0, 0, 0,0, 0, 0};

// final_msg_set_ts / final_msg_get_ts come from the DW3000 library.

// The address lives only in message bytes - frame filtering is off and the
// anchor validates in software - so an identity change is four byte writes,
// not a radio reconfigure.
static void apply_tag_addr(uint16_t addr) {
    cur_tag_addr = addr;
    tx_poll_msg[7] = tx_final_msg[7] = (uint8_t)(addr & 0xFF);
    tx_poll_msg[8] = tx_final_msg[8] = (uint8_t)(addr >> 8);
    rx_resp_msg[5] = tx_poll_msg[7];
    rx_resp_msg[6] = tx_poll_msg[8];
}

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
uint16_t ranging_tag_addr()   { return cur_tag_addr; }
uint16_t ranging_burst_size() { return cur_burst; }
uint32_t ranging_event_ms()   { return cur_event_ms; }
void ranging_set_tag_addr(uint16_t a) {
    tag_ids[0] = a; tag_id_count = 1; tag_idx = 0;
    apply_tag_addr(a);
    prefs.putUShort("tagaddr", a);
    prefs.putString("tagids", "");
}

uint8_t ranging_tag_id_count() { return tag_id_count; }
bool ranging_tag_moving(uint8_t i) {
    return i < tag_id_count && (int32_t)(tag_motion_until[i] - millis()) > 0;
}
uint32_t ranging_tag_wakes(uint8_t i) { return i < tag_id_count ? tag_wakes[i] : 0; }
uint16_t ranging_tag_misses(uint8_t i) { return i < tag_id_count ? tag_misses[i] : 0; }

// Index of the identity currently transmitting, for billing misses correctly.
static int8_t current_slot() {
    for (uint8_t i = 0; i < tag_id_count; i++)
        if (tag_ids[i] == cur_tag_addr) return (int8_t)i;
    return -1;
}

// A coin cell holds near its nominal voltage for most of its life and then
// falls off a cliff, which is exactly why voltage alone is a poor gauge.
static uint16_t batt_mv_for(float uah_used) {
    float frac = uah_used / BATT_CAPACITY_UAH;
    if (frac >= 1.0f) return 2000;
    if (frac < 0.8f) return (uint16_t)(3000.0f - 150.0f * (frac / 0.8f));
    return (uint16_t)(2850.0f - 850.0f * ((frac - 0.8f) / 0.2f));
}

uint16_t ranging_tag_batt_mv(uint8_t i) {
    return i < tag_id_count ? batt_mv_for(tag_uah_used[i]) : 0;
}
float ranging_tag_uah(uint8_t i) { return i < tag_id_count ? tag_uah_used[i] : 0; }
uint32_t ranging_drain_mult() { return drain_mult; }
void ranging_set_drain_mult(uint32_t v) { drain_mult = v; prefs.putUInt("drain", v); }
void ranging_reset_battery() {
    for (uint8_t i = 0; i < MAX_TAG_IDS; i++) tag_uah_used[i] = 0;
}
uint32_t ranging_motion_tick_ms() { return motion_tick_ms; }
uint32_t ranging_idle_tick_ms()   { return idle_tick_ms; }
uint32_t ranging_motion_hold_ms() { return motion_hold_ms; }
void ranging_set_motion_tick_ms(uint32_t v) { motion_tick_ms = v < 500 ? 500 : v; prefs.putUInt("mtick", motion_tick_ms); }
void ranging_set_idle_tick_ms(uint32_t v)   { idle_tick_ms   = v < 1000 ? 1000 : v; prefs.putUInt("itick", idle_tick_ms); }
void ranging_set_motion_hold_ms(uint32_t v) { motion_hold_ms = v; prefs.putUInt("mhold", motion_hold_ms); }

// Stands in for the LIS3DH interrupt: the tag has substantially moved.
bool ranging_trigger_motion(uint16_t addr) {
    for (uint8_t i = 0; i < tag_id_count; i++) {
        if (tag_ids[i] != addr) continue;
        bool was_moving = (int32_t)(tag_motion_until[i] - millis()) > 0;
        tag_motion_until[i] = millis() + motion_hold_ms;
        if (!was_moving) tag_wakes[i]++;     // count wake events, not extensions
        tag_last_tick[i] = 0;                // range immediately
        Serial.printf("[tag] %04x motion triggered, fast tick for %us\n",
                      addr, (unsigned)(motion_hold_ms / 1000));
        return true;
    }
    return false;
}

bool ranging_settle(uint16_t addr) {
    for (uint8_t i = 0; i < tag_id_count; i++) {
        if (tag_ids[i] != addr) continue;
        tag_motion_until[i] = 0;
        tag_last_tick[i] = 0;   // one final range on settling, as a real tag would
        Serial.printf("[tag] %04x settled\n", addr);
        return true;
    }
    return false;
}
uint16_t ranging_tag_id_at(uint8_t i) { return i < tag_id_count ? tag_ids[i] : 0; }

// Comma separated hex, e.g. "b001,b002,b003".
void ranging_set_tag_ids(const char *csv) {
    uint8_t n = 0;
    const char *p = csv;
    while (*p && n < MAX_TAG_IDS) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        tag_ids[n++] = (uint16_t)strtoul(p, nullptr, 16);
        while (*p && *p != ',') p++;
    }
    if (!n) return;
    tag_id_count = n;
    tag_idx = 0;
    apply_tag_addr(tag_ids[0]);
    prefs.putString("tagids", csv);
    Serial.printf("[tag] rotating %u identities\n", n);
}
void ranging_set_burst_size(uint16_t n) { cur_burst = n ? n : 1; prefs.putUShort("burst", cur_burst); }
void ranging_set_event_ms(uint32_t ms)  { cur_event_ms = ms < 200 ? 200 : ms; prefs.putUInt("eventms", cur_event_ms); }

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

  apply_tag_addr(cur_tag_addr);

  dwt_setrxaftertxdelay(RESP_RX_AFTER_TX_DLY_UUS);
  dwt_setrxtimeout(RESP_RX_TIMEOUT_DS_UUS);
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  radio_ok = true;
  Serial.printf("[uwb] configured as %s, antenna delay %u\n",
                cur_role == ROLE_INITIATOR ? "INITIATOR" : "RESPONDER", cur_antdly);
  return true;
}

// ---- Initiator: one DS-TWR exchange ------------------------------------
// The tag learns no distance. It hands the anchor the timestamps the anchor
// needs, then sleeps.
// Every failed poll is invisible to the anchor, so the tag must count it.
static void note_miss() {
    int8_t i = current_slot();
    if (i >= 0 && tag_misses[i] < 0xFFFF) tag_misses[i]++;
}

static void do_initiator() {
  tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

  uint32_t guard = millis() + 100;
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
    if (millis() > guard) {
      dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
      note_miss(); stats_add_fail(FAIL_TIMEOUT);
      return;
    }
  }
  frame_seq_nb++;

  if (!(status_reg & SYS_STATUS_RXFCG_BIT_MASK)) {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    note_miss(); stats_add_fail((status_reg & SYS_STATUS_ALL_RX_TO) ? FAIL_TIMEOUT : FAIL_RX_ERROR);
    return;
  }

  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
  uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
  if (frame_len > sizeof(rx_buffer)) { note_miss(); stats_add_fail(FAIL_BAD_FRAME); return; }
  dwt_readrxdata(rx_buffer, frame_len, 0);
  rx_buffer[ALL_MSG_SN_IDX] = 0;
  if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) != 0) {
    note_miss(); stats_add_fail(FAIL_BAD_FRAME);
    return;
  }

  // Schedule FINAL far enough ahead that its preamble fits before the
  // programmed RMARKER, then embed the three timestamps it must carry.
  uint64_t poll_tx_ts = get_tx_timestamp_u64();
  uint64_t resp_rx_ts = get_rx_timestamp_u64();
  uint32_t final_tx_time =
      (resp_rx_ts + (RESP_RX_TO_FINAL_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
  dwt_setdelayedtrxtime(final_tx_time);
  uint64_t final_tx_ts =
      (((uint64_t)(final_tx_time & 0xFFFFFFFEUL)) << 8) + cur_antdly;

  // Bill this exchange to whichever identity is transmitting, then load the
  // telemetry the anchor cannot observe: battery, motion state, and the misses
  // accumulated since this tag last got through.
  int8_t slot = current_slot();
  if (slot >= 0) {
    tag_uah_used[slot] += BATT_UAH_PER_EXCH * (float)drain_mult;
    uint16_t mv = batt_mv_for(tag_uah_used[slot]);
    tx_final_msg[FINAL_MSG_BATT_MV_IDX]     = (uint8_t)(mv & 0xFF);
    tx_final_msg[FINAL_MSG_BATT_MV_IDX + 1] = (uint8_t)(mv >> 8);

    bool moving = (int32_t)(tag_motion_until[slot] - millis()) > 0;
    tx_final_msg[FINAL_MSG_FLAGS_IDX] = moving ? FINAL_FLAG_MOVING : 0;

    tx_final_msg[FINAL_MSG_MISSES_IDX]     = (uint8_t)(tag_misses[slot] & 0xFF);
    tx_final_msg[FINAL_MSG_MISSES_IDX + 1] = (uint8_t)(tag_misses[slot] >> 8);
  }

  final_msg_set_ts(&tx_final_msg[FINAL_MSG_POLL_TX_TS_IDX],  poll_tx_ts);
  final_msg_set_ts(&tx_final_msg[FINAL_MSG_RESP_RX_TS_IDX],  resp_rx_ts);
  final_msg_set_ts(&tx_final_msg[FINAL_MSG_FINAL_TX_TS_IDX], final_tx_ts);

  tx_final_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_final_msg), tx_final_msg, 0);
  dwt_writetxfctrl(sizeof(tx_final_msg), 0, 1);
  if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
    // Slot missed. Never transmit late with a stale predicted timestamp.
    note_miss(); stats_add_fail(FAIL_TIMEOUT);
    return;
  }

  uint32_t tguard = millis() + 100;
  while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) {
    if (millis() > tguard) { note_miss(); stats_add_fail(FAIL_TIMEOUT); return; }
  }
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  frame_seq_nb++;

  if (slot >= 0) tag_misses[slot] = 0;   // reported; start a fresh count

  float rssi, fp;
  link_quality(&rssi, &fp);
  stats_add_ok(NAN, rssi, fp, 0.0f);   // the tag computes no distance
#ifdef NO_WIFI
  Serial.printf("D exchange ok %.1f %.1f\n", rssi, fp);
#endif
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
    // Each identity keeps its own schedule: fast while moving, slow otherwise.
    // Whichever is due next transmits; the rest stay silent, which is what
    // makes the anchor's per-tag burst detection meaningful.
    uint32_t now = millis();
    for (uint8_t i = 0; i < tag_id_count; i++) {
      bool moving = (int32_t)(tag_motion_until[i] - now) > 0;
      uint32_t due = moving ? motion_tick_ms : idle_tick_ms;
      if (tag_last_tick[i] && (uint32_t)(now - tag_last_tick[i]) < due) continue;

      apply_tag_addr(tag_ids[i]);
      for (uint16_t b = 0; b < cur_burst; b++) {
        do_initiator();
        vTaskDelay(pdMS_TO_TICKS(cur_interval));
      }
      tag_last_tick[i] = millis();
      Serial.printf("[tag] %04x %s burst of %u\n",
                    tag_ids[i], moving ? "MOVING" : "idle", cur_burst);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void ranging_init() {
  prefs.begin("uwb", false);
  cur_role     = (Role)prefs.getUChar("role", ROLE_INITIATOR);
  cur_antdly   = prefs.getUShort("antdly", DEFAULT_ANT_DLY);
  cur_interval = prefs.getUShort("interval", DEFAULT_INTERVAL_MS);
  cur_xtrim    = prefs.getUChar("xtrim", 0x2E);
  cur_phy      = (Phy)prefs.getUChar("phy", PHY_LONG);
  drain_mult = prefs.getUInt("drain", DEFAULT_DRAIN_MULT);
  motion_tick_ms = prefs.getUInt("mtick", DEFAULT_MOTION_TICK_MS);
  idle_tick_ms   = prefs.getUInt("itick", DEFAULT_IDLE_TICK_MS);
  motion_hold_ms = prefs.getUInt("mhold", DEFAULT_MOTION_HOLD_MS);
  cur_tag_addr = prefs.getUShort("tagaddr", DEFAULT_TAG_ADDR);
  tag_ids[0] = cur_tag_addr;
  tag_id_count = 1;
  {
      String ids = prefs.getString("tagids", "");
      if (ids.length()) ranging_set_tag_ids(ids.c_str());
  }
  cur_burst    = prefs.getUShort("burst", DEFAULT_BURST_SIZE);
  cur_event_ms = prefs.getUInt("eventms", DEFAULT_EVENT_MS);
  cur_role     = ROLE_INITIATOR;   // a tag always initiates
  radio_configure();
}

void ranging_start() {
  // Pinned to core 1; Wi-Fi, the web server and OTA all run on core 0.
  xTaskCreatePinnedToCore(radio_task, "uwb", 8192, nullptr, 3, &task_h, 1);
}
