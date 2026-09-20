#include <zephyr/kernel.h>
#include "dw3000_device_api.h"
#include "dw3000_regs.h"
#include "protocol.h"
#include "radio_port.h"
#ifdef BINRANGE_OTA
#include "ota_runtime.h"
#include "tag_power.h"
#endif

static uint16_t TAG = 0xb100;
static constexpr uint16_t ANT_DELAY = 16356; // uncalibrated starting point

volatile struct {
    uint32_t magic, phase;
    int32_t error;
    uint32_t radio_id, attempts, responses, finals, timeouts, bad_frames, late_tx;
    uint32_t last_status, tag_id;
    int32_t final_margin_uus;
    uint32_t late_status, late_state;
} tag_diag = {0x42525247, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xb100, 0, 0, 0};

static dwt_config_t config = {
    5, DWT_PLEN_1024, DWT_PAC32, 9, 9, 2, DWT_BR_850K,
    DWT_PHRMODE_STD, DWT_PHRRATE_STD, (1025+16-32),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
static dwt_txconfig_t tx_power = {0x34, 0xfdfdfdfd, 0};

static uint64_t timestamp(bool tx) {
    uint8_t b[5];
    if (tx) dwt_readtxtimestamp(b); else dwt_readrxtimestamp(b);
    uint64_t value = 0;
    for (int i=4; i>=0; --i) value = (value << 8) | b[i];
    return value;
}

static bool wait_status(uint32_t mask) {
    int64_t deadline = k_uptime_get() + 100;
    do {
        tag_diag.last_status = dwt_read32bitreg(SYS_STATUS_ID);
        if (tag_diag.last_status & mask) return true;
    } while (k_uptime_get() < deadline);
    return false;
}

static bool configure() {
    tag_diag.phase = 1;
    tag_diag.error = radio_port_init();
    if (tag_diag.error) return false;
    tag_diag.radio_id = dwt_readdevid();
    if (tag_diag.radio_id != DWT_C0_DEV_ID) { tag_diag.error = -1; return false; }
    tag_diag.phase = 2;
    dwt_softreset();
    k_msleep(2);
    int64_t deadline = k_uptime_get() + 1000;
    while (!dwt_checkidlerc()) {
        if (k_uptime_get() >= deadline) { tag_diag.error = -2; return false; }
    }
    tag_diag.phase = 3;
    if (dwt_initialise(DWT_DW_INIT) != DWT_SUCCESS) { tag_diag.error = -3; return false; }
    tag_diag.phase = 4;
    if (dwt_configure(&config) != DWT_SUCCESS) { tag_diag.error = -4; return false; }
    dwt_configuretxrf(&tx_power);
    /* Keep the tag's OTP crystal trim, unlike the uncalibrated emulator. */
    dwt_setrxantennadelay(ANT_DELAY);
    dwt_settxantennadelay(ANT_DELAY);
    tag_diag.phase = 5;
    return true;
}

static bool exchange(uint8_t sequence, uint16_t mv, uint8_t flags, uint16_t misses,
                     uint32_t wakes) {
    uint8_t poll[12], response[14], final[BR_FINAL_TELEMETRY_LEN];
    br_poll(poll, TAG, sequence);
    dwt_setrxaftertxdelay(100);
    dwt_setrxtimeout(6000);
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(poll), poll, 0);
    dwt_writetxfctrl(sizeof(poll), 0, 1);
    tag_diag.attempts++;
    tag_diag.phase = 10;
    if (dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
        tag_diag.late_tx++; return false;
    }
    if (!wait_status(SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)
            || !(tag_diag.last_status & SYS_STATUS_RXFCG_BIT_MASK)) {
        tag_diag.timeouts++; return false;
    }
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    uint32_t length = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFLEN_BIT_MASK;
    if (length != sizeof(response)) { tag_diag.bad_frames++; return false; }
    dwt_readrxdata(response, length, 0);
    if (!br_response_valid(response, length, TAG)) { tag_diag.bad_frames++; return false; }
    tag_diag.responses++;
    tag_diag.phase = 11;
    uint64_t poll_tx = timestamp(true), response_rx = timestamp(false);
    uint32_t delayed = br_final_schedule(response_rx);
    uint64_t final_tx = br_final_timestamp(delayed, ANT_DELAY);
    dwt_setdelayedtrxtime(delayed);
    br_final_telemetry(final, TAG, sequence + 1, poll_tx, response_rx, final_tx,
                       mv, flags, misses, wakes);
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(final), final, 0);
    dwt_writetxfctrl(sizeof(final), 0, 1);
    tag_diag.final_margin_uus = static_cast<int32_t>(delayed - dwt_readsystimestamphi32()) / 256;
    if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
        tag_diag.late_tx++;
        tag_diag.late_status = dwt_read32bitreg(SYS_STATUS_ID);
        tag_diag.late_state = dwt_read32bitreg(SYS_STATE_LO_ID);
        return false;
    }
    tag_diag.phase = 12;
    if (wait_status(SYS_STATUS_TXFRS_BIT_MASK)) { tag_diag.finals++; return true; }
    tag_diag.timeouts++;
    return false;
}

static void idle_radio() {
    dwt_forcetrxoff();
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK |
        SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    tag_diag.phase = 5;
}
#ifdef BINRANGE_OTA
static uint32_t sleeping_channel;
static void sleep_radio() {
    idle_radio();
    sleeping_channel = dwt_read32bitreg(CHAN_CTRL_ID);
    dwt_configuresleep(DWT_CONFIG | DWT_PGFCAL | DWT_GOTOIDLE,
                      DWT_WAKE_CSN | DWT_SLP_EN);
    dwt_entersleep(DWT_DW_IDLE);
}
static bool wake_radio(bool recover = true) {
    wakeup_device_with_io();
    if (dwt_readdevid() != DWT_C0_DEV_ID ||
        dwt_read32bitreg(CHAN_CTRL_ID) != sleeping_channel) return recover && configure();
    /* AON does not retain every radio register; restore the vendor's required
     * LDO/bias, DGC and indirect-access settings after each wake. */
    dwt_restoreconfig();
    dwt_configuretxrf(&tx_power);
    dwt_setrxantennadelay(ANT_DELAY);
    dwt_settxantennadelay(ANT_DELAY);
    return true;
}
#endif

int main() {
#ifdef BINRANGE_OTA
    int rc = br_ota_init();
    if (rc) { tag_diag.error = rc; return 0; }
    TAG = br_ota_tag_id();
    tag_diag.tag_id = TAG;
#endif
    if (!configure()) return 0;
    uint8_t sequence = 0;
#ifdef BINRANGE_OTA
    br_motion_config tuning;
    br_power_take_config(&tuning);
    br_motion_state motion;
    br_motion_init(&motion, &tuning, k_uptime_get());
    bool sensor_ok = br_power_sensors_configure(&tuning) == 0;
    br_power_config_applied();
    // Exercise this candidate's wake/restore path before its local trial gate.
    sleep_radio();
    k_msleep(10);
    const bool sleep_path_verified = wake_radio(false);
    bool radio_ok = sleep_path_verified, sleeping = false;
    uint16_t voltage = 0, misses = 0;
    uint64_t sensor_check_at = 0;
    while (true) {
        uint64_t now = k_uptime_get();
        if (br_power_take_config(&tuning)) {
            br_motion_reconfigure(&motion, &tuning, now);
            sensor_ok = br_power_sensors_configure(&tuning) == 0;
            br_power_config_applied();
        }
        if (now >= sensor_check_at) {
            bool was_ok = sensor_ok;
            sensor_ok = br_power_sensor_check();
            if (sensor_ok != was_ok) motion.report_pending = true;
            sensor_check_at = now + 10000;
        }
        bool event = br_power_motion_event();
        br_motion_update(&motion, now, event && sensor_ok);
        bool trial = br_ota_trial();
        if (!br_ota_radio_paused() && br_motion_due(&motion, now)) {
            if (sleeping) { radio_ok = wake_radio(); sleeping = false; }
            else if (!radio_ok) radio_ok = configure();
            voltage = br_power_voltage();
            uint8_t flags = (motion.moving ? BR_FINAL_FLAG_MOVING : 0) |
                (sensor_ok ? 0 : BR_FINAL_FLAG_SENSOR_FAULT);
            const bool sent = radio_ok &&
                exchange(sequence, voltage, flags, misses, motion.wake_count);
            if (!sent && misses != UINT16_MAX) ++misses;
            sequence += 2;
            if (radio_ok) idle_radio();
            br_motion_attempted(&motion, k_uptime_get(), sent);
        }
        if (trial && !sleeping) radio_ok = dwt_readdevid() == DWT_C0_DEV_ID;
        if (!trial && !sleeping && radio_ok) {
            sleep_radio();
            sleeping = true;
        }
        br_power_observe(motion.moving, motion.wake_count, voltage, misses, sleeping);
        // A recovered awake radio must not erase a failed initial wake test.
        br_ota_poll(radio_ok && sleep_path_verified);
        now = k_uptime_get();
        uint64_t deadline = now + br_ota_poll_interval();
        if (!br_ota_radio_paused()) {
            if (motion.report_pending) deadline = now;
            else if (motion.next_report_at < deadline) deadline = motion.next_report_at;
        }
        if (motion.moving && motion.quiet_deadline < deadline) deadline = motion.quiet_deadline;
        if (sensor_check_at < deadline) deadline = sensor_check_at;
        br_app_wait(deadline > now ? static_cast<uint32_t>(deadline - now) : 0);
    }
#else
    while (true) {
        exchange(sequence, 0, BR_FINAL_FLAG_SENSOR_FAULT, 0, 0);
        sequence += 2;
        idle_radio();
        k_msleep(5000);
    }
#endif
    return 0;
}
