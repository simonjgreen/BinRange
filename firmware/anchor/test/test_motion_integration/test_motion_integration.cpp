#include <unity.h>
#include <deque>
#include <string>
#include <cstring>
#include <esp_timer.h>
#include "../../src/motion_mqtt.cpp"

// External boundaries only: broker packets, persistent association storage, BLE.
// The MQTT adapter, real controller, wire codec and readback sequence all run.
namespace {
struct Store : UpdateControllerStorage {
    UpdateSnapshot saved{};
    bool begin(UpdateQueue &, uint64_t) override { return true; }
    bool checkpoint(const UpdateSnapshot &) override { return true; }
    const UpdateSnapshot &snapshot() const override { return saved; }
    const UpdateRelease *staged_release() const override { return nullptr; }
    bool acquire(const UpdateRelease &, const uint8_t *&) override { return false; }
    void release() override {}
    size_t capacity() const override { return 0; }
} store;
struct Tag : UpdateControllerTransport {
    std::deque<UpdateTransportEvent> events;
    SmpMotionConfig saved{7000, 600000, 45000, 320, 3};
    unsigned writes = 0;
    bool insecure = false;
    bool submit(const UpdateTransportCommand &c) override {
        UpdateTransportEvent e{}; e.operation = c.operation; e.session = c.session;
        switch (c.kind) {
        case UpdateTransportCommandKind::Connect: e.kind = UpdateTransportEventKind::Connected; break;
        case UpdateTransportCommandKind::Security:
            TEST_ASSERT_FALSE(c.commissioning); // no implicit re-pairing
            e.kind = UpdateTransportEventKind::Secured; e.bonded_mitm_sc = !insecure; break;
        case UpdateTransportCommandKind::Disconnect: e.kind = UpdateTransportEventKind::Disconnected; break;
        case UpdateTransportCommandKind::Exchange:
            e.kind = UpdateTransportEventKind::Reply;
            if (c.smp == SmpCommand::TagStatus) {
                std::strcpy(e.reply.tag.id, "f00dbaad12345678"); e.reply.tag.tag = 0xb100;
                e.reply.tag.confirmed = true; e.reply.tag.config_status_known = true;
            } else if (c.smp == SmpCommand::ConfigRead) {
                e.reply.config = saved;
            } else if (c.smp == SmpCommand::ConfigWrite) {
                uint8_t wire[512]; std::memcpy(wire, c.frame, c.frame_size); wire[0] = 3;
                SmpAssembler decoder; decoder.begin(SmpCommand::ConfigWrite, wire[6]);
                TEST_ASSERT_EQUAL_INT((int)SmpFeed::Complete, (int)decoder.feed(wire, c.frame_size));
                saved = decoder.reply()->config; ++writes; e.reply.config = saved;
            } else TEST_FAIL_MESSAGE("Unexpected firmware-update command during interval change");
        }
        events.push_back(e); return true;
    }
    bool poll(UpdateTransportEvent &e) override {
        if (events.empty()) return false;
        e = events.front(); events.pop_front(); return true;
    }
} tag;
UpdateController *controller;
Registry registry;
UpdateControllerSnapshot view;
std::string published;
void pump() {
    for (unsigned i = 0; i < 120; ++i) {
        ++fake_sdk::now; fake_sdk::uptime_us = uint64_t(fake_sdk::now) * 1000;
        controller->loop(fake_sdk::now); motion_mqtt_loop();
    }
}
}
bool mqtt_connected() { return true; }
bool mqtt_publish(const char *topic, const char *value, bool retained) {
    TEST_ASSERT_TRUE(retained);
    if (!std::strcmp(topic, "binrange/tag/b100/anchor/a/motion")) published = value;
    return true;
}
Registry &publisher_registry() { return registry; }
const UpdateControllerSnapshot &tag_updater_snapshot() { controller->snapshot(view); return view; }
UpdateQueueResult tag_updater_motion(uint16_t id, uint32_t idle, uint32_t moving) {
    return controller->motion_request(id, idle, moving, fake_sdk::now);
}
void tag_updater_motion_forget(uint16_t id) { controller->motion_forget(id, fake_sdk::now); }
void setUp() {
    store.saved = {}; store.saved.association_count = 1;
    store.saved.associations[0] = {{0xb100, "f00dbaad12345678"}, {1,2,3,4,5,6}, 0};
    tag = Tag{}; registry = Registry{}; registry.adopt(0xb100, "Test bin", "", 0, 21600);
    motion_slots = {}; motion_deletions = {}; motion_connected = false; motion_cursor = 0;
    fake_sdk::now = 1; fake_sdk::uptime_us = 1000; published.clear();
    controller = new UpdateController(&store, &tag); TEST_ASSERT_TRUE(controller->begin(1));
}
void tearDown() { delete controller; }
void test_home_assistant_commands_reach_tag_and_return_confirmed_values() {
    pump(); TEST_ASSERT_EQUAL_UINT(0, tag.writes);
    TEST_ASSERT_NOT_NULL(strstr(published.c_str(), "\"idle_ms\":600000"));
    motion_mqtt_message("binrange/tag/b100/anchor/a/motion/idle/set", "180");
    motion_mqtt_message("binrange/tag/b100/anchor/a/motion/moving/set", "9.0");
    pump();
    TEST_ASSERT_EQUAL_UINT(1, tag.writes); // both controls coalesce before BLE starts
    TEST_ASSERT_EQUAL_UINT32(180000, tag.saved.idle_ms);
    TEST_ASSERT_EQUAL_UINT32(9000, tag.saved.moving_ms);
    TEST_ASSERT_EQUAL_UINT32(45000, tag.saved.quiet_ms);
    TEST_ASSERT_EQUAL_UINT32(320, tag.saved.threshold_mg);
    TEST_ASSERT_EQUAL_UINT32(3, tag.saved.duration_samples);
    TEST_ASSERT_NOT_NULL(strstr(published.c_str(), "\"idle_ms\":180000,\"moving_ms\":9000"));
    TEST_ASSERT_NOT_NULL(strstr(published.c_str(), "\"status\":\"applied\""));
    TEST_ASSERT_NOT_NULL(strstr(published.c_str(), "\"pending\":false"));
    // Anchor restart loses queued requests, but reads settings saved by the tag.
    delete controller; controller = new UpdateController(&store, &tag); controller->begin(fake_sdk::now);
    motion_slots = {}; pump();
    TEST_ASSERT_EQUAL_UINT(1, tag.writes);
    TEST_ASSERT_NOT_NULL(strstr(published.c_str(), "\"idle_ms\":180000,\"moving_ms\":9000"));
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_home_assistant_commands_reach_tag_and_return_confirmed_values);
    return UNITY_END();
}
