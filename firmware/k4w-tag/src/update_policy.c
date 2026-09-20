#include "update_policy.h"

void br_update_policy_init(struct br_update_policy *p, uint64_t now_ms,
                           bool trial)
{
    *p = (struct br_update_policy){
        .boot_ms = now_ms,
        .last_progress_ms = now_ms,
        .trial = trial,
    };
}

void br_update_policy_progress(struct br_update_policy *p, uint64_t now_ms)
{
    if (now_ms >= p->last_progress_ms) {
        if (p->radio_healthy && p->ble_healthy &&
            (!p->observing_health || now_ms - p->last_progress_ms > 6000)) {
            p->observation_start_ms = now_ms;
            p->observing_health = true;
        }
        p->last_progress_ms = now_ms;
    }
}

void br_update_policy_health(struct br_update_policy *p, uint64_t now_ms,
                             bool radio_healthy, bool ble_healthy)
{
    p->radio_healthy = radio_healthy;
    p->ble_healthy = ble_healthy;
    if (!radio_healthy || !ble_healthy || now_ms < p->last_progress_ms ||
        now_ms - p->last_progress_ms > 6000) {
        p->observing_health = false;
    } else if (!p->observing_health) {
        p->observation_start_ms = now_ms;
        p->observing_health = true;
    }
}

bool br_update_policy_confirmation_ready(const struct br_update_policy *p,
                                         uint64_t now_ms)
{
    return p->trial && p->radio_healthy && p->ble_healthy &&
           p->observing_health && now_ms >= p->observation_start_ms &&
           now_ms - p->observation_start_ms >= 30000 &&
           now_ms >= p->last_progress_ms &&
           now_ms - p->last_progress_ms <= 6000;
}

void br_update_policy_open_maintenance(struct br_update_policy *p,
                                       uint64_t now_ms)
{
    p->maintenance_open = true;
    p->maintenance_start_ms = now_ms;
    p->last_activity_ms = now_ms;
}

bool br_update_policy_access_allowed(const struct br_update_policy *p,
                                     uint64_t now_ms, bool authorized)
{
    return authorized && p->maintenance_open &&
           now_ms >= p->maintenance_start_ms &&
           now_ms - p->maintenance_start_ms < 600000 &&
           now_ms >= p->last_activity_ms &&
           now_ms - p->last_activity_ms < 60000;
}

bool br_update_policy_activity(struct br_update_policy *p, uint64_t now_ms,
                               bool authorized)
{
    if (!br_update_policy_access_allowed(p, now_ms, authorized)) {
        return false;
    }
    p->last_activity_ms = now_ms;
    return true;
}
