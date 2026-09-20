#include "motion_settings.h"

void br_motion_settings_init(struct br_motion_settings *s) {
    br_motion_defaults(&s->wanted);
    s->revision = 1; s->applying = 0; s->pending = true;
}
void br_motion_settings_saved(struct br_motion_settings *s, const struct br_motion_config *c) {
    s->wanted = *c; ++s->revision; s->pending = true;
}
bool br_motion_settings_take(struct br_motion_settings *s, struct br_motion_config *out) {
    *out = s->wanted; s->applying = s->revision;
    return s->pending;
}
void br_motion_settings_applied(struct br_motion_settings *s) {
    if (s->applying == s->revision) s->pending = false;
}
