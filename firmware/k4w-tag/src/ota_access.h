#ifndef BR_OTA_ACCESS_H
#define BR_OTA_ACCESS_H
#include "update_policy.h"

/* Header-only runtime decisions; all times are monotonic uint64 milliseconds. */
struct br_ota_access {
    uint64_t start_ms, activity_ms, cooldown_until_ms, pairing_until_ms;
    unsigned attempts;
    bool open;
};
static inline bool br_ota_access_allowed(const struct br_ota_access *a,
                                        uint64_t now, bool authenticated) {
    return authenticated && a->open && now-a->start_ms < 600000 &&
           now-a->activity_ms < 60000;
}
static inline bool br_ota_authenticated(const struct br_ota_access *a, uint64_t now,
                                       bool admitted, bool secure) {
    return admitted && secure && br_ota_access_allowed(a, now, true);
}
static inline void br_ota_expire(struct br_ota_access *a, uint64_t now) {
    if (a->open && !br_ota_access_allowed(a, now, true)) {
        uint64_t end = a->start_ms + 600000;
        if (a->activity_ms + 60000 < end) end = a->activity_ms + 60000;
        a->open = false;
        a->pairing_until_ms = 0;
        a->cooldown_until_ms = end + 60000;
    }
}
static inline bool br_ota_pairing_allowed(const struct br_ota_access *a, uint64_t now) {
    return br_ota_access_allowed(a, now, true) && now < a->pairing_until_ms;
}
static inline bool br_ota_local(struct br_ota_access *a, uint64_t now) {
    br_ota_expire(a, now);
    if (now < a->cooldown_until_ms) return false;
    if (!a->open) {
        a->open = true; a->attempts = 0;
        a->start_ms = a->activity_ms = now;
    }
    a->pairing_until_ms = now + 60000;
    return true;
}
static inline bool br_ota_attempt(struct br_ota_access *a, uint64_t now, bool known) {
    br_ota_expire(a, now);
    if (now < a->cooldown_until_ms || (!known && !br_ota_pairing_allowed(a, now)))
        return false;
    if (!a->open) {
        a->open = true; a->attempts = 0;
        a->start_ms = a->activity_ms = now;
    }
    if (a->attempts >= 3) return false;
    a->attempts++;
    return true;
}
static inline void br_ota_end_attempt(struct br_ota_access *a, uint64_t now) {
    br_ota_expire(a, now);
    if (a->open && a->attempts >= 3) {
        a->open = false; a->pairing_until_ms = 0;
        a->cooldown_until_ms = now + 60000;
    }
}
static inline bool br_ota_activity(struct br_ota_access *a, uint64_t now, bool auth) {
    br_ota_expire(a, now);
    if (!br_ota_access_allowed(a, now, auth)) return false;
    a->activity_ms = now;
    return true;
}
static inline void br_ota_consume_cookie(volatile uint32_t cookie[5], uint32_t copy[5]) {
    for (unsigned i=0; i<5; i++) { copy[i] = cookie[i]; cookie[i] = 0; }
}
static inline int br_ota_provision(const uint32_t cookie[5], bool loaded, bool corrupt,
                                  uint32_t id0, uint32_t id1) {
    if (corrupt) return -1;
    if (loaded) return 0;
    if (cookie[0] != 0x42525056 || cookie[1] != 0x53445731 ||
        cookie[2] == 0 || cookie[2] > 0xfffe || cookie[3] != id0 || cookie[4] != id1)
        return -1;
    return (int)cookie[2];
}
static inline void br_ota_ad_result(bool *healthy, int result) {
    *healthy = result == 0;
}
static inline bool br_ota_health(struct br_update_policy *p, uint64_t now,
                                bool radio, bool ble) {
    br_update_policy_health(p, now, radio, ble);
    br_update_policy_progress(p, now);
    return br_update_policy_confirmation_ready(p, now);
}
#endif
