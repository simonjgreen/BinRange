#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "motion_policy.h"

static br_motion_config config(void) {
    br_motion_config c;
    br_motion_defaults(&c);
    return c;
}

static void test_config_boundaries(void) {
    br_motion_config c = config();
    assert(c.moving_ms == 5000);
    assert(c.idle_ms == 600000);
    assert(c.quiet_ms == 30000);
    assert(c.threshold_mg == 250);
    assert(c.duration_samples == 2);
    assert(br_motion_config_valid(&c));

    c.moving_ms = 999; assert(!br_motion_config_valid(&c));
    c.moving_ms = 60001; assert(!br_motion_config_valid(&c));
    c = config(); c.idle_ms = 59999; assert(!br_motion_config_valid(&c));
    c = config(); c.idle_ms = 3600001; assert(!br_motion_config_valid(&c));
    c = config(); c.quiet_ms = 4999; assert(!br_motion_config_valid(&c));
    c = config(); c.quiet_ms = 300001; assert(!br_motion_config_valid(&c));
    c = config(); c.threshold_mg = 31; assert(!br_motion_config_valid(&c));
    c = config(); c.threshold_mg = 1001; assert(!br_motion_config_valid(&c));
    c = config(); c.duration_samples = 0; assert(!br_motion_config_valid(&c));
    c = config(); c.duration_samples = 128; assert(!br_motion_config_valid(&c));
    c = config(); c.idle_ms = c.moving_ms - 1; assert(!br_motion_config_valid(&c));
    assert(!br_motion_config_valid(NULL));
}

static void test_boot_and_cadence(void) {
    br_motion_config c = config();
    br_motion_state s;
    br_motion_init(&s, &c, 1000);
    assert(!s.moving);
    assert(s.wake_count == 0);
    assert(br_motion_due(&s, 1000));
    br_motion_reported(&s, 1000);
    assert(!br_motion_due(&s, 1001));
    assert(br_motion_due(&s, 601000));
    br_motion_reported(&s, 601000);
    assert(!br_motion_due(&s, 601001));
}

static void test_motion_transition_and_saturated_count(void) {
    br_motion_config c = config();
    br_motion_state s;
    br_motion_init(&s, &c, 0);
    br_motion_reported(&s, 0);

    assert(br_motion_update(&s, 100, true));
    assert(s.moving);
    assert(s.wake_count == 1);
    br_motion_reported(&s, 100);
    assert(!br_motion_due(&s, 5099));
    assert(br_motion_due(&s, 5100));
    br_motion_reported(&s, 5100);
    assert(!br_motion_due(&s, 5101));

    s.wake_count = UINT32_MAX;
    assert(!br_motion_update(&s, 5200, true));
    assert(s.wake_count == UINT32_MAX);
}

static void test_retrigger_refreshes_quiet_without_resetting_cadence(void) {
    br_motion_config c = config();
    br_motion_state s;
    br_motion_init(&s, &c, 0);
    br_motion_reported(&s, 0);
    br_motion_update(&s, 100, true);
    br_motion_reported(&s, 100);

    assert(!br_motion_update(&s, 1000, true));
    /* The moving cadence remains anchored at the last report. */
    assert(br_motion_due(&s, 5100));
    br_motion_reported(&s, 5100);
    assert(br_motion_update(&s, 29999, false));
    assert(s.moving);
    assert(br_motion_update(&s, 31000, false));
    assert(br_motion_due(&s, 31000));

    /* Quiet expiry creates one immediate settled idle report. */
    assert(!s.moving);
    assert(br_motion_due(&s, 31000));
    br_motion_reported(&s, 31000);
    assert(!br_motion_due(&s, 31001));
    assert(br_motion_due(&s, 631000));
}

static void test_reconfigure_preserves_episode_and_prompts(void) {
    br_motion_config c = config();
    br_motion_config changed = c;
    br_motion_state s;
    br_motion_init(&s, &c, 100);
    br_motion_reported(&s, 100);
    br_motion_update(&s, 200, true);
    br_motion_reported(&s, 200);
    changed.moving_ms = 60000;
    changed.quiet_ms = 300000;
    assert(br_motion_reconfigure(&s, &changed, 300));
    assert(s.moving);
    assert(s.wake_count == 1);
    assert(br_motion_due(&s, 300));
    br_motion_reported(&s, 300);
    assert(!br_motion_due(&s, 60299));
    assert(br_motion_due(&s, 60300));
    br_motion_reported(&s, 60300);
    changed.quiet_ms = 5000;
    assert(br_motion_reconfigure(&s, &changed, 60400));
    br_motion_reported(&s, 60400);
    assert(br_motion_update(&s, 65400, false));
    assert(!s.moving);
    /* Quiet expiry must win over the later moving cadence deadline. */
    assert(br_motion_due(&s, 65400));
    assert(!br_motion_reconfigure(&s, NULL, 400));
    assert(!s.moving);
}

static void test_timestamps_do_not_go_backwards(void) {
    br_motion_config c = config();
    br_motion_state s;
    br_motion_init(&s, &c, UINT64_MAX - 10);
    br_motion_reported(&s, UINT64_MAX - 10);
    assert(br_motion_update(&s, 0, true));
    assert(s.moving);
    assert(s.last_now == UINT64_MAX - 10);
    assert(br_motion_due(&s, 0));
}

/* Break caught: a failed idle exchange must not immediately defer 10 minutes,
 * or create an endless fast retry loop when the anchor is unavailable. */
static void test_failed_idle_report_gets_a_bounded_early_retry(void) {
    br_motion_config c = config();
    br_motion_state s;
    br_motion_init(&s, &c, 0);
    br_motion_attempted(&s, 100, false);
    assert(!br_motion_due(&s, 5099));
    assert(br_motion_due(&s, 5100));
    br_motion_attempted(&s, 5100, false);
    assert(!br_motion_due(&s, 10099));
    assert(br_motion_due(&s, 10100));
    br_motion_attempted(&s, 10100, false);
    assert(!br_motion_due(&s, 15100));
    assert(!br_motion_due(&s, 610099));
    assert(br_motion_due(&s, 610100));
    br_motion_attempted(&s, 610100, false);
    assert(br_motion_due(&s, 615100));
}

/* Break caught: success or resumed motion must not inherit an idle retry. */
static void test_idle_retries_stop_on_success_and_moving_cadence_is_unchanged(void) {
    br_motion_config c = config();
    br_motion_state s;
    br_motion_init(&s, &c, 0);
    br_motion_attempted(&s, 100, false);
    br_motion_attempted(&s, 5100, true);
    assert(!br_motion_due(&s, 10100));
    assert(br_motion_due(&s, 605100));
    br_motion_attempted(&s, 605100, false);
    assert(br_motion_update(&s, 606000, true));
    assert(s.wake_count == 1);
    br_motion_attempted(&s, 606000, false);
    assert(!br_motion_due(&s, 610999));
    assert(br_motion_due(&s, 611000));
    br_motion_attempted(&s, 611000, false);
    assert(br_motion_update(&s, 636000, false));
    assert(!s.moving);
    br_motion_attempted(&s, 636000, false);
    br_motion_attempted(&s, 641000, false);
    assert(br_motion_due(&s, 646000));
}

int main(void) {
    test_config_boundaries();
    test_boot_and_cadence();
    test_motion_transition_and_saturated_count();
    test_retrigger_refreshes_quiet_without_resetting_cadence();
    test_reconfigure_preserves_episode_and_prompts();
    test_timestamps_do_not_go_backwards();
    test_failed_idle_report_gets_a_bounded_early_retry();
    test_idle_retries_stop_on_success_and_moving_cadence_is_unchanged();
    return 0;
}
