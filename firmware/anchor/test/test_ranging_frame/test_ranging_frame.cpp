#include <unity.h>
#include <initializer_list>

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
    TEST_ASSERT_TRUE(ranging_final_length_valid(41));
}

void test_final_rejects_truncated_and_oversize_wire_lengths() {
    const unsigned invalid_lengths[] = {0u, 9u, 10u, 23u, 25u, 27u,
                                        28u, 30u, 32u, 34u, 39u, 40u, 42u, 127u};
    for (unsigned length : invalid_lengths)
        TEST_ASSERT_FALSE(ranging_final_length_valid(length));
}


void test_tip_extension_is_little_endian_and_preserves_readiness() {
    uint8_t frame[41]{};
    frame[24] = 0x05; // moving + tip ready
    frame[31] = 0x78; frame[32] = 0x56; frame[33] = 0x34; frame[34] = 0x12;
    frame[35] = 0x04; frame[36] = 0x03; frame[37] = 0x02; frame[38] = 0x01;
    frame[39] = frame[40] = 0xff; // FCS must not enter the age
    const auto tip = ranging_tip_telemetry(frame, sizeof(frame));
    TEST_ASSERT_TRUE(tip.has_tip);
    TEST_ASSERT_TRUE(tip.tip_ready);
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, tip.tip_count);
    TEST_ASSERT_EQUAL_UINT32(0x01020304u, tip.tip_age_s);
}

void test_sensor_fault_suppresses_ready_even_if_ready_bit_set() {
    uint8_t frame[41]{};
    frame[24] = 0x06;
    auto tip = ranging_tip_telemetry(frame, sizeof(frame));
    TEST_ASSERT_TRUE(tip.has_tip);
    TEST_ASSERT_FALSE(tip.tip_ready);
    frame[24] = 0;
    tip = ranging_tip_telemetry(frame, sizeof(frame));
    TEST_ASSERT_TRUE(tip.has_tip);
    TEST_ASSERT_FALSE(tip.tip_ready);
}

void test_legacy_and_invalid_frames_cannot_claim_tip_telemetry() {
    uint8_t frame[42];
    for (auto &v : frame) v = 0xff;
    for (unsigned length : {24u, 26u, 29u, 33u, 40u, 42u}) {
        const auto tip = ranging_tip_telemetry(frame, length);
        TEST_ASSERT_FALSE(tip.has_tip);
        TEST_ASSERT_FALSE(tip.tip_ready);
        TEST_ASSERT_EQUAL_UINT32(0, tip.tip_count);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tip.tip_age_s);
    }
    TEST_ASSERT_FALSE(ranging_tip_telemetry(nullptr, 41).has_tip);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_tip_extension_is_little_endian_and_preserves_readiness);
    RUN_TEST(test_sensor_fault_suppresses_ready_even_if_ready_bit_set);
    RUN_TEST(test_legacy_and_invalid_frames_cannot_claim_tip_telemetry);
    RUN_TEST(test_poll_accepts_only_its_complete_wire_length);
    RUN_TEST(test_final_accepts_each_deployed_complete_wire_length);
    RUN_TEST(test_final_rejects_truncated_and_oversize_wire_lengths);
    return UNITY_END();
}
