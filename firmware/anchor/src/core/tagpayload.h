#pragma once
#include <cstddef>
#include <cstdint>
#include "core/tagstats.h"

// The per-tag state payload. Extracted from the publisher and made pure so it
// can be unit tested: two malformed-JSON bugs reached hardware when this was
// built inline with snprintf, and both were invisible until a consumer tried
// to parse it. Home Assistant discards bad JSON silently.
struct TagState {
    const TagSummary *summary;
    float offset;
    const char *iso_time;    // empty string if the clock is not set
    uint32_t age_s;
    bool stale;
    bool motion_known;
    bool moving;
    bool sensor_fault_known;
    bool sensor_fault;
    bool misses_known;
    uint16_t misses;
    bool wake_count_known;
    uint32_t wake_count;
    bool batt_known;
    uint16_t batt_mv;        // 0 explicitly clears the retained measurement
    bool absence_known;
    bool absent;
    bool received;
};

// Returns the length written, or 0 if it did not fit.
size_t tag_state_json(char *out, size_t n, const TagState &s);
