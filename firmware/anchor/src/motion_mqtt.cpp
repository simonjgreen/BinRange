#include "motion_mqtt.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <esp_timer.h>
#include "core/topics.h"
#include "core/update_status.h"
#include "mqttlink.h"
#include "publisher.h"
#include "tag_updater.h"

namespace {
constexpr size_t kMotionEntities = 5;
constexpr uint64_t kRefreshMs = 15 * 60 * 1000;
struct MotionSlot {
    uint16_t tag = 0;
    uint8_t discovery = 0;
    bool initial_requested = false, sent = false, enabled = false;
    uint64_t changed = 0, last_publish = 0, last_refresh = 0;
    bool known = false, queued = false;
    MotionPhase phase = MotionPhase::Unknown;
    char command_error[80]{};
};
struct MotionDeletion { uint16_t tag = 0; uint8_t step = 0; };
std::array<MotionSlot, REGISTRY_MAX_TAGS> motion_slots{};
std::array<MotionDeletion, REGISTRY_MAX_TAGS> motion_deletions{};
bool motion_connected = false;
size_t motion_cursor = 0;
char motion_payload[1500];
const char *const keys[] = {"idle_interval", "moving_interval", "refresh_intervals", "interval_status", "interval_error"};
const char *const components[] = {"number", "number", "button", "sensor", "sensor"};
const char *const names[] = {"Stationary check-in interval", "Moving check-in interval", "Read check-in intervals", "Check-in settings status", "Check-in settings error"};
const char *phase_name(MotionPhase phase) {
    switch (phase) {
    case MotionPhase::Unknown: return "unknown";
    case MotionPhase::Queued: return "queued";
    case MotionPhase::Reading: return "reading";
    case MotionPhase::Writing: return "writing";
    case MotionPhase::Verifying: return "verifying";
    case MotionPhase::Applied: return "applied";
    case MotionPhase::Failed: return "failed";
    }
    return "unknown";
}
void motion_topic(char *out, size_t size, uint16_t tag) {
    std::snprintf(out, size, BINRANGE_TOPIC_BASE "/tag/%04x/anchor/a/motion", tag);
}
void motion_discovery_topic(char *out, size_t size, uint16_t tag, size_t step) {
    char object[80];
    std::snprintf(object, sizeof(object), "binrange_%04x_a_%s", tag, keys[step]);
    topic_discovery(out, size, components[step], object);
}
bool paired(const UpdateControllerSnapshot &s, uint16_t tag) {
    for (size_t i = 0; i < s.association_count; ++i) if (s.associations[i].target.tag == tag) return true;
    return false;
}
const MotionSettings *settings_for(const UpdateControllerSnapshot &s, uint16_t tag) {
    for (const auto &m : s.motion) if (m.tag == tag) return &m;
    return nullptr;
}
MotionSlot *motion_slot(uint16_t tag) {
    for (auto &s : motion_slots) if (s.tag == tag) return &s;
    for (auto &s : motion_slots) if (!s.tag) { s = {}; s.tag = tag; return &s; }
    return nullptr;
}
bool deleting(uint16_t tag) {
    for (const auto &d : motion_deletions) if (d.tag == tag) return true;
    return false;
}
bool publish_discovery(const TagRecord &tag, size_t step) {
    char state[96], topic[160], object[80], device[32], command[128];
    motion_topic(state, sizeof(state), tag.addr);
    motion_discovery_topic(topic, sizeof(topic), tag.addr, step);
    std::snprintf(object, sizeof(object), "binrange_%04x_a_%s", tag.addr, keys[step]);
    std::snprintf(device, sizeof(device), "binrange_tag_%04x", tag.addr);
    UpdateJson j(motion_payload, sizeof(motion_payload));
    j.raw("{\"name\":"); j.str(names[step]);
    j.raw(",\"unique_id\":"); j.str(object);
    j.raw(",\"device\":{\"identifiers\":["); j.str(device); j.raw("],\"name\":"); j.str(tag.name); j.raw("}");
    j.raw(",\"entity_category\":"); j.str(step < 3 ? "config" : "diagnostic");
    if (step < 3) {
        std::snprintf(command, sizeof(command), "%s/%s/set", state, step == 0 ? "idle" : step == 1 ? "moving" : "refresh");
        j.raw(",\"command_topic\":"); j.str(command);
    }
    if (step != 2) {
        j.raw(",\"state_topic\":"); j.str(state);
        j.raw(",\"value_template\":");
        j.str(step == 0 ? "{{ value_json.idle_ms / 1000 if value_json.idle_ms is not none else 'None' }}" :
              step == 1 ? "{{ value_json.moving_ms / 1000 if value_json.moving_ms is not none else 'None' }}" :
              step == 3 ? "{{ value_json.status }}" : "{{ value_json.error if value_json.error else 'none' }}");
    }
    if (step < 2) {
        j.raw(step == 0 ? ",\"min\":60,\"max\":3600" : ",\"min\":1,\"max\":60");
        j.raw(",\"step\":1,\"mode\":\"box\",\"unit_of_measurement\":\"s\",\"optimistic\":false,\"retain\":false");
    } else if (step == 2) j.raw(",\"payload_press\":\"PRESS\",\"retain\":false");
    j.raw(",\"availability_mode\":\"all\",\"availability\":[{\"topic\":\"binrange/anchor/a/status\"}");
    if (step < 3) {
        j.raw(",{\"topic\":"); j.str(state);
        j.raw(",\"value_template\":\"{{ 'online' if value_json.available else 'offline' }}\"}");
    }
    j.raw("]}");
    return j.finish() && mqtt_publish(topic, motion_payload, true);
}
bool publish_state(MotionSlot &slot, const MotionSettings *m, bool enabled, uint64_t now) {
    char topic[96]; motion_topic(topic, sizeof(topic), slot.tag);
    const bool known = m && m->known;
    UpdateJson j(motion_payload, sizeof(motion_payload));
    j.raw("{\"available\":"); j.boolean(enabled);
    j.raw(",\"idle_ms\":"); if (known) j.number(m->config.idle_ms); else j.raw("null");
    j.raw(",\"moving_ms\":"); if (known) j.number(m->config.moving_ms); else j.raw("null");
    j.raw(",\"status\":"); j.str(slot.command_error[0] ? "failed" : m ? phase_name(m->phase) : "unknown");
    j.raw(",\"pending\":"); j.boolean(m && (m->queued || m->phase == MotionPhase::Reading ||
        m->phase == MotionPhase::Writing || m->phase == MotionPhase::Verifying));
    j.raw(",\"error\":");
    const char *error = slot.command_error[0] ? slot.command_error : m ? m->error : "";
    if (error[0]) j.str(error); else j.raw("null");
    j.raw(",\"observed_age_ms\":"); if (known && now >= m->seen_ms) j.number(now - m->seen_ms); else j.raw("null");
    j.raw("}");
    if (!j.finish() || !mqtt_publish(topic, motion_payload, true)) return false;
    slot.sent = true; slot.enabled = enabled; slot.changed = m ? m->changed_ms : 0;
    slot.known = known; slot.queued = m && m->queued;
    slot.phase = m ? m->phase : MotionPhase::Unknown; slot.last_publish = now;
    return true;
}
// Whole seconds; accept HA's decimal representation (e.g. "5.0"), no truncation.
bool seconds_ms(const char *text, bool idle, uint32_t &ms) {
    if (!text || !*text) return false;
    uint32_t value = 0; size_t n = 0;
    while (text[n] >= '0' && text[n] <= '9') {
        if (n >= 4) return false;
        value = value * 10 + text[n++] - '0';
    }
    if (!n) return false;
    if (text[n] == '.') {
        ++n; size_t digits = 0;
        while (text[n] == '0' && digits < 6) { ++n; ++digits; }
        if (!digits) return false;
    }
    if (text[n] || value < (idle ? 60u : 1u) || value > (idle ? 3600u : 60u)) return false;
    ms = value * 1000; return true;
}
}

bool motion_mqtt_message(const char *topic, const char *payload) {
    // Exact routing using the existing four-hex tag parser.
    constexpr char prefix[] = BINRANGE_TOPIC_BASE "/tag/";
    const size_t pre = sizeof(prefix) - 1;
    if (!topic || std::strncmp(topic, prefix, pre) || std::strlen(topic) < pre + 4) return false;
    char config_topic[48];
    std::snprintf(config_topic, sizeof(config_topic), BINRANGE_TOPIC_BASE "/tag/%.4s/config", topic + pre);
    uint16_t tag;
    if (!parse_tag_config_topic(config_topic, &tag)) return false;
    const char *suffix = topic + pre + 4;
    const bool idle = !std::strcmp(suffix, "/anchor/a/motion/idle/set");
    const bool moving = !std::strcmp(suffix, "/anchor/a/motion/moving/set");
    const bool refresh = !std::strcmp(suffix, "/anchor/a/motion/refresh/set");
    if (!idle && !moving && !refresh) return false;
    const auto *record = publisher_registry().find(tag);
    if (!record || !record->adopted || deleting(tag)) return true;
    MotionSlot *slot = motion_slot(tag);
    if (!slot) return true;
    uint32_t ms = 0;
    const char *error = nullptr;
    if ((refresh && (!payload || std::strcmp(payload, "PRESS"))) || (!refresh && !seconds_ms(payload, idle, ms)))
        error = "Invalid interval; use whole seconds within the displayed limits";
    else if (!paired(tag_updater_snapshot(), tag)) error = "Pair this tag with the anchor first";
    else {
        auto result = tag_updater_motion(tag, idle ? ms : 0, moving ? ms : 0);
        if (result != UpdateQueueResult::Accepted) error = result == UpdateQueueResult::Conflict
            ? "Anchor busy staging firmware; retry after staging" : "Tag settings unavailable; check pairing and Bluetooth";
    }
    std::snprintf(slot->command_error, sizeof(slot->command_error), "%s", error ? error : "");
    slot->sent = false;
    return true;
}

void motion_mqtt_forget(uint16_t tag) {
    tag_updater_motion_forget(tag);
    for (auto &s : motion_slots) if (s.tag == tag) s = {};
    if (deleting(tag)) return;
    for (auto &d : motion_deletions) if (!d.tag) { d.tag = tag; d.step = 0; return; }
}

void motion_mqtt_loop() {
    if (!mqtt_connected()) { motion_connected = false; return; }
    if (!motion_connected) {
        for (auto &s : motion_slots) { s.discovery = 0; s.sent = false; }
        motion_connected = true;
    }
    // One packet per loop: preserve radio/network responsiveness and retry failures.
    for (auto &d : motion_deletions) if (d.tag) {
        char topic[160];
        if (d.step < kMotionEntities) motion_discovery_topic(topic, sizeof(topic), d.tag, d.step);
        else motion_topic(topic, sizeof(topic), d.tag);
        if (mqtt_publish(topic, "", true) && ++d.step > kMotionEntities) d = {};
        return;
    }
    const auto &snapshot = tag_updater_snapshot();
    auto &registry = publisher_registry();
    const uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    for (size_t count = 0; count < registry.size(); ++count) {
        const TagRecord *record = registry.at(motion_cursor++ % registry.size());
        if (!record || !record->adopted) continue;
        auto *slot = motion_slot(record->addr);
        if (!slot) continue;
        const MotionSettings *m = settings_for(snapshot, record->addr);
        bool enabled = !snapshot.disabled && paired(snapshot, record->addr);
        if (enabled && (!slot->initial_requested || (m && (m->phase == MotionPhase::Applied || m->phase == MotionPhase::Failed) && !m->queued && now - slot->last_refresh >= kRefreshMs))) {
            if (tag_updater_motion(record->addr, 0, 0) == UpdateQueueResult::Accepted) {
                slot->initial_requested = true; slot->last_refresh = now;
            }
        }
        if (slot->discovery < kMotionEntities) {
            if (publish_discovery(*record, slot->discovery)) ++slot->discovery;
            return;
        }
        if (!slot->sent || slot->enabled != enabled || slot->changed != (m ? m->changed_ms : 0) ||
            slot->known != (m && m->known) || slot->queued != (m && m->queued) ||
            slot->phase != (m ? m->phase : MotionPhase::Unknown) || now - slot->last_publish >= 30000) {
            publish_state(*slot, m, enabled, now); return;
        }
    }
}
