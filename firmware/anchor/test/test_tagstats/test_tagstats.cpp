#include <unity.h>
#include <cmath>
#include "core/tagstats.h"

void test_empty_summary_is_not_a_number() {
    TagStats s;
    TagSummary t;
    s.summarise(&t);
    TEST_ASSERT_EQUAL_UINT32(0, t.n);
    TEST_ASSERT_TRUE(std::isnan(t.mean));
    TEST_ASSERT_TRUE(std::isnan(t.sd));
}

void test_mean_and_spread() {
    TagStats s;
    s.add({10.0f, -80.0f, -75.0f, -3.0f});
    s.add({10.2f, -80.0f, -75.0f, -3.0f});
    s.add({9.8f,  -80.0f, -75.0f, -3.0f});
    TagSummary t;
    s.summarise(&t);
    TEST_ASSERT_EQUAL_UINT32(3, t.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, t.mean);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, t.sd);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.8f, t.dmin);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.2f, t.dmax);
}

void test_single_sample_has_zero_spread() {
    TagStats s;
    s.add({5.0f, -80.0f, -75.0f, -3.0f});
    TagSummary t;
    s.summarise(&t);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, t.sd);
}

void test_nlos_gap_is_rssi_minus_first_path() {
    TagStats s;
    s.add({10.0f, -85.0f, -74.0f, -3.0f});
    TagSummary t;
    s.summarise(&t);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -11.0f, t.gap);
}

void test_window_is_bounded_and_keeps_recent_samples() {
    TagStats s;
    for (int i = 0; i < TAGSTATS_WINDOW + 10; i++)
        s.add({1.0f, -80.0f, -75.0f, -3.0f});
    TagSummary t;
    s.summarise(&t);
    TEST_ASSERT_EQUAL_UINT32(TAGSTATS_WINDOW, t.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, t.mean);
}

void test_reset_clears() {
    TagStats s;
    s.add({10.0f, -80.0f, -75.0f, -3.0f});
    s.reset();
    TagSummary t;
    s.summarise(&t);
    TEST_ASSERT_EQUAL_UINT32(0, t.n);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_summary_is_not_a_number);
    RUN_TEST(test_mean_and_spread);
    RUN_TEST(test_single_sample_has_zero_spread);
    RUN_TEST(test_nlos_gap_is_rssi_minus_first_path);
    RUN_TEST(test_window_is_bounded_and_keeps_recent_samples);
    RUN_TEST(test_reset_clears);
    return UNITY_END();
}
