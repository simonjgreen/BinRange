#include <unity.h>
#include "core/freshness.h"

void setUp(void) {}
void tearDown(void) {}

void test_moving_silence_is_range_stale_before_long_absence() {
    TagFreshness f = tag_freshness(true, true, true, 1000, 1000, 22000, 21600);
    TEST_ASSERT_TRUE(f.range_stale);
    TEST_ASSERT_TRUE(f.absence_known);
    TEST_ASSERT_FALSE(f.absent);
}

void test_long_absence_alarms_and_a_new_report_recovers() {
    TagFreshness overdue = tag_freshness(true, false, false, 1000, 1000,
                                         21601001, 21600);
    TEST_ASSERT_TRUE(overdue.range_stale);
    TEST_ASSERT_TRUE(overdue.absence_known);
    TEST_ASSERT_TRUE(overdue.absent);

    TagFreshness recovered = tag_freshness(true, false, false, 21602000, 1000,
                                           21602000, 21600);
    TEST_ASSERT_FALSE(recovered.range_stale);
    TEST_ASSERT_TRUE(recovered.absence_known);
    TEST_ASSERT_FALSE(recovered.absent);
}

void test_retained_adoption_before_reception_has_unknown_absence_then_alarms() {
    TagFreshness waiting = tag_freshness(false, false, false, 0, 5000, 6000, 21600);
    TEST_ASSERT_TRUE(waiting.range_stale);
    TEST_ASSERT_FALSE(waiting.absence_known);

    TagFreshness overdue = tag_freshness(false, false, false, 0, 5000,
                                         21605001, 21600);
    TEST_ASSERT_TRUE(overdue.range_stale);
    TEST_ASSERT_TRUE(overdue.absence_known);
    TEST_ASSERT_TRUE(overdue.absent);
}

void test_queue_delay_and_millis_wrap_keep_the_reception_timestamp() {
    TEST_ASSERT_EQUAL_UINT32(1700000001,
                             reception_epoch(1700000005, 5000, 1000));
    TEST_ASSERT_EQUAL_UINT32(1700000001,
                             reception_epoch(1700000005, 4000, 0xFFFFFFFC));
    TEST_ASSERT_EQUAL_UINT32(0, reception_epoch(1600000000, 5000, 1000));
}

void test_absence_stays_latched_across_millis_wrap_without_a_report() {
    // This is evaluated after an alarm has already been published. The
    // low elapsed value is indistinguishable from a fresh tag without the
    // retained latch, but no reception occurred to clear it.
    TagFreshness f = tag_freshness(true, false, false, 1000, 1000, 2000,
                                   21600, true);
    TEST_ASSERT_TRUE(f.range_stale);
    TEST_ASSERT_TRUE(f.absence_known);
    TEST_ASSERT_TRUE(f.absent);
}

void test_delayed_reception_uses_its_event_time_for_moving_freshness() {
    // The event was received at 1 s but only drained at 22 s. It is not a
    // fresh 22 s measurement merely because the publisher saw it just now.
    TagFreshness f = tag_freshness(true, true, true, 1000, 0, 22000, 21600);
    TEST_ASSERT_TRUE(f.range_stale);
    TEST_ASSERT_FALSE(f.absent);
}

void test_moving_freshness_transitions_at_twenty_seconds() {
    TagFreshness before = tag_freshness(true, true, true, 1000, 0, 20999, 21600);
    TagFreshness boundary = tag_freshness(true, true, true, 1000, 0, 21000, 21600);
    TEST_ASSERT_FALSE(before.range_stale);
    TEST_ASSERT_TRUE(boundary.range_stale);
}

void test_long_absence_transitions_at_configured_boundary() {
    TagFreshness before = tag_freshness(true, false, false, 1000, 0,
                                        21600999, 21600);
    TagFreshness boundary = tag_freshness(true, false, false, 1000, 0,
                                          21601000, 21600);
    TEST_ASSERT_FALSE(before.absent);
    TEST_ASSERT_TRUE(boundary.absent);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_moving_silence_is_range_stale_before_long_absence);
    RUN_TEST(test_long_absence_alarms_and_a_new_report_recovers);
    RUN_TEST(test_retained_adoption_before_reception_has_unknown_absence_then_alarms);
    RUN_TEST(test_queue_delay_and_millis_wrap_keep_the_reception_timestamp);
    RUN_TEST(test_absence_stays_latched_across_millis_wrap_without_a_report);
    RUN_TEST(test_delayed_reception_uses_its_event_time_for_moving_freshness);
    RUN_TEST(test_moving_freshness_transitions_at_twenty_seconds);
    RUN_TEST(test_long_absence_transitions_at_configured_boundary);
    return UNITY_END();
}
