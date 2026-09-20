#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct br_motion_config {
    uint32_t moving_ms;
    uint32_t idle_ms;
    uint32_t quiet_ms;
    uint32_t threshold_mg;
    uint32_t duration_samples;
} br_motion_config;

typedef struct br_motion_state {
    br_motion_config config;
    bool moving;
    uint32_t wake_count;
    uint64_t next_report_at;
    uint64_t quiet_deadline;
    uint64_t last_now;
    bool report_pending;
    uint8_t idle_retries;
} br_motion_state;

void br_motion_defaults(br_motion_config *config);
bool br_motion_config_valid(const br_motion_config *config);
void br_motion_init(br_motion_state *state, const br_motion_config *config,
                    uint64_t now);
bool br_motion_update(br_motion_state *state, uint64_t now,
                      bool motion_event);
bool br_motion_due(const br_motion_state *state, uint64_t now);
void br_motion_reported(br_motion_state *state, uint64_t now);
/* Local TX success is not proof that the anchor received the FINAL. */
void br_motion_attempted(br_motion_state *state, uint64_t now, bool sent);
bool br_motion_reconfigure(br_motion_state *state,
                           const br_motion_config *config, uint64_t now);

#ifdef __cplusplus
}
#endif
