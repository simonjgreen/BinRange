#include "publisher.h"
#include "config.h"
#include "ranging.h"
#include "stats.h"
#include "mqttlink.h"
#include "core/topics.h"
#include "core/discovery.h"
#include "core/freshness.h"
#include "core/tagpayload.h"
#include "core/version.h"
#include "update_mqtt.h"
#include <WiFi.h>
#include <math.h>
#include <time.h>

// ISO 8601 UTC, or empty if SNTP has not yet set the clock.
static void iso_epoch(char *out, size_t n, uint32_t epoch) {
    if (!epoch) { out[0] = '\0'; return; }
    time_t now = (time_t)epoch;
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(out, n, "%Y-%m-%dT%H:%M:%S+00:00", &tm);
}

static Registry reg;
static uint16_t last_unknown;
static uint32_t last_anchor_pub_ms;
#define ANCHOR_HEARTBEAT_MS 30000

Registry &publisher_registry() { return reg; }
uint16_t publisher_last_unknown() { return last_unknown; }
uint32_t publisher_unknown_count() { return reg.size() - reg.adopted_count(); }

// ---- Discovery ---------------------------------------------------------

static void publish_discovery(uint16_t addr) {
    TagRecord *t = reg.find(addr);
    if (!t || !t->adopted) return;

    char tag[8], object_id[96], topic[160];
    static char payload[1024];
    tag_id_to_hex(tag, sizeof(tag), addr);

    for (size_t i = 0; i < TAG_ENTITY_COUNT; i++) {
        const EntitySpec &e = TAG_ENTITIES[i];
        discovery_object_id(object_id, sizeof(object_id), tag, ANCHOR_ID, e.key);
        topic_discovery(topic, sizeof(topic), e.component, object_id);
        size_t len = discovery_tag_entity(payload, sizeof(payload), e, tag,
                                          ANCHOR_ID, t->name, t->area,
                                          BINRANGE_FW_VERSION);
        if (len) mqtt_publish(topic, payload, true);
    }
    t->discovery_sent = true;
    Serial.printf("[pub] discovery published for %s (%s)\n", tag, t->name);
}

// An empty retained payload is how Home Assistant is told to delete an entity.
static void remove_discovery(uint16_t addr) {
    update_mqtt_forget(addr);
    char tag[8], object_id[96], topic[160];
    tag_id_to_hex(tag, sizeof(tag), addr);
    for (size_t i = 0; i < TAG_ENTITY_COUNT; i++) {
        const EntitySpec &e = TAG_ENTITIES[i];
        discovery_object_id(object_id, sizeof(object_id), tag, ANCHOR_ID, e.key);
        topic_discovery(topic, sizeof(topic), e.component, object_id);
        mqtt_publish(topic, "", true);
    }
    Serial.printf("[pub] discovery removed for %s\n", tag);
}

// ---- State -------------------------------------------------------------

// Payload construction lives in core/tagpayload.cpp so it can be unit tested.
// Two malformed-JSON bugs reached hardware while this was inline.
static void publish_tag_state_raw(TagRecord *t) {
    TagSummary s;
    t->stats.summarise(&s);

    char tag[8], topic[96], ts[40];
    static char payload[384];
    tag_id_to_hex(tag, sizeof(tag), t->addr);
    topic_tag_state(topic, sizeof(topic), tag, ANCHOR_ID);
    iso_epoch(ts, sizeof(ts), t->last_seen_epoch);

    TagState st{&s, t->offset, ts, (millis() - t->last_seen_ms) / 1000,
                t->stale, t->motion_known, t->moving,
                t->sensor_fault_known, t->sensor_fault,
                t->misses_known, t->misses,
                t->wake_count_known, t->wake_count,
                t->batt_known, t->batt_mv,
                t->absence_known, t->absent, t->heard};
    if (tag_state_json(payload, sizeof(payload), st))
        mqtt_publish(topic, payload, true);
}

static void publish_tag_state(TagRecord *t) { publish_tag_state_raw(t); }

// A received event clears a prior absence latch, but can still already be
// stale by the time the radio queue is drained. All other callers retain the
// latch until a real event reaches this point.
static void update_tag_freshness(TagRecord *t, uint32_t now_ms,
                                 bool received_now) {
    TagFreshness f = tag_freshness(t->heard, t->motion_known, t->moving,
                                   t->last_seen_ms, t->adopted_ms, now_ms,
                                   t->stale_after_s,
                                   received_now ? false : t->absent);
    t->stale = f.range_stale;
    t->absence_known = f.absence_known;
    t->absent = f.absent;
}

static void publish_anchor_state() {
    char topic[96];
    static char payload[512];
    topic_anchor_state(topic, sizeof(topic), ANCHOR_ID);
    char unk[8];
    tag_id_to_hex(unk, sizeof(unk), last_unknown);

    MqttActivity a;
    mqtt_activity(&a);

    // Boot time as a timestamp reads better than a seconds counter: Home
    // Assistant renders it as "5 minutes ago" and keeps the exact time on hover.
    char boot_iso[40] = "";
    {
        time_t now = time(nullptr);
        if (now > 1700000000) {
            time_t boot = now - (time_t)(millis() / 1000);
            struct tm tm;
            gmtime_r(&boot, &tm);
            strftime(boot_iso, sizeof(boot_iso), "%Y-%m-%dT%H:%M:%S+00:00", &tm);
        }
    }
    snprintf(payload, sizeof(payload),
             "{\"radio\":%s,\"ok\":%u,\"timeout\":%u,\"rx_err\":%u,\"bad\":%u,"
             "\"tags\":%u,\"adopted\":%u,\"unknown\":%u,\"last_unknown\":\"%s\","
             "\"dropped\":%u,\"pub\":%u,\"pubfail\":%u,\"reconnects\":%u,"
             "\"heap\":%u,\"rssi_wifi\":%d,\"uptime\":%u,\"fw\":\"%s\","
             "\"antdly\":%u,\"phy\":\"%s\",\"boot\":\"%s\"}",
             ranging_radio_ok() ? "true" : "false",
             (unsigned)stats_counter_ok(), (unsigned)stats_counter_fail(FAIL_TIMEOUT),
             (unsigned)stats_counter_fail(FAIL_RX_ERROR),
             (unsigned)stats_counter_fail(FAIL_BAD_FRAME),
             (unsigned)reg.size(), (unsigned)reg.adopted_count(),
             (unsigned)(reg.size() - reg.adopted_count()),
             last_unknown ? unk : "-",
             (unsigned)ranging_dropped(), a.published, a.failed, a.reconnects,
             (unsigned)ESP.getFreeHeap(), (int)WiFi.RSSI(),
             (unsigned)(millis() / 1000), BINRANGE_FW_VERSION,
             (unsigned)ranging_antdly(),
             ranging_phy() == PHY_MAX ? "Max"
               : ranging_phy() == PHY_LONG ? "Long" : "Short",
             boot_iso);
    mqtt_publish(topic, payload, true);
    last_anchor_pub_ms = millis();
}

// ---- Incoming ----------------------------------------------------------

static void handle_command(const char *topic, const char *payload) {
    const char *slash = strrchr(topic, '/');
    if (!slash) return;
    const char *cmd = slash + 1;

    if (!strcmp(cmd, "antenna_delay")) {
        long v = atol(payload);
        if (v >= 0 && v <= 65535) ranging_set_antdly((uint16_t)v);
    } else if (!strcmp(cmd, "phy")) {
        if (!strcasecmp(payload, "short")) ranging_set_phy(PHY_SHORT);
        else if (!strcasecmp(payload, "long")) ranging_set_phy(PHY_LONG);
        else if (!strcasecmp(payload, "max")) ranging_set_phy(PHY_MAX);
    } else if (!strcmp(cmd, "reset_counters")) {
        stats_reset();
    } else if (!strcmp(cmd, "restart_radio")) {
        ranging_set_phy(ranging_phy());   // forces a reconfigure
    } else {
        return;
    }
    publish_anchor_state();
}

void publisher_on_message(const char *topic, const char *payload) {
    uint16_t addr;
    if (parse_tag_config_topic(topic, &addr)) {
        if (!payload || !payload[0]) {   // empty retained payload = forget
            remove_discovery(addr);
            reg.forget(addr);
            return;
        }
        char name[REGISTRY_NAME_LEN] = {0}, area[REGISTRY_NAME_LEN] = {0};
        float offset = 0.0f, stale_after = 0.0f;
        json_str(payload, "name", name, sizeof(name));
        json_str(payload, "area", area, sizeof(area));
        json_float(payload, "offset", &offset);
        json_float(payload, "stale_after", &stale_after);
        if (!name[0]) snprintf(name, sizeof(name), "Tag %04x", addr);
        if (reg.adopt(addr, name, area, offset, (uint32_t)stale_after, millis())) {
            if (last_unknown == addr) last_unknown = 0;   // no longer unknown
            publish_discovery(addr);
        }
        return;
    }
    handle_command(topic, payload);
}

// ---- Main loop ---------------------------------------------------------

static bool anchor_discovery_sent;

static void publish_anchor_discovery() {
    char topic[160];
    static char payload[1024];
    for (size_t i = 0; i < ANCHOR_ENTITY_COUNT; i++) {
        const AnchorEntity &e = ANCHOR_ENTITIES[i];
        char object_id[96];
        snprintf(object_id, sizeof(object_id), "binrange_anchor_%s_%s",
                 ANCHOR_ID, e.key);
        topic_discovery(topic, sizeof(topic), e.component, object_id);
        size_t len = discovery_anchor_entity(payload, sizeof(payload), e,
                                             ANCHOR_ID, BINRANGE_FW_VERSION);
        if (len) mqtt_publish(topic, payload, true);
    }
    anchor_discovery_sent = true;
    Serial.println("[pub] anchor device discovery published");
}

void publisher_begin() {
    mqtt_set_callback(publisher_on_message);
}

void publisher_loop() {
    RangeEvent ev;
    while (ranging_pop(&ev)) {
        const uint32_t drained_ms = millis();
        TagRecord *t = reg.touch(ev.addr, ev.at_ms);
        if (!t) continue;                      // registry full
        t->last_seen_epoch = reception_epoch((uint32_t)time(nullptr), drained_ms,
                                             ev.at_ms);
        if (!t->adopted) {
            // Reported, but never given entities: a neighbour's hardware must
            // not populate Home Assistant.
            if (last_unknown != ev.addr) {
                last_unknown = ev.addr;
                Serial.printf("[pub] unknown tag %04x seen\n", ev.addr);
                publish_anchor_state();     // surface it promptly
            }
            continue;
        }
        // Keep the completed burst after publication for truthful transition
        // republishing; replace it only when this tag's next burst starts.
        if (!t->pending) t->stats.reset();
        t->stats.add(ev.sample);
        t->batt_known = ev.has_battery && ev.batt_mv != 0;
        t->batt_mv = ev.batt_mv;
        t->sensor_fault_known = ev.has_telemetry;
        t->sensor_fault = ev.sensor_fault;
        t->motion_known = ev.has_telemetry && !ev.sensor_fault;
        t->moving = ev.moving;
        t->misses_known = ev.has_telemetry;
        t->misses = ev.misses;
        t->wake_count_known = ev.has_wake_count;
        t->wake_count = ev.wake_count;
        // A real reception clears the absence latch, while preserving its
        // actual age if queue delay has already crossed a threshold.
        update_tag_freshness(t, drained_ms, true);
        t->pending = true;
    }

    // A burst has ended when the tag has gone quiet briefly.
    uint32_t now = millis();
    for (size_t i = 0; i < reg.size(); i++) {
        TagRecord *t = reg.at(i);
        if (t->pending && now - t->last_seen_ms > BURST_IDLE_MS) {
            publish_tag_state(t);
            t->pending = false;
        }
    }

    // A silent tag has no new samples to publish. Re-emit only when range
    // freshness or long absence changes, never as a periodic synthetic report.
    for (size_t i = 0; i < reg.size(); i++) {
        TagRecord *t = reg.at(i);
        if (!t->adopted) continue;
        if (t->pending) continue; // publish the new timestamp with its burst summary
        const bool was_stale = t->stale;
        const bool was_absence_known = t->absence_known;
        const bool was_absent = t->absent;
        update_tag_freshness(t, now, false);
        if (t->stale == was_stale && t->absence_known == was_absence_known &&
            t->absent == was_absent) continue;
        Serial.printf("[pub] %04x range=%s absence=%s\n", t->addr,
                      t->stale ? "stale" : "fresh",
                      !t->absence_known ? "unknown" : t->absent ? "missing" : "present");
        publish_tag_state_raw(t);
    }

    if (mqtt_connected() && !anchor_discovery_sent) {
        publish_anchor_discovery();
        publish_anchor_state();
        // MQTT may have lost a retained threshold-transition state while the
        // link was down. Re-emit completed adopted states without changing
        // their saved reception timestamp. Pending bursts publish at flush.
        for (size_t i = 0; i < reg.size(); i++) {
            TagRecord *t = reg.at(i);
            if (!t->adopted || t->pending) continue;
            update_tag_freshness(t, now, false);
            publish_tag_state_raw(t);
        }
    }
    if (!mqtt_connected()) anchor_discovery_sent = false;   // republish on reconnect

    if (mqtt_connected() && now - last_anchor_pub_ms > ANCHOR_HEARTBEAT_MS)
        publish_anchor_state();
}
