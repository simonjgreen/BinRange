#ifndef BR_OTA_RUNTIME_H
#define BR_OTA_RUNTIME_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int br_ota_init(void);
uint16_t br_ota_tag_id(void);
/* Main owner polls after events/deadlines and at least every 10 s for watchdog.
 * Trial/connected/button states request a shorter interval. */
void br_ota_poll(bool radio_healthy);
bool br_ota_radio_paused(void);
bool br_ota_trial(void);
uint32_t br_ota_poll_interval(void);
#ifdef __cplusplus
}
#endif
#endif
