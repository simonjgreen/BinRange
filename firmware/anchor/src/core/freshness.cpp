#include "core/freshness.h"

namespace {
constexpr uint32_t kMovingStaleMs = 20000;
constexpr uint32_t kValidEpoch = 1700000000;
}

TagFreshness tag_freshness(bool heard, bool motion_known, bool moving,
                           uint32_t last_seen_ms, uint32_t adopted_ms,
                           uint32_t now_ms, uint32_t stale_after_s,
                           bool absent_latched) {
    if (absent_latched) return {true, true, true};
    const uint32_t absence_ms = stale_after_s * 1000UL;
    if (!heard) {
        const bool overdue = (uint32_t)(now_ms - adopted_ms) >= absence_ms;
        return {true, overdue, overdue};
    }
    const uint32_t elapsed_ms = now_ms - last_seen_ms;
    const uint32_t range_limit = motion_known && moving
        ? kMovingStaleMs : absence_ms;
    return {elapsed_ms >= range_limit, true, elapsed_ms >= absence_ms};
}

uint32_t reception_epoch(uint32_t wall_now_s, uint32_t now_ms,
                         uint32_t received_ms) {
    if (wall_now_s < kValidEpoch) return 0;
    return wall_now_s - ((uint32_t)(now_ms - received_ms) / 1000UL);
}
