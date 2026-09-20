#include <unity.h>

#include "core/ranging_frame.h"

void test_poll_accepts_only_its_complete_wire_length() {
    TEST_ASSERT_FALSE(ranging_poll_length_valid(0));
    TEST_ASSERT_FALSE(ranging_poll_length_valid(11));
    TEST_ASSERT_TRUE(ranging_poll_length_valid(12));
    TEST_ASSERT_FALSE(ranging_poll_length_valid(13));
}

void test_final_accepts_each_deployed_complete_wire_length() {
    TEST_ASSERT_TRUE(ranging_final_length_valid(24));
    TEST_ASSERT_TRUE(ranging_final_length_valid(26));
    TEST_ASSERT_TRUE(ranging_final_length_valid(29));
    TEST_ASSERT_TRUE(ranging_final_length_valid(33));
}

void test_final_rejects_truncated_and_oversize_wire_lengths() {
    const unsigned invalid_lengths[] = {0u, 9u, 10u, 23u, 25u, 27u,
                                        28u, 30u, 32u, 34u, 127u};
    for (unsigned length : invalid_lengths)
        TEST_ASSERT_FALSE(ranging_final_length_valid(length));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_poll_accepts_only_its_complete_wire_length);
    RUN_TEST(test_final_accepts_each_deployed_complete_wire_length);
    RUN_TEST(test_final_rejects_truncated_and_oversize_wire_lengths);
    return UNITY_END();
}
