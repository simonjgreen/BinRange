#include "update_mqtt.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <time.h>
#include <esp_timer.h>

#include "core/discovery.h"
#include "core/topics.h"
#include "core/update_status.h"
#include "core/version.h"
#include "mqttlink.h"
#include "publisher.h"
#include "tag_updater.h"

namespace {

constexpr char kAnchor[] = "a";
constexpr size_t kPayloadCapacity = 1800;
constexpr uint32_t kActiveIntervalMs = 1000;
constexpr uint32_t kIdleIntervalMs = 30000;

struct DiscoverySlot {
    uint16_t tag;
    uint8_t step;
};
struct PendingDeletion {
    uint16_t tag;
    uint8_t step;
};
struct DiagnosticSpec {
    const char *component;
    const char *key;
    const char *name;
    const char *template_value;
};

const DiagnosticSpec kTagDiagnostics[] = {
    {"sensor", "phase", "Update phase", "{{ value_json.job.phase if value_json.job else none }}"},
    {"sensor", "progress", "Update progress", "{{ value_json.job.acknowledged if value_json.job else none }}"},
    {"sensor", "error", "Update error", "{{ value_json.job.error if value_json.job else none }}"},
    {"sensor", "observed_firmware", "Observed firmware", "{{ value_json.job.observed_version if value_json.job else none }}"},
    {"sensor", "confirmed", "Update confirmation", "{{ value_json.job.confirmed if value_json.job else none }}"},
};
const DiagnosticSpec kAnchorDiagnostics[] = {
    {"sensor", "state", "Updater state", "{{ value_json.state }}"},
    {"sensor", "active_job", "Active update job", "{{ value_json.active_job.id if value_json.active_job else none }}"},
    {"sensor", "error", "Updater error", "{{ value_json.error }}"},
};

DiscoverySlot discovery[REGISTRY_MAX_TAGS]{};
PendingDeletion deletions[REGISTRY_MAX_TAGS]{};
uint8_t anchor_discovery_step;
size_t state_cursor;
uint32_t last_cycle_ms;
bool state_cycle;
bool was_connected;
uint32_t last_active_job;
size_t last_job_count;
UpdatePairingPhase last_pairing = UpdatePairingPhase::Idle;
bool last_disabled;
char payload[kPayloadCapacity];

bool pairing_active(UpdatePairingPhase phase) {
    return phase == UpdatePairingPhase::Connecting || phase == UpdatePairingPhase::Securing ||
        phase == UpdatePairingPhase::Checking || phase == UpdatePairingPhase::Disconnecting;
}

void update_topic(char *out, size_t size, const char *tag) {
    std::snprintf(out, size, BINRANGE_TOPIC_BASE "/tag/%s/anchor/%s/update", tag, kAnchor);
}

void anchor_update_topic(char *out, size_t size) {
    std::snprintf(out, size, BINRANGE_TOPIC_BASE "/anchor/%s/update", kAnchor);
}

void wall_time(UpdateJson &json) {
    const time_t now = time(nullptr);
    if (now < 1700000000) { json.raw("null"); return; }
    tm utc{};
    char value[32];
    gmtime_r(&now, &utc);
    strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%S+00:00", &utc);
    json.str(value);
}

const UpdateAssociation *association_for(const UpdateControllerSnapshot &snapshot, uint16_t tag) {
    for (size_t i = 0; i < snapshot.association_count; ++i)
        if (snapshot.associations[i].target.tag == tag) return &snapshot.associations[i];
    return nullptr;
}

size_t latest_job_for(const UpdateControllerSnapshot &snapshot, uint16_t tag) {
    size_t found = UPDATE_QUEUE_MAX;
    for (size_t i = 0; i < snapshot.job_count; ++i)
        if (snapshot.jobs[i].target.tag == tag &&
            (found == UPDATE_QUEUE_MAX || snapshot.jobs[i].id > snapshot.jobs[found].id))
            found = i;
    return found;
}

const char *updater_state(const UpdateControllerSnapshot &snapshot, size_t active) {
    if (snapshot.disabled) return "disabled";
    if (pairing_active(snapshot.pairing.phase)) return "pairing";
    if (active != UPDATE_QUEUE_MAX) return update_phase_name(snapshot.jobs[active].phase);
    return "idle";
}

bool tag_state(uint16_t tag, const TagRecord &record,
               const UpdateControllerSnapshot &snapshot, uint64_t now) {
    const size_t latest = latest_job_for(snapshot, tag);
    const bool pairing = snapshot.pairing.phase != UpdatePairingPhase::Idle &&
        snapshot.pairing.association.target.tag == tag;
    UpdateJson json(payload, sizeof(payload));
    json.raw("{\"tag\":");
    char id[5];
    std::snprintf(id, sizeof(id), "%04x", tag);
    json.str(id);
    json.raw(",\"name\":"); json.str(record.name);
    json.raw(",\"paired\":"); json.boolean(association_for(snapshot, tag) != nullptr);
    json.raw(",\"pairing\":"); json.str(pairing ? update_pairing_name(snapshot.pairing.phase) : "idle");
    json.raw(",\"pairing_error\":");
    if (pairing && snapshot.pairing.error[0]) json.str(snapshot.pairing.error); else json.raw("null");
    json.raw(",\"maintenance\":");
    json.boolean((pairing && pairing_active(snapshot.pairing.phase)) ||
                 (latest != UPDATE_QUEUE_MAX && snapshot.active_job_id == snapshot.jobs[latest].id));
    json.raw(",\"job\":");
    if (latest == UPDATE_QUEUE_MAX) json.raw("null");
    else update_job_json(json, snapshot, latest, record.name, now);
    json.raw(",\"age_ms\":");
    if (latest != UPDATE_QUEUE_MAX && snapshot.changed_ms[latest] && now >= snapshot.changed_ms[latest])
        json.number(now - snapshot.changed_ms[latest]);
    else json.raw("null");
    json.raw(",\"restart_count\":"); json.number(tag_updater_restart_count());
    json.raw(",\"uptime_ms\":"); json.number(now);
    json.raw(",\"wall_time\":"); wall_time(json);
    json.raw("}");
    const size_t length = json.finish();
    char topic[96];
    update_topic(topic, sizeof(topic), id);
    return length && mqtt_publish(topic, payload, true);
}

bool anchor_state(const UpdateControllerSnapshot &snapshot, uint64_t now) {
    size_t active = UPDATE_QUEUE_MAX;
    for (size_t i = 0; i < snapshot.job_count; ++i)
        if (snapshot.jobs[i].id == snapshot.active_job_id) { active = i; break; }
    const UpdateRelease *release = tag_updater_release();
    UpdateJson json(payload, sizeof(payload));
    json.raw("{\"state\":"); json.str(updater_state(snapshot, active));
    json.raw(",\"error\":");
    if (snapshot.error[0]) json.str(snapshot.error); else json.raw("null");
    json.raw(",\"pairing\":{\"phase\":"); json.str(update_pairing_name(snapshot.pairing.phase));
    json.raw(",\"error\":");
    if (snapshot.pairing.error[0]) json.str(snapshot.pairing.error); else json.raw("null");
    json.raw("},\"active_job\":");
    if (active == UPDATE_QUEUE_MAX) json.raw("null");
    else {
        const TagRecord *tag = publisher_registry().find(snapshot.jobs[active].target.tag);
        update_job_json(json, snapshot, active, tag ? tag->name : "", now);
    }
    json.raw(",\"release\":");
    if (!release) json.raw("null");
    else {
        json.raw("{\"version\":"); json.str(release->version);
        json.raw(",\"size\":"); json.number(release->size);
        json.raw(",\"file_sha256\":"); json.hex(release->file_sha, 32);
        json.raw(",\"image_hash\":"); json.hex(release->image_hash, 32);
        json.raw("}");
    }
    json.raw(",\"restart_count\":"); json.number(tag_updater_restart_count());
    json.raw(",\"uptime_ms\":"); json.number(now);
    json.raw(",\"wall_time\":"); wall_time(json);
    json.raw("}");
    const size_t length = json.finish();
    char topic[64];
    anchor_update_topic(topic, sizeof(topic));
    return length && mqtt_publish(topic, payload, true);
}

bool discovery_payload(char *out, size_t capacity, const DiagnosticSpec &spec,
                       const char *object_id, const char *state_topic,
                       const char *device_id, const char *device_name) {
    char availability[64];
    topic_anchor_status(availability, sizeof(availability), kAnchor);
    UpdateJson json(out, capacity);
    json.raw("{\"name\":"); json.str(spec.name);
    json.raw(",\"uniq_id\":"); json.str(object_id);
    json.raw(",\"stat_t\":"); json.str(state_topic);
    json.raw(",\"val_tpl\":"); json.str(spec.template_value);
    json.raw(",\"avty_t\":"); json.str(availability);
    json.raw(",\"ent_cat\":\"diagnostic\",\"dev\":{\"ids\":["); json.str(device_id);
    json.raw("],\"name\":"); json.str(device_name);
    // These entities join existing devices; do not overwrite their hardware or
    // firmware metadata with that of the publisher.
    json.raw("},\"o\":{\"name\":\"binrange-anchor-a\",\"sw\":"); json.str(BINRANGE_FW_VERSION);
    json.raw("}}");
    return json.finish() != 0;
}

bool publish_tag_discovery(const TagRecord &record, uint8_t step) {
    if (step >= sizeof(kTagDiagnostics) / sizeof(kTagDiagnostics[0])) return true;
    char tag[5], object_id[96], state[96], device_id[32], topic[160];
    tag_id_to_hex(tag, sizeof(tag), record.addr);
    const DiagnosticSpec &spec = kTagDiagnostics[step];
    std::snprintf(object_id, sizeof(object_id), "binrange_%s_a_update_%s", tag, spec.key);
    std::snprintf(device_id, sizeof(device_id), "binrange_tag_%s", tag);
    update_topic(state, sizeof(state), tag);
    topic_discovery(topic, sizeof(topic), spec.component, object_id);
    return discovery_payload(payload, sizeof(payload), spec, object_id, state, device_id, record.name) &&
        mqtt_publish(topic, payload, true);
}

bool publish_anchor_discovery(uint8_t step) {
    if (step >= sizeof(kAnchorDiagnostics) / sizeof(kAnchorDiagnostics[0])) return true;
    const DiagnosticSpec &spec = kAnchorDiagnostics[step];
    char object_id[96], state[64], topic[160];
    std::snprintf(object_id, sizeof(object_id), "binrange_anchor_a_update_%s", spec.key);
    anchor_update_topic(state, sizeof(state));
    topic_discovery(topic, sizeof(topic), spec.component, object_id);
    return discovery_payload(payload, sizeof(payload), spec, object_id, state,
                             "binrange_anchor_a", "BinRange Anchor a") &&
        mqtt_publish(topic, payload, true);
}

bool deletion_packet(PendingDeletion &deletion) {
    char tag[5], object_id[96], topic[160];
    tag_id_to_hex(tag, sizeof(tag), deletion.tag);
    const size_t discovery_count = sizeof(kTagDiagnostics) / sizeof(kTagDiagnostics[0]);
    if (deletion.step < discovery_count) {
        const DiagnosticSpec &spec = kTagDiagnostics[deletion.step];
        std::snprintf(object_id, sizeof(object_id), "binrange_%s_a_update_%s", tag, spec.key);
        topic_discovery(topic, sizeof(topic), spec.component, object_id);
    } else {
        update_topic(topic, sizeof(topic), tag);
    }
    return mqtt_publish(topic, "", true);
}

void refresh_discovery_slots() {
    Registry &registry = publisher_registry();
    for (size_t i = 0; i < registry.size(); ++i) {
        const TagRecord *tag = registry.at(i);
        if (!tag || !tag->adopted) continue;
        bool present = false;
        for (const auto &slot : discovery) if (slot.tag == tag->addr) { present = true; break; }
        if (present) continue;
        for (auto &slot : discovery) {
            if (!slot.tag) { slot.tag = tag->addr; slot.step = 0; break; }
        }
    }
}

bool publish_one_discovery() {
    if (anchor_discovery_step < sizeof(kAnchorDiagnostics) / sizeof(kAnchorDiagnostics[0])) {
        if (!publish_anchor_discovery(anchor_discovery_step)) return true;
        ++anchor_discovery_step;
        return true;
    }
    Registry &registry = publisher_registry();
    for (auto &slot : discovery) {
        if (!slot.tag || slot.step >= sizeof(kTagDiagnostics) / sizeof(kTagDiagnostics[0])) continue;
        const TagRecord *tag = registry.find(slot.tag);
        if (!tag || !tag->adopted) { slot = {}; continue; }
        if (!publish_tag_discovery(*tag, slot.step)) return true;
        ++slot.step;
        return true;
    }
    return false;
}

bool publish_one_state(const UpdateControllerSnapshot &snapshot, uint64_t now) {
    Registry &registry = publisher_registry();
    const size_t count = registry.size() + 1;
    if (state_cursor >= count) { state_cycle = false; return true; }
    bool published;
    if (state_cursor == 0) published = anchor_state(snapshot, now);
    else {
        const TagRecord *tag = registry.at(state_cursor - 1);
        published = !tag || !tag->adopted || tag_state(tag->addr, *tag, snapshot, now);
    }
    if (!published) return false;
    ++state_cursor;
    if (state_cursor >= count) state_cycle = false;
    return true;
}

}  // namespace

void update_mqtt_forget(uint16_t tag) {
    for (auto &slot : discovery)
        if (slot.tag == tag) slot = {};
    for (auto &deletion : deletions)
        if (deletion.tag == tag) return;
    for (auto &deletion : deletions) {
        if (!deletion.tag) { deletion = {tag, 0}; return; }
    }
}

void update_mqtt_loop() {
    if (!mqtt_connected()) { was_connected = false; return; }
    const uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    if (!was_connected) {
        was_connected = true;
        anchor_discovery_step = 0;
        for (auto &slot : discovery) if (slot.tag) slot.step = 0;
        state_cursor = 0;
        state_cycle = true;
    }

    for (auto &deletion : deletions) {
        if (!deletion.tag) continue;
        if (!deletion_packet(deletion)) return;
        ++deletion.step;
        if (deletion.step > sizeof(kTagDiagnostics) / sizeof(kTagDiagnostics[0])) deletion = {};
        return;
    }

    refresh_discovery_slots();
    if (publish_one_discovery()) return;

    const UpdateControllerSnapshot &snapshot = tag_updater_snapshot();
    // Publish completion/failure promptly, even though it switches to the
    // slower idle heartbeat. Active transfer progress remains rate-limited.
    if (snapshot.active_job_id != last_active_job || snapshot.job_count != last_job_count ||
        snapshot.pairing.phase != last_pairing || snapshot.disabled != last_disabled) {
        last_active_job = snapshot.active_job_id;
        last_job_count = snapshot.job_count;
        last_pairing = snapshot.pairing.phase;
        last_disabled = snapshot.disabled;
        state_cursor = 0;
        state_cycle = true;
    }
    const bool active = snapshot.active_job_id != 0 ||
        pairing_active(snapshot.pairing.phase);
    const uint32_t interval = active ? kActiveIntervalMs : kIdleIntervalMs;
    if (!state_cycle && uint32_t(now) - last_cycle_ms >= interval) {
        state_cursor = 0;
        state_cycle = true;
    }
    if (!state_cycle) return;
    if (!publish_one_state(snapshot, now)) return;
    if (!state_cycle) last_cycle_ms = uint32_t(now);
}
