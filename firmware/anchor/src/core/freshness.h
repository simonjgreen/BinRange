#pragma once
#include <cstdint>

struct TagFreshness {
    bool range_stale;
    bool absence_known;
    bool absent;
};

// All elapsed-time arithmetic is unsigned so an ordinary millis() wrap remains
// coherent. A tag that has not been heard has no measurement freshness, but
// its absence becomes knowable once its adopted interval elapses.
TagFreshness tag_freshness(bool heard, bool motion_known, bool moving,
                           uint32_t last_seen_ms, uint32_t adopted_ms,
                           uint32_t now_ms, uint32_t stale_after_s,
                           bool absent_latched = false);

// Infers a reception wall-clock time from a valid drain-time clock and the
// monotonic queue age. Zero means no valid clock was available at drain.
uint32_t reception_epoch(uint32_t wall_now_s, uint32_t now_ms,
                         uint32_t received_ms);
