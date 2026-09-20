#include <unity.h>
#include <cstring>
#include "core/topics.h"

static char buf[128];

void test_anchor_status_topic() {
    topic_anchor_status(buf, sizeof(buf), "a");
    TEST_ASSERT_EQUAL_STRING("binrange/anchor/a/status", buf);
}

void test_tag_state_topic_nests_anchor() {
    topic_tag_state(buf, sizeof(buf), "4556", "a");
    TEST_ASSERT_EQUAL_STRING("binrange/tag/4556/anchor/a/state", buf);
}

void test_tag_config_topic() {
    topic_tag_config(buf, sizeof(buf), "4556");
    TEST_ASSERT_EQUAL_STRING("binrange/tag/4556/config", buf);
}

void test_config_wildcard_subscribes_all_tags() {
    topic_tag_config_wildcard(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("binrange/tag/+/config", buf);
}

void test_cmd_wildcard() {
    topic_anchor_cmd_wildcard(buf, sizeof(buf), "a");
    TEST_ASSERT_EQUAL_STRING("binrange/anchor/a/cmd/+", buf);
}

void test_discovery_topic() {
    topic_discovery(buf, sizeof(buf), "sensor", "binrange_4556_a_distance");
    TEST_ASSERT_EQUAL_STRING(
        "homeassistant/sensor/binrange_4556_a_distance/config", buf);
}

void test_tag_id_is_four_lowercase_hex_digits() {
    tag_id_to_hex(buf, sizeof(buf), 0x4556);
    TEST_ASSERT_EQUAL_STRING("4556", buf);
    tag_id_to_hex(buf, sizeof(buf), 0x000f);
    TEST_ASSERT_EQUAL_STRING("000f", buf);
    tag_id_to_hex(buf, sizeof(buf), 0xABCD);
    TEST_ASSERT_EQUAL_STRING("abcd", buf);
}

void test_parse_tag_config_topic() {
    uint16_t addr = 0;
    TEST_ASSERT_TRUE(parse_tag_config_topic("binrange/tag/4556/config", &addr));
    TEST_ASSERT_EQUAL_UINT16(0x4556, addr);
    TEST_ASSERT_TRUE(parse_tag_config_topic("binrange/tag/000f/config", &addr));
    TEST_ASSERT_EQUAL_UINT16(0x000f, addr);
}

void test_parse_rejects_other_topics() {
    uint16_t addr = 0;
    TEST_ASSERT_FALSE(parse_tag_config_topic("binrange/anchor/a/cmd/phy", &addr));
    TEST_ASSERT_FALSE(parse_tag_config_topic("binrange/tag/4556/anchor/a/state", &addr));
    TEST_ASSERT_FALSE(parse_tag_config_topic("nonsense", &addr));
    TEST_ASSERT_FALSE(parse_tag_config_topic("binrange/tag/zzzz/config", &addr));
}

void test_truncation_is_safe() {
    char small[10];
    topic_anchor_status(small, sizeof(small), "a");
    TEST_ASSERT_TRUE(strlen(small) < sizeof(small));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_anchor_status_topic);
    RUN_TEST(test_tag_state_topic_nests_anchor);
    RUN_TEST(test_tag_config_topic);
    RUN_TEST(test_config_wildcard_subscribes_all_tags);
    RUN_TEST(test_cmd_wildcard);
    RUN_TEST(test_discovery_topic);
    RUN_TEST(test_tag_id_is_four_lowercase_hex_digits);
    RUN_TEST(test_parse_tag_config_topic);
    RUN_TEST(test_parse_rejects_other_topics);
    RUN_TEST(test_truncation_is_safe);
    return UNITY_END();
}
