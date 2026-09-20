#ifndef BR_TAG_POWER_H
#define BR_TAG_POWER_H
#include "motion_policy.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Call before settings_load. Settings contain no identity or authentication data. */
int br_power_settings_init(void);
int br_power_config_save(const struct br_motion_config *config);
void br_power_config_get(struct br_motion_config *config);
bool br_power_take_config(struct br_motion_config *config);
/* Main owner only, after policy and sensor application (including error capture). */
void br_power_config_applied(void);
int br_power_sensors_configure(const struct br_motion_config *config);
bool br_power_sensor_check(void);
bool br_power_motion_event(void);
uint16_t br_power_voltage(void);
void br_app_wake(void);
void br_app_wait(uint32_t ms);
struct br_power_status {
    uint32_t wake_count, irq_count;
    uint16_t battery_mv, misses;
    int32_t sensor_error;
    bool moving, sleeping, config_pending;
};
void br_power_observe(bool moving, uint32_t wakes, uint16_t mv, uint16_t misses, bool sleeping);
void br_power_status_get(struct br_power_status *status);
#ifdef __cplusplus
}
#endif
#endif
