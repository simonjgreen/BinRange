#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include "tip_policy.h"

static uint64_t now;
static bool feed(br_tip_state *s, int x, int y, int z, unsigned n, bool quiet) {
    bool changed = false;
    while (n--) { now += 40; changed |= br_tip_sample(s, x, y, z, now, quiet); }
    return changed;
}
static br_tip_state upright(void) {
    br_tip_state s;
    now = 0;
    br_tip_init(&s, NULL);
    assert(br_tip_interval(&s, false) == 40);
    assert(!feed(&s, 0, 0, 1000, 49, true));
    assert(!s.calibrated);
    assert(feed(&s, 0, 0, 1000, 1, true));
    assert(s.calibrated && s.armed && s.record.count == 0);
    assert(br_tip_age_s(&s, now) == UINT32_MAX);
    return s;
}
static void boundary_and_rearm(void) {
    br_tip_state s = upright();
    assert(!feed(&s, 1000, 0, 0, 10, false)); /* exactly 90 */
    assert(!feed(&s, 1000, 0, -1, 2, false));
    assert(feed(&s, 1000, 0, -1, 1, false)); /* strictly beyond */
    assert(s.record.count == 1 && !s.armed);
    assert(br_tip_age_s(&s, now + 5000) == 5);
    assert(!feed(&s, 0, 0, -1000, 200, false));
    assert(s.record.count == 1);
    assert(!feed(&s, 0, 0, 1000, 5, false));
    assert(s.armed);
    assert(feed(&s, 0, 0, -1000, 3, false));
    assert(s.record.count == 2);
}
static void invalid_and_gaps(void) {
    br_tip_state s = upright();
    assert(!feed(&s, 0, 0, -1000, 2, false));
    assert(!feed(&s, 0, 0, 0, 1, false));
    assert(!feed(&s, 0, 0, -1000, 2, false));
    br_tip_invalid(&s);
    assert(!feed(&s, 0, 0, -1000, 2, false));
    now += 1000; /* samples separated by sleep/outage are not consecutive */
    assert(!feed(&s, 0, 0, -1000, 1, false));
    assert(!feed(&s, 0, 0, -2000, 10, false));
    assert(!feed(&s, INT_MAX, INT_MIN, 0, 10, false));
    assert(s.record.count == 0);
    assert(!br_tip_sample(&s, 0, 0, -1000, now - 100, false));
    assert(!br_tip_sample(&s, 0, 0, -1000, now, false));
    assert(feed(&s, 0, 0, -1000, 3, false));
}
static void mounting_and_restart(void) {
    const br_tip_record record = {{600, 0, 800}, 17};
    br_tip_state s;
    br_tip_init(&s, &record);
    now = 0;
    assert(s.calibrated && !s.armed);
    assert(!feed(&s, -600, 0, -800, 20, false)); /* restart inverted */
    assert(s.record.count == 17 && br_tip_age_s(&s, now) == UINT32_MAX);
    assert(!feed(&s, 600, 0, 800, 5, true));
    assert(s.armed);
    assert(!feed(&s, 800, 0, -600, 3, false)); /* orthogonal */
    assert(feed(&s, -600, 0, -800, 3, false));
    assert(s.record.count == 18);
    br_tip_record full = {{0, 0, 1000}, UINT32_MAX};
    br_tip_init(&s, &full);
    feed(&s, 0, 0, 1000, 5, true);
    assert(!feed(&s, 0, 0, -1000, 3, false)); /* never wrap */
    assert(s.record.count == UINT32_MAX);
}
static void calibration_and_slow_tilt(void) {
    br_tip_state s;
    now = 0;
    br_tip_init(&s, NULL);
    assert(!feed(&s, 0, 0, 1000, 60, false)); /* moving cannot calibrate */
    assert(!s.calibrated);
    for (unsigned i=0; i<60; i++) feed(&s, i%2 ? 300 : -300, 0, 950, 1, true);
    assert(!s.calibrated);
    feed(&s, 0, 0, 1000, 50, true);
    assert(s.calibrated);
    assert(br_tip_interval(&s, false) == 1000);
    now += 1000;
    assert(!feed(&s, 800, 0, 600, 1, true)); /* no motion IRQ, 53 degree tilt */
    assert(br_tip_interval(&s, false) == 40);
    assert(feed(&s, 0, 0, -1000, 3, true));
    br_tip_record invalid = {{0,0,0}, 1};
    assert(!br_tip_record_valid(&invalid));
}
int main(void) {
    boundary_and_rearm(); invalid_and_gaps(); mounting_and_restart();
    calibration_and_slow_tilt();
    return 0;
}
