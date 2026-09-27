/* System ON idle: the motion IRQ, BLE callbacks and key can wake the main owner.
 * No System OFF, automatic UICR programming or assumed battery percentage. */
#include "tag_power.h"
#include "motion_settings.h"
#include "tip_monitor.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <hal/nrf_saadc.h>
#include <errno.h>
#include <string.h>

static K_SEM_DEFINE(wake, 0, 1);
static K_MUTEX_DEFINE(config_lock);
static struct br_motion_settings motion_settings;
static struct br_power_status status = { .sensor_error = -ENODEV, .tip_age_s = UINT32_MAX };
static br_tip_record tip_saved;
static br_tip_monitor tip_monitor;
static int tip_load_error;
static bool tip_started;
static atomic_t motion_pending, irq_count;
static const struct device *const accel = DEVICE_DT_GET(DT_NODELABEL(accel));
static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(accel));
static const struct device *const adc = DEVICE_DT_GET(DT_NODELABEL(adc));
static const struct sensor_trigger trigger = {
    .type = SENSOR_TRIG_DELTA, .chan = SENSOR_CHAN_ACCEL_XYZ,
};
static bool adc_ready, calibrated;

void br_app_wake(void) { k_sem_give(&wake); }
void br_app_wait(uint32_t ms) { (void)k_sem_take(&wake, K_MSEC(ms)); }

static int config_load(const char *name, size_t len, settings_read_cb read, void *arg) {
    if (!strcmp(name, "tip_v1")) {
        if (len != sizeof(tip_saved) || read(arg, &tip_saved, sizeof(tip_saved)) != sizeof(tip_saved) ||
            !br_tip_record_valid(&tip_saved)) {
            tip_load_error = -EINVAL;
            return tip_load_error;
        }
        return 0;
    }
    struct br_motion_config next;
    if (strcmp(name, "v1")) return -ENOENT;
    if (len != sizeof(next) || read(arg, &next, sizeof(next)) != sizeof(next) ||
        !br_motion_config_valid(&next)) return -EINVAL;
    br_motion_settings_saved(&motion_settings, &next);
    return 0;
}
static struct settings_handler settings = { .name = "motion", .h_set = config_load };

int br_power_settings_init(void) {
    br_motion_settings_init(&motion_settings);
    return settings_register(&settings);
}
void br_power_config_get(struct br_motion_config *config) {
    k_mutex_lock(&config_lock, K_FOREVER);
    *config = motion_settings.wanted;
    k_mutex_unlock(&config_lock);
}
int br_power_config_save(const struct br_motion_config *config) {
    if (!br_motion_config_valid(config)) return -EINVAL;
    k_mutex_lock(&config_lock, K_FOREVER);
    /* Do not claim a change accepted if persistence failed. One complete record. */
    int rc = settings_save_one("motion/v1", config, sizeof(*config));
    if (!rc) br_motion_settings_saved(&motion_settings, config);
    k_mutex_unlock(&config_lock);
    if (!rc) br_app_wake();
    return rc;
}
bool br_power_take_config(struct br_motion_config *config) {
    k_mutex_lock(&config_lock, K_FOREVER);
    bool pending = br_motion_settings_take(&motion_settings, config);
    k_mutex_unlock(&config_lock);
    return pending;
}
void br_power_config_applied(void) {
    k_mutex_lock(&config_lock, K_FOREVER);
    br_motion_settings_applied(&motion_settings);
    k_mutex_unlock(&config_lock);
}
static void motion(const struct device *dev, const struct sensor_trigger *trig) {
    ARG_UNUSED(dev); ARG_UNUSED(trig);
    atomic_inc(&irq_count);
    atomic_set(&motion_pending, 1);
    br_app_wake();
}
bool br_power_motion_event(void) { return atomic_set(&motion_pending, 0) != 0; }
static int sensor_result(int rc) {
    k_mutex_lock(&config_lock, K_FOREVER);
    status.sensor_error = rc;
    k_mutex_unlock(&config_lock);
    return rc;
}
int br_power_sensors_configure(const struct br_motion_config *config) {
    /* P0.09 is an NFC pin until explicitly provisioned. Never change UICR here. */
    if ((NRF_UICR->NFCPINS & 1) || !device_is_ready(accel)) return sensor_result(-ENODEV);
    int rc = sensor_trigger_set(accel, &trigger, NULL);
    if (rc) return sensor_result(rc);
    int64_t threshold = (int64_t)config->threshold_mg * SENSOR_G / 1000;
    struct sensor_value value = { .val1 = threshold / 1000000, .val2 = threshold % 1000000 };
    rc = sensor_attr_set(accel, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SLOPE_TH, &value);
    if (rc) return sensor_result(rc);
    value.val1 = config->duration_samples; value.val2 = 0;
    rc = sensor_attr_set(accel, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SLOPE_DUR, &value);
    if (rc) return sensor_result(rc);
    /* CONFIG_LIS2DH_ACCEL_HP_FILTERS exposes this setter; it does not enable
     * filtering. HPIS1 routes the normal-mode high-pass output to INT1 so
     * static gravity cannot continuously retrigger the motion interrupt. */
    value.val1 = 0x01; value.val2 = 0;
    rc = sensor_attr_set(accel, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION, &value);
    if (rc) return sensor_result(rc);
    /* Reset the normal-mode high-pass reference after a new configuration. */
    uint8_t reference;
    rc = i2c_reg_read_byte_dt(&bus, 0x26, &reference);
    if (rc) return sensor_result(rc);
    rc = sensor_trigger_set(accel, &trigger, motion);
    if (rc) return sensor_result(rc);
    /* The pinned driver's trigger setup runs on the system work queue. Verify
     * its actual registers too: trigger_set alone cannot report deferred errors. */
    k_msleep(20);
    return br_power_sensor_check() ? 0 : -EIO;
}
bool br_power_sensor_check(void) {
    uint8_t id = 0, ctrl1 = 0, ctrl2 = 0, ctrl3 = 0, interrupt = 0;
    int rc = -ENODEV;
    if (!(NRF_UICR->NFCPINS & 1) && device_is_ready(accel)) {
        rc = i2c_reg_read_byte_dt(&bus, 0x0f, &id);
        if (!rc) rc = i2c_reg_read_byte_dt(&bus, 0x20, &ctrl1);
        if (!rc) rc = i2c_reg_read_byte_dt(&bus, 0x21, &ctrl2);
        if (!rc) rc = i2c_reg_read_byte_dt(&bus, 0x22, &ctrl3);
        if (!rc) rc = i2c_reg_read_byte_dt(&bus, 0x30, &interrupt);
        if (!rc && (id != 0x33 || ctrl1 != 0x3f || ctrl2 != 0x01 ||
                    !(ctrl3 & 0x40) || interrupt != 0x2a))
            rc = -EIO;
    }
    sensor_result(rc);
    return rc == 0;
}
static int tip_read(void *context, int32_t sample[3]) {
    ARG_UNUSED(context);
    if (status.sensor_error) return status.sensor_error;
    /* The pinned LIS2DH fetch returns success even when DRDY was clear.
     * Check ZYXDA explicitly so the debounce never counts a held sample twice. */
    uint8_t data_status;
    int rc = i2c_reg_read_byte_dt(&bus, 0x27, &data_status);
    if (rc) return rc;
    if (!(data_status & 0x08)) return -ENODATA;
    struct sensor_value values[3];
    rc = sensor_sample_fetch(accel);
    if (!rc) rc = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, values);
    if (rc) return rc;
    for (unsigned i=0; i<3; ++i) {
        int64_t micro = (int64_t)values[i].val1 * 1000000 + values[i].val2;
        sample[i] = (int32_t)(micro * 1000 / SENSOR_G);
    }
    return 0;
}
static int tip_save(void *context, const br_tip_record *record) {
    ARG_UNUSED(context);
    return settings_save_one("motion/tip_v1", record, sizeof(*record));
}
bool br_power_tip_update(uint64_t now, bool moving, bool *event) {
    if (!tip_started) {
        br_tip_monitor_init(&tip_monitor, &tip_saved, tip_load_error);
        tip_started = true;
    }
    uint32_t before = tip_monitor.committed_count;
    bool changed = br_tip_monitor_update(&tip_monitor, now, moving, tip_read, tip_save, NULL);
    *event = before != tip_monitor.committed_count;
    k_mutex_lock(&config_lock, K_FOREVER);
    status.tip_count = tip_monitor.committed_count;
    status.tip_age_s = br_tip_age_s(&tip_monitor.policy, now);
    status.tip_ready = br_tip_monitor_ready(&tip_monitor);
    status.tip_error = tip_monitor.error;
    k_mutex_unlock(&config_lock);
    return changed;
}
uint64_t br_power_tip_deadline(void) { return br_tip_monitor_deadline(&tip_monitor); }
uint16_t br_power_voltage(void) {
    if (!adc_ready) {
        const struct adc_channel_cfg channel = {
            .gain = ADC_GAIN_1_6, .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
            /* Zephyr's nrfx input shim, not the raw HAL PSELP register enum. */
            .channel_id = 0, .input_positive = NRF_SAADC_VDD,
        };
        if (!device_is_ready(adc) || adc_channel_setup(adc, &channel)) return 0;
        adc_ready = true;
    }
    int16_t raw;
    struct adc_sequence sequence = {
        .channels = BIT(0), .buffer = &raw, .buffer_size = sizeof(raw),
        .resolution = 14, .oversampling = 4, .calibrate = !calibrated,
    };
    if (adc_read(adc, &sequence)) return 0;
    calibrated = true;
    int32_t mv = raw;
    if (adc_raw_to_millivolts(adc_ref_internal(adc), ADC_GAIN_1_6, 14, &mv) ||
        mv < 1500 || mv > 3600) return 0;
    return (uint16_t)mv;
}
void br_power_observe(bool moving, uint32_t wakes, uint16_t mv, uint16_t misses, bool sleeping) {
    k_mutex_lock(&config_lock, K_FOREVER);
    status.moving = moving; status.wake_count = wakes; status.battery_mv = mv;
    status.misses = misses; status.sleeping = sleeping;
    k_mutex_unlock(&config_lock);
}
void br_power_status_get(struct br_power_status *out) {
    k_mutex_lock(&config_lock, K_FOREVER);
    *out = status;
    out->config_pending = motion_settings.pending;
    out->irq_count = (uint32_t)atomic_get(&irq_count);
    k_mutex_unlock(&config_lock);
}
