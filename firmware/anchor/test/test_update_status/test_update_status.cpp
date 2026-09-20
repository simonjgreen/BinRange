#include <unity.h>
#include <cstring>
#include "core/update_status.h"

void setUp() {}
void tearDown() {}

void test_status_strings_cannot_inject_json() {
    char buffer[128]; UpdateJson j(buffer, sizeof(buffer));
    j.raw("{\"name\":"); j.str("Glass \"x\"\\\n\t"); j.raw("}");
    TEST_ASSERT_GREATER_THAN(0, j.finish());
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"Glass \\\"x\\\"\\\\\\u000a\\u0009\"}", buffer);
}

void test_overflow_is_empty_not_a_partial_success_document() {
    char buffer[12]; UpdateJson j(buffer, sizeof(buffer));
    j.raw("{\"phase\":"); j.str("successful"); j.raw("}");
    TEST_ASSERT_EQUAL(0, j.finish());
    TEST_ASSERT_EQUAL_STRING("", buffer);
}

void test_unknown_tag_observation_is_not_false_confirmation_or_zero_age() {
    UpdateControllerSnapshot s{}; s.job_count = 1;
    s.jobs[0].id = 7; s.jobs[0].target.tag = 0xb100;
    s.jobs[0].phase = UpdatePhase::NeedsAction;
    s.active_job_id = 7;
    char buffer[1800]; UpdateJson j(buffer, sizeof(buffer));
    update_job_json(j, s, 0, "Garden", 9000);
    TEST_ASSERT_GREATER_THAN(0, j.finish());
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"observed_version\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"confirmed\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"observed_age_ms\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"maintenance\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"phase\":\"needs_action\""));
}

void test_observed_result_and_age_are_independent_of_job_phase() {
    UpdateControllerSnapshot s{}; s.job_count = 1;
    s.jobs[0].id = 7; s.jobs[0].phase = UpdatePhase::Checking;
    s.observed[0].image_valid = true; s.observed[0].seen_ms = 1000;
    s.observed[0].active.confirmed = true;
    std::strcpy(s.observed[0].active.version, "0.2.2");
    char buffer[1800]; UpdateJson j(buffer, sizeof(buffer));
    update_job_json(j, s, 0, "Glass", 4000);
    TEST_ASSERT_GREATER_THAN(0, j.finish());
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"phase\":\"checking\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"confirmed\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"observed_age_ms\":3000"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"maintenance\":false"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_status_strings_cannot_inject_json);
    RUN_TEST(test_overflow_is_empty_not_a_partial_success_document);
    RUN_TEST(test_unknown_tag_observation_is_not_false_confirmation_or_zero_age);
    RUN_TEST(test_observed_result_and_age_are_independent_of_job_phase);
    return UNITY_END();
}
