#include <unity.h>
#include <string>
#include <vector>
#include <esp_timer.h>
#include "../../src/update_mqtt.cpp"

namespace {
struct Packet { std::string topic, body; bool retained; };
std::vector<Packet> packets;
Registry registry;
UpdateControllerSnapshot state;
bool connected = true, reject_publish = false;
void pump() { for (unsigned i = 0; i < 80; ++i) update_mqtt_loop(); }
const std::string &body(const char *topic) {
    for (auto p = packets.rbegin(); p != packets.rend(); ++p)
        if (p->topic == topic) return p->body;
    static const std::string missing;
    return missing;
}
void at(uint64_t ms) { fake_sdk::now = uint32_t(ms); fake_sdk::uptime_us = ms * 1000; }
}
bool mqtt_connected() { return connected; }
bool mqtt_publish(const char *topic, const char *value, bool retained) {
    if (reject_publish) return false;
    packets.push_back({topic, value, retained}); return true;
}
Registry &publisher_registry() { return registry; }
const UpdateControllerSnapshot &tag_updater_snapshot() { return state; }
const UpdateRelease *tag_updater_release() { return nullptr; }
uint32_t tag_updater_restart_count() { return 2; }
void setUp() {
    packets.clear(); registry = Registry{}; state = {};
    connected = true; reject_publish = false; at(1);
    for (auto &slot : discovery) slot = {};
    for (auto &slot : deletions) slot = {};
    anchor_discovery_step = 0; state_cursor = 0; last_cycle_ms = 0;
    state_cycle = false; was_connected = false;
    registry.adopt(0xb100, "Development tag", "", 0, 21600);
}
void tearDown() {}

void test_terminal_pairing_is_not_an_active_updater() {
    state.pairing.phase = UpdatePairingPhase::Successful;
    pump();
    TEST_ASSERT_NOT_NULL(strstr(body("binrange/anchor/a/update").c_str(), "\"state\":\"idle\""));
    packets.clear(); at(2001); pump();
    TEST_ASSERT_EQUAL_UINT(0, packets.size()); // terminal pairing uses idle heartbeat
}
void test_completion_is_published_without_idle_heartbeat_delay() {
    state.active_job_id = 7;
    state.job_count = 1; state.jobs[0].id = 7; state.jobs[0].target.tag = 0xb100;
    state.jobs[0].phase = UpdatePhase::Checking;
    pump(); packets.clear(); at(100);
    state.active_job_id = 0; state.jobs[0].phase = UpdatePhase::Successful;
    pump();
    TEST_ASSERT_NOT_NULL(strstr(body("binrange/tag/b100/anchor/a/update").c_str(), "\"phase\":\"successful\""));
}
void test_observation_age_survives_millis_wrap() {
    const uint64_t now = (uint64_t(1) << 32) + 5000; at(now);
    state.job_count = 1; state.jobs[0].id = 1; state.jobs[0].target.tag = 0xb100;
    state.jobs[0].phase = UpdatePhase::Successful;
    state.observed[0].image_valid = true; state.observed[0].seen_ms = now - 1234;
    pump();
    TEST_ASSERT_NOT_NULL(strstr(body("binrange/tag/b100/anchor/a/update").c_str(), "\"observed_age_ms\":1234"));
}
void test_discovery_keeps_existing_device_metadata_and_unknown_observations() {
    pump();
    const auto &config = body("homeassistant/sensor/binrange_anchor_a_update_state/config");
    TEST_ASSERT_NOT_NULL(strstr(config.c_str(), "binrange_anchor_a"));
    TEST_ASSERT_NULL(strstr(config.c_str(), "UWB bin tag"));
    TEST_ASSERT_NOT_NULL(strstr(body("binrange/tag/b100/anchor/a/update").c_str(), "\"job\":null"));
    for (const auto &p : packets) {
        TEST_ASSERT_TRUE(p.retained);
        TEST_ASSERT_LESS_THAN_UINT(2048, p.body.size() + p.topic.size() + 8);
    }
}
void test_forget_survives_disconnect_and_publish_failure_without_recreation() {
    pump(); packets.clear(); connected = false;
    update_mqtt_forget(0xb100); registry.forget(0xb100); pump();
    TEST_ASSERT_TRUE(packets.empty());
    connected = true; reject_publish = true; pump();
    TEST_ASSERT_TRUE(packets.empty());
    reject_publish = false; pump();
    size_t deletions_seen = 0;
    for (const auto &p : packets) if (p.topic.find("b100") != std::string::npos) {
        TEST_ASSERT_TRUE(p.body.empty()); ++deletions_seen;
    }
    TEST_ASSERT_EQUAL_UINT(6, deletions_seen);
    connected = false; pump(); packets.clear(); connected = true; pump();
    for (const auto &p : packets) TEST_ASSERT_TRUE(p.topic.find("b100") == std::string::npos);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_terminal_pairing_is_not_an_active_updater);
    RUN_TEST(test_completion_is_published_without_idle_heartbeat_delay);
    RUN_TEST(test_observation_age_survives_millis_wrap);
    RUN_TEST(test_discovery_keeps_existing_device_metadata_and_unknown_observations);
    RUN_TEST(test_forget_survives_disconnect_and_publish_failure_without_recreation);
    return UNITY_END();
}
