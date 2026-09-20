#include <unity.h>
#include <string>
#include <vector>
#include <esp_timer.h>
#include "../../src/motion_mqtt.cpp"

namespace {
struct Packet { std::string topic, body; bool retained; };
std::vector<Packet> packets;
Registry registry;
UpdateControllerSnapshot snapshot;
bool connected = true, reject_publish = false;
unsigned requests;
uint16_t requested_tag;
uint32_t requested_idle, requested_moving;
UpdateQueueResult request_result = UpdateQueueResult::Accepted;
void pump() { for (int i = 0; i < 200; ++i) motion_mqtt_loop(); }
std::string body(const char *topic) {
    for (auto p = packets.rbegin(); p != packets.rend(); ++p) if (p->topic == topic) return p->body;
    return "";
}
}
bool mqtt_connected() { return connected; }
bool mqtt_publish(const char *topic, const char *value, bool retained) {
    if (reject_publish) return false;
    packets.push_back({topic, value, retained}); return true;
}
Registry &publisher_registry() { return registry; }
const UpdateControllerSnapshot &tag_updater_snapshot() { return snapshot; }
UpdateQueueResult tag_updater_motion(uint16_t tag, uint32_t idle, uint32_t moving) {
    ++requests; requested_tag = tag; requested_idle = idle; requested_moving = moving;
    return request_result;
}
void tag_updater_motion_forget(uint16_t) {}
void setUp() {
    registry = Registry{}; snapshot = {}; packets.clear(); connected = true; reject_publish = false;
    requests = 0; request_result = UpdateQueueResult::Accepted;
    fake_sdk::now = 1; fake_sdk::uptime_us = 1000;
    motion_slots = {}; motion_deletions = {}; motion_connected = false; motion_cursor = 0;
    registry.adopt(0xb100, "Garden bin", "", 0, 21600);
    snapshot.association_count = 1; snapshot.associations[0].target.tag = 0xb100;
}
void tearDown() {}
void test_discovery_numbers_use_confirmed_state_and_nonretained_commands() {
    pump();
    auto idle = body("homeassistant/number/binrange_b100_a_idle_interval/config");
    TEST_ASSERT_NOT_NULL(strstr(idle.c_str(), "\"min\":60,\"max\":3600"));
    TEST_ASSERT_NOT_NULL(strstr(idle.c_str(), "\"unit_of_measurement\":\"s\""));
    TEST_ASSERT_NOT_NULL(strstr(idle.c_str(), "\"optimistic\":false,\"retain\":false"));
    TEST_ASSERT_NOT_NULL(strstr(idle.c_str(), "binrange/tag/b100/anchor/a/motion/idle/set"));
    TEST_ASSERT_NOT_NULL(strstr(idle.c_str(), "binrange_tag_b100"));
    TEST_ASSERT_NOT_NULL(strstr(body("homeassistant/number/binrange_b100_a_moving_interval/config").c_str(), "\"min\":1,\"max\":60"));
    TEST_ASSERT_NOT_NULL(strstr(body("binrange/tag/b100/anchor/a/motion").c_str(), "\"idle_ms\":null"));
    for (const auto &p : packets) TEST_ASSERT_LESS_THAN_UINT(2048, p.topic.size() + p.body.size() + 8);
}
void test_commands_validate_bounds_and_target_before_queueing() {
    pump(); requests = 0;
    TEST_ASSERT_TRUE(motion_mqtt_message("binrange/tag/b100/anchor/a/motion/idle/set", "300.0"));
    TEST_ASSERT_EQUAL_UINT32(300000, requested_idle);
    TEST_ASSERT_EQUAL_UINT32(0, requested_moving);
    TEST_ASSERT_EQUAL_HEX16(0xb100, requested_tag);
    motion_mqtt_message("binrange/tag/b100/anchor/a/motion/moving/set", "12");
    TEST_ASSERT_EQUAL_UINT32(12000, requested_moving);
    TEST_ASSERT_EQUAL_UINT32(0, requested_idle);
    for (const char *v : {"0", "61", "NaN", "inf", "5x", "5.5", "-1", "", "99999999999999999999999999"})
        motion_mqtt_message("binrange/tag/b100/anchor/a/motion/moving/set", v);
    motion_mqtt_message("binrange/tag/b101/anchor/a/motion/idle/set", "300");
    TEST_ASSERT_FALSE(motion_mqtt_message("binrange/tag/b100/anchor/b/motion/idle/set", "300"));
    TEST_ASSERT_EQUAL_UINT(2, requests);
}
void test_changes_publish_promptly_and_forget_survives_broker_outage() {
    pump(); packets.clear();
    snapshot.motion[0].tag = 0xb100; snapshot.motion[0].known = true;
    snapshot.motion[0].config = {12000, 300000, 30000, 250, 2};
    snapshot.motion[0].phase = MotionPhase::Applied;
    snapshot.motion[0].changed_ms = 2; snapshot.motion[0].seen_ms = 2;
    fake_sdk::now = 2; fake_sdk::uptime_us = 2000; pump();
    TEST_ASSERT_NOT_NULL(strstr(body("binrange/tag/b100/anchor/a/motion").c_str(), "\"idle_ms\":300000"));
    connected = false; motion_mqtt_forget(0xb100); registry.forget(0xb100); pump();
    packets.clear(); connected = true; reject_publish = true; pump(); TEST_ASSERT_TRUE(packets.empty());
    reject_publish = false; pump();
    TEST_ASSERT_EQUAL_UINT(6, packets.size());
    for (const auto &p : packets) TEST_ASSERT_TRUE(p.body.empty());
}
void test_failed_reads_are_refreshed_without_replaying_writes() {
    pump(); requests = 0;
    snapshot.motion[0].tag = 0xb100;
    snapshot.motion[0].phase = MotionPhase::Failed;
    fake_sdk::now = 900002; fake_sdk::uptime_us = 900002000; pump();
    TEST_ASSERT_EQUAL_UINT(1, requests);
    TEST_ASSERT_EQUAL_UINT32(0, requested_idle);
    TEST_ASSERT_EQUAL_UINT32(0, requested_moving);
    pump(); TEST_ASSERT_EQUAL_UINT(1, requests);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_failed_reads_are_refreshed_without_replaying_writes);
    RUN_TEST(test_discovery_numbers_use_confirmed_state_and_nonretained_commands);
    RUN_TEST(test_commands_validate_bounds_and_target_before_queueing);
    RUN_TEST(test_changes_publish_promptly_and_forget_survives_broker_outage);
    return UNITY_END();
}
