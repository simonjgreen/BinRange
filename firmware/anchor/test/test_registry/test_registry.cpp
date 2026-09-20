#include <unity.h>
#include <cstring>
#include "core/registry.h"

void setUp(void) {}
void tearDown(void) {}

void test_unknown_tag_is_created_unadopted() {
    Registry r;
    TagRecord *t = r.touch(0x4556, 1000);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_UINT16(0x4556, t->addr);
    TEST_ASSERT_FALSE(t->adopted);
    TEST_ASSERT_EQUAL_UINT32(1000, t->last_seen_ms);
    TEST_ASSERT_EQUAL_UINT32(1, r.size());
    TEST_ASSERT_EQUAL_UINT32(0, r.adopted_count());
}

void test_touch_is_idempotent_and_updates_last_seen() {
    Registry r;
    r.touch(0x4556, 1000);
    TagRecord *t = r.touch(0x4556, 2000);
    TEST_ASSERT_EQUAL_UINT32(1, r.size());
    TEST_ASSERT_EQUAL_UINT32(2000, t->last_seen_ms);
}

void test_adopt_sets_name_and_offset() {
    Registry r;
    r.touch(0x4556, 1000);
    TEST_ASSERT_TRUE(r.adopt(0x4556, "Kitchen Bin", "Driveway", 0.25f, 3600));
    TagRecord *t = r.find(0x4556);
    TEST_ASSERT_TRUE(t->adopted);
    TEST_ASSERT_EQUAL_STRING("Kitchen Bin", t->name);
    TEST_ASSERT_EQUAL_STRING("Driveway", t->area);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, t->offset);
    TEST_ASSERT_EQUAL_UINT32(3600, t->stale_after_s);
    TEST_ASSERT_EQUAL_UINT32(1, r.adopted_count());
}

void test_adopt_creates_the_record_if_never_seen() {
    Registry r;
    TEST_ASSERT_TRUE(r.adopt(0x0001, "Garden Bin", "Garden", 0.0f, 0));
    TagRecord *t = r.find(0x0001);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_TRUE(t->adopted);
}

void test_stale_after_defaults_when_unset() {
    // A motion-woken tag is silent because it has not moved. Zero means
    // "use the default", never "expire immediately".
    Registry r;
    r.adopt(0x0002, "Compost", "", 0.0f, 0);
    TEST_ASSERT_EQUAL_UINT32(21600, r.find(0x0002)->stale_after_s);
}

void test_stale_after_is_per_tag() {
    Registry r;
    r.adopt(0x0003, "Glass", "", 0.0f, 900);
    TEST_ASSERT_EQUAL_UINT32(900, r.find(0x0003)->stale_after_s);
}

void test_retained_replay_does_not_restart_never_heard_absence_clock() {
    Registry r;
    TEST_ASSERT_TRUE(r.adopt(0xb100, "Development tag", "", 0.0f, 21600, 1000));
    // A retained config replay on MQTT reconnect must not postpone the alarm.
    TEST_ASSERT_TRUE(r.adopt(0xb100, "Development tag", "", 0.0f, 21600, 2000));
    TagRecord *t = r.find(0xb100);
    TEST_ASSERT_FALSE(t->heard);
    TEST_ASSERT_EQUAL_UINT32(1000, t->adopted_ms);
}

void test_new_tag_starts_not_stale_with_no_battery() {
    Registry r;
    TagRecord *t = r.touch(0x0004, 1000);
    TEST_ASSERT_FALSE(t->stale);
    TEST_ASSERT_EQUAL_UINT16(0, t->batt_mv);
}

void test_forget_removes_the_record() {
    Registry r;
    r.adopt(0x4556, "Kitchen Bin", "Driveway", 0.0f, 0);
    TEST_ASSERT_TRUE(r.forget(0x4556));
    TEST_ASSERT_NULL(r.find(0x4556));
    TEST_ASSERT_EQUAL_UINT32(0, r.size());
}

void test_forget_unknown_tag_reports_failure() {
    Registry r;
    TEST_ASSERT_FALSE(r.forget(0xDEAD));
}

void test_registry_is_bounded() {
    Registry r;
    for (int i = 0; i < REGISTRY_MAX_TAGS; i++)
        TEST_ASSERT_NOT_NULL(r.touch(0x1000 + i, 1));
    TEST_ASSERT_EQUAL_UINT32(REGISTRY_MAX_TAGS, r.size());
    TEST_ASSERT_NULL(r.touch(0x9999, 1));
}

void test_names_are_truncated_not_overflowed() {
    Registry r;
    char longname[128];
    memset(longname, 'x', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    r.adopt(0x4556, longname, longname, 0.0f, 0);
    TagRecord *t = r.find(0x4556);
    TEST_ASSERT_TRUE(strlen(t->name) < sizeof(t->name));
    TEST_ASSERT_TRUE(strlen(t->area) < sizeof(t->area));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_tag_is_created_unadopted);
    RUN_TEST(test_touch_is_idempotent_and_updates_last_seen);
    RUN_TEST(test_adopt_sets_name_and_offset);
    RUN_TEST(test_adopt_creates_the_record_if_never_seen);
    RUN_TEST(test_stale_after_defaults_when_unset);
    RUN_TEST(test_stale_after_is_per_tag);
    RUN_TEST(test_retained_replay_does_not_restart_never_heard_absence_clock);
    RUN_TEST(test_new_tag_starts_not_stale_with_no_battery);
    RUN_TEST(test_forget_removes_the_record);
    RUN_TEST(test_forget_unknown_tag_reports_failure);
    RUN_TEST(test_registry_is_bounded);
    RUN_TEST(test_names_are_truncated_not_overflowed);
    return UNITY_END();
}
