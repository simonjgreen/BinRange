#ifndef BR_UPDATE_POLICY_H
#define BR_UPDATE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* Pure decisions only: caller owns clocks, authorization, hardware and flash.
 * Pass non-null state pointers and monotonic uint64_t milliseconds from one
 * clock epoch (not a zero-extended wrapping uint32_t tick). No RF reception
 * input is required. State is caller-allocated; mutate through these functions.
 */
struct br_update_policy {
    uint64_t boot_ms;
    uint64_t last_progress_ms;
    uint64_t observation_start_ms;
    uint64_t maintenance_start_ms;
    uint64_t last_activity_ms;
    bool radio_healthy;
    bool ble_healthy;
    bool trial;
    bool observing_health;
    bool maintenance_open;
};

/* Reset all state; health starts false and maintenance starts closed. */
void br_update_policy_init(struct br_update_policy *p, uint64_t now_ms,
                           bool trial);
/* Record completed main-loop progress. A gap >6000 ms resets observation;
 * resumed progress starts a new interval if both subsystems are healthy.
 */
void br_update_policy_progress(struct br_update_policy *p, uint64_t now_ms);
/* Report current local health at now_ms. Either false flag clears observation.
 * Both true starts observation if progress is fresh; repeated healthy reports
 * preserve the interval. Initialization counts as the initial progress sample.
 * Supply progress/health notifications in monotonic timestamp order, including
 * every health loss; observation cannot infer unreported subsystem failures.
 */
void br_update_policy_health(struct br_update_policy *p, uint64_t now_ms,
                             bool radio_healthy, bool ble_healthy);
/* Trial only: >=30000 continuous ms with both subsystems healthy and progress
 * gaps <=6000 ms, including current progress age. A recovery requires a fresh
 * full interval. Health and progress at the same timestamp may arrive in either
 * order. A stale-progress query rejects; the next notification resets the
 * interval before any subsequent confirmation can succeed.
 * This predicate does not confirm an image or change trial state.
 */
bool br_update_policy_confirmation_ready(const struct br_update_policy *p,
                                         uint64_t now_ms);

/* Trusted local entry only (e.g. verified button event), never a remote command.
 * Starts/restarts the window and idle timer; caller enforces physical presence.
 */
void br_update_policy_open_maintenance(struct br_update_policy *p,
                                       uint64_t now_ms);
/* Requires authorization, an open window, idle age <60000 ms and total age
 * <600000 ms. Read-only: checking access does not refresh idle time.
 */
bool br_update_policy_access_allowed(const struct br_update_policy *p,
                                     uint64_t now_ms, bool authorized);
/* Returns true and refreshes idle time only if access is currently allowed.
 * Unauthorized/expired activity cannot reopen or extend a window. The original
 * absolute deadline is never changed by activity.
 */
bool br_update_policy_activity(struct br_update_policy *p, uint64_t now_ms,
                               bool authorized);

#endif
