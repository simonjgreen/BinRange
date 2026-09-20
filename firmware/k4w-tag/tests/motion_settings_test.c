#include <assert.h>
#include "motion_settings.h"
int main(void) {
    struct br_motion_settings s;
    struct br_motion_config a, b;
    br_motion_settings_init(&s);
    assert(br_motion_settings_take(&s, &a));
    assert(s.pending); /* status sampled during sensor configuration */
    b = a; b.idle_ms = 300000;
    br_motion_settings_saved(&s, &b);
    br_motion_settings_applied(&s); /* completes the old record only */
    assert(s.pending);
    assert(br_motion_settings_take(&s, &a));
    assert(a.idle_ms == 300000);
    assert(s.pending);
    br_motion_settings_applied(&s);
    assert(!s.pending);
    assert(!br_motion_settings_take(&s, &a));
    b.moving_ms = 12000;
    br_motion_settings_saved(&s, &b);
    assert(s.pending);
    assert(br_motion_settings_take(&s, &a));
    assert(a.moving_ms == 12000 && a.idle_ms == 300000);
    br_motion_settings_applied(&s);
    assert(!s.pending);
    return 0;
}
