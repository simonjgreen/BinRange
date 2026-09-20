#pragma once
#include <cstdint>
#include <cstddef>
#include "core/tagstats.h"

// Bounded and statically allocated: the radio path must not allocate.
#define REGISTRY_MAX_TAGS 16
#define REGISTRY_NAME_LEN 32

struct TagRecord {
    uint16_t addr;
    char name[REGISTRY_NAME_LEN];
    char area[REGISTRY_NAME_LEN];
    float offset;            // per-tag residual correction, metres
    uint32_t stale_after_s;  // liveness threshold; per tag, since idle ticks differ
    uint16_t batt_mv;        // 0 means unknown; clears a prior measurement
    uint16_t misses;         // cumulative failed local exchanges since boot
    uint32_t wake_count;     // since-boot diagnostic, not total_increasing
    uint32_t last_seen_ms;
    uint32_t adopted_ms;     // monotonic time used for never-heard absence
    uint32_t last_seen_epoch; // real reception wall time, or zero if unknown
    // Main-loop-owned runtime flags, not a persisted/on-wire structure. Keep
    // them together: padding across 16 records exhausted the anchor's DRAM.
    bool adopted : 1;
    bool batt_known : 1;
    bool stale : 1;
    bool moving : 1;
    bool motion_known : 1;
    bool sensor_fault : 1;
    bool sensor_fault_known : 1;
    bool misses_known : 1;
    bool wake_count_known : 1;
    bool discovery_sent : 1;
    bool pending : 1;        // unpublished samples from an in-flight burst
    bool heard : 1;
    bool absence_known : 1;
    bool absent : 1;
    TagStats stats;
};

class Registry {
  public:
    Registry() : n_(0) {}

    TagRecord *find(uint16_t addr);
    // Finds or creates. Returns nullptr when full — an unknown tag never
    // evicts a known one.
    TagRecord *touch(uint16_t addr, uint32_t now_ms);
    bool adopt(uint16_t addr, const char *name, const char *area, float offset,
               uint32_t stale_after_s, uint32_t now_ms = 0);
    bool forget(uint16_t addr);

    size_t size() const { return n_; }
    TagRecord *at(size_t i) { return i < n_ ? &tags_[i] : nullptr; }
    size_t adopted_count() const;

  private:
    TagRecord tags_[REGISTRY_MAX_TAGS];
    size_t n_;
};
