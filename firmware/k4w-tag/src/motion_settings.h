#ifndef BR_MOTION_SETTINGS_H
#define BR_MOTION_SETTINGS_H
#include "motion_policy.h"

/* Owner supplies synchronization. A take is not an application acknowledgment.
 * A new save while the main owner applies an older record remains pending. */
struct br_motion_settings {
    struct br_motion_config wanted;
    uint64_t revision, applying;
    bool pending;
};
void br_motion_settings_init(struct br_motion_settings *state);
void br_motion_settings_saved(struct br_motion_settings *state, const struct br_motion_config *config);
bool br_motion_settings_take(struct br_motion_settings *state, struct br_motion_config *out);
void br_motion_settings_applied(struct br_motion_settings *state);
#endif
