#include "core/registry.h"
// Kept in the core so host tests do not need the Arduino config header.
#define DEFAULT_STALE_AFTER_S_CORE 21600
#include <cstring>

static void copy_bounded(char *dst, size_t n, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

TagRecord *Registry::find(uint16_t addr) {
    for (size_t i = 0; i < n_; i++)
        if (tags_[i].addr == addr) return &tags_[i];
    return nullptr;
}

TagRecord *Registry::touch(uint16_t addr, uint32_t now_ms) {
    TagRecord *t = find(addr);
    if (t) {
        t->last_seen_ms = now_ms;
        t->heard = true;
        return t;
    }
    if (n_ >= REGISTRY_MAX_TAGS) return nullptr;
    t = &tags_[n_++];
    t->addr = addr;
    t->adopted = false;
    t->name[0] = '\0';
    t->area[0] = '\0';
    t->offset = 0.0f;
    t->stale_after_s = DEFAULT_STALE_AFTER_S_CORE;
    t->batt_mv = 0;
    t->batt_known = false;
    t->stale = false;
    t->moving = false;
    t->motion_known = false;
    t->sensor_fault = false;
    t->sensor_fault_known = false;
    t->misses = 0;
    t->misses_known = false;
    t->wake_count = 0;
    t->wake_count_known = false;
    t->last_seen_ms = now_ms;
    t->adopted_ms = now_ms;
    t->last_seen_epoch = 0;
    t->discovery_sent = false;
    t->pending = false;
    t->heard = true;
    t->absence_known = true;
    t->absent = false;
    t->stats.reset();
    return t;
}

bool Registry::adopt(uint16_t addr, const char *name, const char *area,
                     float offset, uint32_t stale_after_s, uint32_t now_ms) {
    // Retained config can arrive before the tag has ever been heard.
    TagRecord *t = find(addr);
    if (!t) {
        t = touch(addr, 0);
        if (!t) return false;
        t->heard = false;
        t->absence_known = false;
    }
    copy_bounded(t->name, REGISTRY_NAME_LEN, name);
    copy_bounded(t->area, REGISTRY_NAME_LEN, area);
    t->offset = offset;
    t->stale_after_s = stale_after_s ? stale_after_s : DEFAULT_STALE_AFTER_S_CORE;
    if (!t->adopted) {
        t->adopted_ms = now_ms;
        t->absence_known = t->heard;
        t->absent = false;
    }
    t->adopted = true;
    return true;
}

bool Registry::forget(uint16_t addr) {
    for (size_t i = 0; i < n_; i++) {
        if (tags_[i].addr != addr) continue;
        // Compact: order is not significant.
        tags_[i] = tags_[n_ - 1];
        n_--;
        return true;
    }
    return false;
}

size_t Registry::adopted_count() const {
    size_t c = 0;
    for (size_t i = 0; i < n_; i++)
        if (tags_[i].adopted) c++;
    return c;
}
