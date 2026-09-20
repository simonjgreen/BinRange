#include <unity.h>
#include <cmath>
#include <cstring>
#include <cctype>
#include "core/tagpayload.h"

static char buf[512];

void setUp(void) {}
void tearDown(void) {}

// A real check beats a heuristic: walk the flat object and require every
// element to be "key":value with a non-empty value and exactly one separator
// between pairs. The first attempt at this used pattern matching and produced
// false failures, which is its own lesson about testing parsers with guesses.
static bool well_formed(const char *j) {
    size_t n = strlen(j);
    if (n < 2 || j[0] != '{' || j[n - 1] != '}') return false;
    size_t i = 1;
    bool first = true;
    while (i < n - 1) {
        if (!first) {
            if (j[i] != ',') return false;
            i++;
        }
        first = false;
        if (j[i] != '"') return false;          // key must be quoted
        i++;
        size_t ks = i;
        while (i < n && j[i] != '"') i++;
        if (i >= n || i == ks) return false;    // unterminated or empty key
        i++;
        if (j[i] != ':') return false;
        i++;
        size_t vs = i;
        if (j[i] == '"') {                      // string value
            i++;
            while (i < n && j[i] != '"') i++;
            if (i >= n) return false;
            i++;
        } else {
            while (i < n - 1 && j[i] != ',') i++;
        }
        if (i == vs) return false;              // empty value
    }
    return i == n - 1;
}

static TagSummary full() {
    TagSummary t{};
    t.n = 16; t.mean = 10.0f; t.sd = 0.02f;
    t.dmin = 9.9f; t.dmax = 10.1f;
    t.rssi = -83.0f; t.fp = -73.0f; t.gap = -10.0f;
    return t;
}

void test_full_payload_is_well_formed() {
    TagSummary t = full();
    TagState s{&t, 0.0f, "2026-09-08T12:00:00+00:00", 3, false,
               true, true, true, false, true, 2, true, 123, true, 2950,
               true, false, true};
    char mqtt_payload[384];
    size_t len = tag_state_json(mqtt_payload, sizeof(mqtt_payload), s);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(well_formed(mqtt_payload));
    TEST_ASSERT_NOT_NULL(strstr(mqtt_payload, "\"d\":10.000"));
    TEST_ASSERT_NOT_NULL(strstr(mqtt_payload, "\"misses\":2"));
    TEST_ASSERT_NOT_NULL(strstr(mqtt_payload, "\"moving\":true"));
    TEST_ASSERT_NOT_NULL(strstr(mqtt_payload, "\"sensor_fault\":false"));
    TEST_ASSERT_NOT_NULL(strstr(mqtt_payload, "\"wake_count\":123"));
    TEST_ASSERT_NOT_NULL(strstr(mqtt_payload, "\"ok\":null"));
    TEST_ASSERT_NOT_NULL(strstr(mqtt_payload, "\"bat\":2.950"));
}

void test_received_finals_do_not_fabricate_a_success_rate() {
    TagSummary t = full();
    TagState s{&t, 0.0f, "", 0, false,
               true, false, true, false, true, 2, false, 0, false, 0,
               true, false, true};
    tag_state_json(buf, sizeof(buf), s);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ok\":null"));
}

void test_no_attempts_reports_null_not_a_division() {
    TagSummary t = full();
    TagState s{&t, 0.0f, "", 0, false,
               false, false, false, false, false, 0, false, 0, false, 0,
               true, false, true};
    tag_state_json(buf, sizeof(buf), s);
    TEST_ASSERT_TRUE(well_formed(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ok\":null"));
}

void test_stale_republish_with_no_samples_is_valid() {
    // This is the case that shipped broken: the window is reset, so every
    // distance field is NaN.
    TagSummary t{};
    t.n = 0;
    t.mean = t.sd = t.dmin = t.dmax = t.rssi = t.fp = t.gap = NAN;
    TagState s{&t, 0.0f, "", 7200, true,
               false, false, false, false, false, 0, false, 0, false, 0,
               true, false, true};
    size_t len = tag_state_json(buf, sizeof(buf), s);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(well_formed(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"d\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"stale\":true"));
    TEST_ASSERT_NULL(strstr(buf, "nan"));
}

void test_offset_is_applied_to_distance() {
    TagSummary t = full();
    TagState s{&t, 0.25f, "", 0, false,
               false, false, false, false, false, 0, false, 0, false, 0,
               true, false, true};
    tag_state_json(buf, sizeof(buf), s);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"d\":10.250"));
}

void test_legacy_telemetry_is_explicitly_unknown() {
    TagSummary t = full();
    TagState s{&t, 0.0f, "", 0, false,
               false, false, false, false, false, 0, false, 0, false, 0,
               true, false, true};
    tag_state_json(buf, sizeof(buf), s);
    TEST_ASSERT_TRUE(well_formed(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bat\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"moving\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"misses\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"wake_count\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"sensor_fault\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":null"));
}

void test_never_heard_tag_reports_unknown_measurement_and_absence() {
    TagSummary t{};
    t.mean = t.sd = t.rssi = t.fp = t.gap = NAN;
    TagState s{&t, 0.0f, "", 0, true,
               false, false, false, false, false, 0, false, 0, false, 0,
               false, false, false};
    tag_state_json(buf, sizeof(buf), s);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"d\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"age\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"missing\":null"));
}

void test_republish_keeps_the_recorded_reception_timestamp() {
    TagSummary t = full();
    TagState first{&t, 0.0f, "2026-09-19T10:00:01+00:00", 4, false,
                   false, false, false, false, false, 0, false, 0, false, 0,
                   true, false, true};
    TagState later = first;
    later.age_s = 7200;
    tag_state_json(buf, sizeof(buf), first);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":\"2026-09-19T10:00:01+00:00\""));
    tag_state_json(buf, sizeof(buf), later);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"age\":7200"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts\":\"2026-09-19T10:00:01+00:00\""));
}

void test_sensor_fault_makes_motion_unknown() {
    TagSummary t = full();
    TagState s{&t, 0.0f, "", 0, false,
               false, false, true, true, true, 7, true, 44, true, 3000,
               true, false, true};
    tag_state_json(buf, sizeof(buf), s);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"moving\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"sensor_fault\":true"));
}

void test_zero_voltage_clears_the_retained_measurement() {
    TagSummary t = full();
    TagState s{&t, 0.0f, "", 0, false,
               true, false, true, false, true, 7, true, 44, false, 0,
               true, false, true};
    tag_state_json(buf, sizeof(buf), s);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bat\":null"));
    TEST_ASSERT_NULL(strstr(buf, "\"bat\":0.000"));
}

void test_truncation_reports_failure_rather_than_bad_json() {
    TagSummary t = full();
    TagState s{&t, 0.0f, "2026-09-08T12:00:00+00:00", 3, false,
               true, true, true, false, true, 2, true, 123, true, 2950,
               true, false, true};
    char small[40];
    TEST_ASSERT_EQUAL_UINT32(0, tag_state_json(small, sizeof(small), s));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_full_payload_is_well_formed);
    RUN_TEST(test_received_finals_do_not_fabricate_a_success_rate);
    RUN_TEST(test_no_attempts_reports_null_not_a_division);
    RUN_TEST(test_stale_republish_with_no_samples_is_valid);
    RUN_TEST(test_offset_is_applied_to_distance);
    RUN_TEST(test_legacy_telemetry_is_explicitly_unknown);
    RUN_TEST(test_never_heard_tag_reports_unknown_measurement_and_absence);
    RUN_TEST(test_republish_keeps_the_recorded_reception_timestamp);
    RUN_TEST(test_sensor_fault_makes_motion_unknown);
    RUN_TEST(test_zero_voltage_clears_the_retained_measurement);
    RUN_TEST(test_truncation_reports_failure_rather_than_bad_json);
    return UNITY_END();
}
