#include "tag_updater.h"

#include <cstdio>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <new>
#include "publisher.h"
#include "update_ble.h"
#include "update_store.h"

namespace {
uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
class Store final : public UpdateControllerStorage {
 public:
    bool begin(UpdateQueue &queue, uint64_t now) override { return update_store().begin(queue, now); }
    bool checkpoint(const UpdateSnapshot &s) override { return update_store().checkpoint(s); }
    const UpdateSnapshot &snapshot() const override { return update_store().snapshot(); }
    const UpdateRelease *staged_release() const override { return update_store().staged_release(); }
    bool acquire(const UpdateRelease &r, const uint8_t *&bytes) override { return update_store().acquire(r, bytes); }
    void release() override { update_store().release(); }
    size_t capacity() const override { return update_store().capacity(); }
};
Store store;
// This bounded management state is too large for the ESP32 static DRAM window.
// The WROVER's PSRAM is already required for firmware staging. Allocation is
// main-loop owned; the BLE worker only receives copied transport commands.
UpdateController *controller = nullptr;
bool begun = false, initialized = false, staging = false;
UpdateControllerSnapshot snapshot;
bool adopted(uint16_t tag) {
    const TagRecord *record = publisher_registry().find(tag);
    return record && record->adopted;
}
bool available() { return initialized && update_ble_ready(); }
}

void tag_updater_begin() {
    if (begun) return;
    begun = true;
    void *memory = heap_caps_malloc(sizeof(UpdateController), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return; // fail closed; snapshot reports the reason below
    controller = new (memory) UpdateController(&store, &update_ble_transport());
    initialized = controller->begin(now_ms());
    if (initialized) update_ble_begin(store.snapshot().association_count != 0);
}

void tag_updater_loop() {
    // Always drain disconnect ACKs after initialization. In particular, a
    // latched worker fault must not strand a safely quiesced stage buffer.
    if (initialized && !staging && (update_ble_ready() || controller->busy()))
        controller->loop(now_ms());
}

const UpdateControllerSnapshot &tag_updater_snapshot() {
    if (controller) controller->snapshot(snapshot);
    else {
        snapshot = {};
        snapshot.disabled = true;
        std::snprintf(snapshot.error, sizeof(snapshot.error), "Tag controller unavailable; PSRAM allocation required");
    }
    if (!snapshot.disabled && !available()) {
        snapshot.disabled = true;
        const char *reason = update_ble_fault() == UpdateBleFault::None
            ? "Bluetooth starting; no tag operations available yet"
            : update_ble_fault() == UpdateBleFault::Identity
            ? "Bluetooth identity unavailable; restore this anchor's identity, do not re-pair"
            : "Bluetooth worker unavailable; restart required";
        std::snprintf(snapshot.error, sizeof(snapshot.error), "%s", reason);
    }
    return snapshot;
}
const UpdateRelease *tag_updater_release() { return store.staged_release(); }
uint32_t tag_updater_restart_count() { return store.snapshot().restart_count; }
bool tag_updater_busy() { return staging || (controller && controller->busy()); }
UpdateQueueResult tag_updater_pair(const UpdateAssociation &association, uint32_t pin) {
    UpdateQueueResult result = UpdateQueueResult::Error;
    if (available()) {
        if (staging) result = UpdateQueueResult::Conflict;
        else if (!adopted(association.target.tag)) result = UpdateQueueResult::Invalid;
        else result = controller->commission(association, pin, now_ms());
    }
    volatile uint32_t *secret = &pin; *secret = 0;
    return result;
}
UpdateQueueResult tag_updater_queue(uint16_t tag, uint32_t *id) {
    if (id) *id = 0;
    if (!available()) return UpdateQueueResult::Error;
    if (staging) return UpdateQueueResult::Conflict;
    if (!adopted(tag)) return UpdateQueueResult::Invalid;
    return controller->queue(tag, id, now_ms());
}
bool tag_updater_cancel(uint32_t id) { return initialized && controller->cancel(id, now_ms()); }
bool tag_updater_retry(uint32_t id) {
    return available() && !staging && controller->retry(id, now_ms());
}
UpdateQueueResult tag_updater_motion(uint16_t tag, uint32_t idle_ms, uint32_t moving_ms) {
    if (!available()) return UpdateQueueResult::Error;
    if (staging) return UpdateQueueResult::Conflict;
    if (!adopted(tag)) return UpdateQueueResult::Invalid;
    return controller->motion_request(tag, idle_ms, moving_ms, now_ms());
}
void tag_updater_motion_forget(uint16_t tag) {
    if (controller) controller->motion_forget(tag, now_ms());
}
UpdateQueueResult tag_updater_stage_begin(const UpdateRelease &release) {
    if (!available() || tag_updater_snapshot().disabled) return UpdateQueueResult::Error;
    if (tag_updater_busy()) return UpdateQueueResult::Conflict;
    if (release.size > store.capacity() || !store.capacity()) return UpdateQueueResult::Capacity;
    if (!update_store().stage_begin(release)) return UpdateQueueResult::Conflict;
    staging = true;
    return UpdateQueueResult::Accepted;
}
bool tag_updater_stage_write(const uint8_t *bytes, size_t size) {
    if (!staging) return false;
    if (update_store().stage_write(bytes, size)) return true;
    tag_updater_stage_abort();
    return false;
}
bool tag_updater_stage_finish() {
    if (!staging) return false;
    staging = false;
    return update_store().stage_finish();
}
void tag_updater_stage_abort() {
    if (staging) update_store().stage_abort();
    staging = false;
}
