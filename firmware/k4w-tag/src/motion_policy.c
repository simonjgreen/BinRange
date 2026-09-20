#include "motion_policy.h"

#include <limits.h>
#include <stddef.h>

static uint64_t monotonic_now(const br_motion_state *state, uint64_t now) {
    return now < state->last_now ? state->last_now : now;
}

static uint64_t add_ms(uint64_t now, uint32_t duration_ms) {
    const uint64_t duration = duration_ms;
    if (UINT64_MAX - now < duration) return UINT64_MAX;
    return now + duration;
}

void br_motion_defaults(br_motion_config *config) {
    if (!config) return;
    config->moving_ms = 5000;
    config->idle_ms = 600000;
    config->quiet_ms = 30000;
    config->threshold_mg = 250;
    config->duration_samples = 2;
}

bool br_motion_config_valid(const br_motion_config *config) {
    if (!config) return false;
    return config->moving_ms >= 1000 && config->moving_ms <= 60000 &&
           config->idle_ms >= 60000 && config->idle_ms <= 3600000 &&
           config->quiet_ms >= 5000 && config->quiet_ms <= 300000 &&
           config->threshold_mg >= 32 && config->threshold_mg <= 1000 &&
           config->duration_samples >= 1 && config->duration_samples <= 127 &&
           config->idle_ms >= config->moving_ms;
}

void br_motion_init(br_motion_state *state, const br_motion_config *config,
                    uint64_t now) {
    if (!state) return;
    br_motion_config defaults;
    if (!br_motion_config_valid(config)) {
        br_motion_defaults(&defaults);
        config = &defaults;
    }
    state->config = *config;
    state->moving = false;
    state->wake_count = 0;
    state->next_report_at = now;
    state->quiet_deadline = now;
    state->last_now = now;
    state->report_pending = true;
    state->idle_retries = 0;
}

bool br_motion_due(const br_motion_state *state, uint64_t now) {
    if (!state) return false;
    now = monotonic_now(state, now);
    return state->report_pending || now >= state->next_report_at ||
           (state->moving && now >= state->quiet_deadline);
}

bool br_motion_update(br_motion_state *state, uint64_t now,
                      bool motion_event) {
    if (!state) return false;
    now = monotonic_now(state, now);
    state->last_now = now;

    if (motion_event) {
        if (!state->moving) {
            state->moving = true;
            state->idle_retries = 0;
            if (state->wake_count != UINT32_MAX) state->wake_count++;
            state->report_pending = true;
        }
        state->quiet_deadline = add_ms(now, state->config.quiet_ms);
    } else if (state->moving && now >= state->quiet_deadline) {
        state->moving = false;
        state->report_pending = true;
    }
    return br_motion_due(state, now);
}

void br_motion_reported(br_motion_state *state, uint64_t now) {
    if (!state) return;
    now = monotonic_now(state, now);
    state->last_now = now;
    state->report_pending = false;
    state->idle_retries = 0;
    state->next_report_at = add_ms(
        now, state->moving ? state->config.moving_ms : state->config.idle_ms);
}

void br_motion_attempted(br_motion_state *state, uint64_t now, bool sent) {
    if (!state) return;
    if (sent || state->moving || state->idle_retries >= 2) {
        br_motion_reported(state, now);
        return;
    }
    now = monotonic_now(state, now);
    state->last_now = now;
    state->report_pending = false;
    ++state->idle_retries;
    state->next_report_at = add_ms(now, 5000);
}

bool br_motion_reconfigure(br_motion_state *state,
                           const br_motion_config *config, uint64_t now) {
    if (!state || !br_motion_config_valid(config)) return false;
    now = monotonic_now(state, now);
    state->last_now = now;
    state->config = *config;
    state->idle_retries = 0;
    if (state->moving) state->quiet_deadline = add_ms(now, config->quiet_ms);
    state->report_pending = true;
    return true;
}
