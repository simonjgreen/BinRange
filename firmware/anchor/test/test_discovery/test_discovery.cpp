#include <unity.h>
#include <cstring>
#include "core/discovery.h"

static char buf[1024];
static bool has(const char *needle) { return strstr(buf, needle) != nullptr; }

void setUp(void) {}
void tearDown(void) {}

void test_object_id_is_unique_per_tag_anchor_and_key() {
    char id[96];
    discovery_object_id(id, sizeof(id), "4556", "a", "distance");
    TEST_ASSERT_EQUAL_STRING("binrange_4556_a_distance", id);
}

void test_distance_payload_has_required_fields() {
    EntitySpec e{"sensor", "distance", "Distance", "m", "distance",
                 "measurement", "{{ value_json.d }}", false};
    size_t len = discovery_tag_entity(buf, sizeof(buf), e, "4556", "a",
                                      "Kitchen Bin", "Driveway", "0.1.0");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(len < sizeof(buf));
    TEST_ASSERT_TRUE(has("\"uniq_id\":\"binrange_4556_a_distance\""));
    TEST_ASSERT_TRUE(has("\"stat_t\":\"binrange/tag/4556/anchor/a/state\""));
    TEST_ASSERT_TRUE(has("\"val_tpl\":\"{{ value_json.d }}\""));
    TEST_ASSERT_TRUE(has("\"unit_of_meas\":\"m\""));
    TEST_ASSERT_TRUE(has("\"dev_cla\":\"distance\""));
    TEST_ASSERT_TRUE(has("\"stat_cla\":\"measurement\""));
}

void test_availability_points_at_the_publishing_anchor() {
    EntitySpec e{"sensor", "distance", "Distance", "m", "distance",
                 "measurement", "{{ value_json.d }}", false};
    discovery_tag_entity(buf, sizeof(buf), e, "4556", "a", "Kitchen Bin", "", "0.1.0");
    TEST_ASSERT_TRUE(has("\"avty_t\":\"binrange/anchor/a/status\""));
}

void test_device_identifiers_are_shared_across_anchors() {
    EntitySpec e{"sensor", "distance", "Distance", "m", "distance",
                 "measurement", "{{ value_json.d }}", false};
    discovery_tag_entity(buf, sizeof(buf), e, "4556", "a", "Kitchen Bin", "", "0.1.0");
    TEST_ASSERT_TRUE(has("\"ids\":[\"binrange_tag_4556\"]"));
    TEST_ASSERT_TRUE(has("\"name\":\"Kitchen Bin\""));

    discovery_tag_entity(buf, sizeof(buf), e, "4556", "b", "Kitchen Bin", "", "0.1.0");
    TEST_ASSERT_TRUE(has("\"ids\":[\"binrange_tag_4556\"]"));
    TEST_ASSERT_TRUE(has("\"uniq_id\":\"binrange_4556_b_distance\""));
}

void test_optional_fields_are_omitted_when_null() {
    EntitySpec e{"sensor", "samples", "Samples", nullptr, nullptr, nullptr,
                 "{{ value_json.n }}", true};
    discovery_tag_entity(buf, sizeof(buf), e, "4556", "a", "Kitchen Bin", "", "0.1.0");
    TEST_ASSERT_FALSE(has("\"unit_of_meas\""));
    TEST_ASSERT_FALSE(has("\"dev_cla\""));
    TEST_ASSERT_TRUE(has("\"ent_cat\":\"diagnostic\""));
}

void test_json_str_extracts_a_field() {
    const char *j = "{\"name\":\"Kitchen Bin\",\"area\":\"Driveway\",\"offset\":0.25}";
    char out[32];
    TEST_ASSERT_TRUE(json_str(j, "name", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Kitchen Bin", out);
    TEST_ASSERT_TRUE(json_str(j, "area", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Driveway", out);
}

void test_json_str_missing_key_reports_false() {
    char out[32];
    TEST_ASSERT_FALSE(json_str("{\"name\":\"x\"}", "area", out, sizeof(out)));
}

void test_json_float_extracts_a_number() {
    float v = -1.0f;
    TEST_ASSERT_TRUE(json_float("{\"offset\":0.25}", "offset", &v));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, v);
    TEST_ASSERT_TRUE(json_float("{\"offset\":-1.5}", "offset", &v));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.5f, v);
}

void test_json_str_truncates_safely() {
    char out[6];
    json_str("{\"name\":\"A very long name\"}", "name", out, sizeof(out));
    TEST_ASSERT_TRUE(strlen(out) < sizeof(out));
}

void test_entity_table_covers_the_spec() {
    TEST_ASSERT_TRUE(TAG_ENTITY_COUNT >= 8);
    bool found_distance = false;
    bool found_sensor_fault = false;
    bool found_wake_count = false;
    bool found_stale_range = false;
    for (size_t i = 0; i < TAG_ENTITY_COUNT; i++)
        if (strcmp(TAG_ENTITIES[i].key, "distance") == 0) found_distance = true;
        else if (strcmp(TAG_ENTITIES[i].key, "sensor_fault") == 0) found_sensor_fault = true;
        else if (strcmp(TAG_ENTITIES[i].key, "wake_count") == 0) found_wake_count = true;
        else if (strcmp(TAG_ENTITIES[i].key, "stale_range") == 0) found_stale_range = true;
    TEST_ASSERT_TRUE(found_distance);
    TEST_ASSERT_TRUE(found_sensor_fault);
    TEST_ASSERT_TRUE(found_wake_count);
    TEST_ASSERT_TRUE(found_stale_range);
}

void test_motion_template_leaves_unknown_telemetry_unknown() {
    bool found_motion = false;
    bool found_fault = false;
    for (size_t i = 0; i < TAG_ENTITY_COUNT; i++) {
        if (strcmp(TAG_ENTITIES[i].key, "moving") != 0 &&
            strcmp(TAG_ENTITIES[i].key, "sensor_fault") != 0) continue;
        discovery_tag_entity(buf, sizeof(buf), TAG_ENTITIES[i], "4556", "a",
                             "Kitchen Bin", "", "0.1.0");
        if (strcmp(TAG_ENTITIES[i].key, "moving") == 0) {
            TEST_ASSERT_TRUE(has("value_json.moving is true"));
            found_motion = true;
        } else {
            TEST_ASSERT_TRUE(has("value_json.sensor_fault is true"));
            found_fault = true;
        }
        TEST_ASSERT_TRUE(has("else none"));
    }
    TEST_ASSERT_TRUE(found_motion);
    TEST_ASSERT_TRUE(found_fault);
}

void test_every_table_entity_fits_the_buffer() {
    for (size_t i = 0; i < TAG_ENTITY_COUNT; i++) {
        size_t len = discovery_tag_entity(buf, sizeof(buf), TAG_ENTITIES[i],
                                          "4556", "a",
                                          "A Very Long Bin Name Indeed",
                                          "Somewhere", "0.1.0");
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_TRUE(len < sizeof(buf));
    }
}

void test_anchor_sensor_uses_anchor_device() {
    AnchorEntity e{"sensor", "heap", "Free heap", "B", nullptr, "measurement",
                   "{{ value_json.heap }}", true, nullptr, nullptr};
    size_t len = discovery_anchor_entity(buf, sizeof(buf), e, "a", "0.1.0");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(has("\"uniq_id\":\"binrange_anchor_a_heap\""));
    TEST_ASSERT_TRUE(has("\"stat_t\":\"binrange/anchor/a/state\""));
    // The anchor is its own device, distinct from any tag.
    TEST_ASSERT_TRUE(has("\"ids\":[\"binrange_anchor_a\"]"));
    TEST_ASSERT_FALSE(has("binrange_tag_"));
}

void test_anchor_control_has_a_command_topic() {
    AnchorEntity e{"number", "antenna_delay", "Antenna delay", nullptr, nullptr,
                   nullptr, "{{ value_json.antdly }}", false,
                   "antenna_delay", nullptr};
    discovery_anchor_entity(buf, sizeof(buf), e, "a", "0.1.0");
    TEST_ASSERT_TRUE(has("\"cmd_t\":\"binrange/anchor/a/cmd/antenna_delay\""));
}

void test_select_carries_its_options() {
    AnchorEntity e{"select", "phy", "PHY profile", nullptr, nullptr, nullptr,
                   "{{ value_json.phy }}", false, "phy",
                   "\"Short\",\"Long\",\"Max\""};
    discovery_anchor_entity(buf, sizeof(buf), e, "a", "0.1.0");
    TEST_ASSERT_TRUE(has("\"options\":[\"Short\",\"Long\",\"Max\"]"));
}

void test_button_omits_state_topic() {
    // A button has nothing to report, only a command.
    AnchorEntity e{"button", "reset_counters", "Reset counters", nullptr,
                   nullptr, nullptr, nullptr, false, "reset_counters", nullptr};
    discovery_anchor_entity(buf, sizeof(buf), e, "a", "0.1.0");
    TEST_ASSERT_FALSE(has("\"stat_t\""));
    TEST_ASSERT_TRUE(has("\"cmd_t\":\"binrange/anchor/a/cmd/reset_counters\""));
}

void test_anchor_table_is_populated() {
    TEST_ASSERT_TRUE(ANCHOR_ENTITY_COUNT >= 10);
    for (size_t i = 0; i < ANCHOR_ENTITY_COUNT; i++) {
        size_t len = discovery_anchor_entity(buf, sizeof(buf),
                                             ANCHOR_ENTITIES[i], "a", "0.1.0");
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_TRUE(len < sizeof(buf));
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_object_id_is_unique_per_tag_anchor_and_key);
    RUN_TEST(test_distance_payload_has_required_fields);
    RUN_TEST(test_availability_points_at_the_publishing_anchor);
    RUN_TEST(test_device_identifiers_are_shared_across_anchors);
    RUN_TEST(test_optional_fields_are_omitted_when_null);
    RUN_TEST(test_json_str_extracts_a_field);
    RUN_TEST(test_json_str_missing_key_reports_false);
    RUN_TEST(test_json_float_extracts_a_number);
    RUN_TEST(test_json_str_truncates_safely);
    RUN_TEST(test_entity_table_covers_the_spec);
    RUN_TEST(test_motion_template_leaves_unknown_telemetry_unknown);
    RUN_TEST(test_every_table_entity_fits_the_buffer);
    RUN_TEST(test_anchor_sensor_uses_anchor_device);
    RUN_TEST(test_anchor_control_has_a_command_topic);
    RUN_TEST(test_select_carries_its_options);
    RUN_TEST(test_button_omits_state_topic);
    RUN_TEST(test_anchor_table_is_populated);
    return UNITY_END();
}
