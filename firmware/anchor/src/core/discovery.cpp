#include "core/discovery.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>

// Keys match the compact state payload published per tag:
//   {"d":..,"sd":..,"n":..,"ok":..,"rssi":..,"fp":..,"gap":..,"ts":..}
const EntitySpec TAG_ENTITIES[] = {
    {"sensor", "distance", "Distance", "m", "distance", "measurement",
     "{{ value_json.d }}", false},
    {"sensor", "spread", "Spread", "m", nullptr, "measurement",
     "{{ value_json.sd }}", true},
    {"sensor", "samples", "Samples", nullptr, nullptr, "measurement",
     "{{ value_json.n }}", true},
    {"sensor", "success", "Success rate", "%", nullptr, "measurement",
     "{{ value_json.ok }}", true},
    {"sensor", "misses", "Missed polls", nullptr, nullptr, "measurement",
     "{{ value_json.misses }}", true},
    {"binary_sensor", "moving", "Moving", nullptr, "moving", nullptr,
     "{{ 'ON' if value_json.moving is true else 'OFF' if value_json.moving is false else none }}", false},
    {"binary_sensor", "sensor_fault", "Motion sensor fault", nullptr, "problem", nullptr,
     "{{ 'ON' if value_json.sensor_fault is true else 'OFF' if value_json.sensor_fault is false else none }}", true},
    {"sensor", "wake_count", "Wake count", nullptr, nullptr, nullptr,
     "{{ value_json.wake_count }}", true},
    {"sensor", "rssi", "RSSI", "dBm", "signal_strength", "measurement",
     "{{ value_json.rssi }}", true},
    {"sensor", "firstpath", "First path", "dBm", "signal_strength",
     "measurement", "{{ value_json.fp }}", true},
    {"sensor", "nlos_gap", "NLOS gap", "dB", nullptr, "measurement",
     "{{ value_json.gap }}", true},
    {"sensor", "last_seen", "Last seen", nullptr, "timestamp", nullptr,
     "{{ value_json.ts }}", true},
    {"sensor", "battery", "Battery", "V", "voltage", "measurement",
     "{{ value_json.bat }}", true},
    // device_class problem: ON means there is a problem, so "not checked in".
    {"binary_sensor", "checkin", "Not checked in", nullptr, "problem", nullptr,
     "{{ 'ON' if value_json.missing is true else 'OFF' if value_json.missing is false else none }}", false},
    {"binary_sensor", "stale_range", "Range freshness", nullptr, "problem", nullptr,
     "{{ 'ON' if value_json.stale else 'OFF' }}", true},
};
const size_t TAG_ENTITY_COUNT = sizeof(TAG_ENTITIES) / sizeof(TAG_ENTITIES[0]);

void discovery_object_id(char *out, size_t n, const char *tag,
                         const char *anchor, const char *key) {
    snprintf(out, n, "binrange_%s_%s_%s", tag, anchor, key);
}

// Appends to out at *len, returning false if it would not fit.
static bool append(char *out, size_t n, size_t *len, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + *len, n - *len, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= n - *len) return false;
    *len += (size_t)w;
    return true;
}

size_t discovery_tag_entity(char *out, size_t n, const EntitySpec &e,
                            const char *tag, const char *anchor,
                            const char *tag_name, const char *area,
                            const char *fw) {
    char object_id[96], state_topic[96], avail_topic[96];
    discovery_object_id(object_id, sizeof(object_id), tag, anchor, e.key);
    topic_tag_state(state_topic, sizeof(state_topic), tag, anchor);
    topic_anchor_status(avail_topic, sizeof(avail_topic), anchor);

    size_t len = 0;
    if (!append(out, n, &len,
                "{\"name\":\"%s (%s)\",\"uniq_id\":\"%s\",\"stat_t\":\"%s\","
                "\"val_tpl\":\"%s\",\"avty_t\":\"%s\"",
                e.name, anchor, object_id, state_topic, e.value_template,
                avail_topic))
        return 0;

    // Optional fields are omitted entirely when absent.
    if (e.unit && !append(out, n, &len, ",\"unit_of_meas\":\"%s\"", e.unit))
        return 0;
    if (e.device_class &&
        !append(out, n, &len, ",\"dev_cla\":\"%s\"", e.device_class))
        return 0;
    if (e.state_class &&
        !append(out, n, &len, ",\"stat_cla\":\"%s\"", e.state_class))
        return 0;
    if (e.diagnostic && !append(out, n, &len, ",\"ent_cat\":\"diagnostic\""))
        return 0;

    // The device block is identical from every anchor, so Home Assistant
    // merges their entities onto one device.
    if (!append(out, n, &len,
                ",\"dev\":{\"ids\":[\"binrange_tag_%s\"],\"name\":\"%s\","
                "\"mf\":\"BinRange\",\"mdl\":\"UWB bin tag\"",
                tag, tag_name))
        return 0;
    if (area && area[0] && !append(out, n, &len, ",\"sa\":\"%s\"", area))
        return 0;
    if (!append(out, n, &len,
                "},\"o\":{\"name\":\"binrange-anchor-%s\",\"sw\":\"%s\"}}",
                anchor, fw))
        return 0;
    return len;
}

// The anchor's own device. Diagnostics read from its state topic; controls
// additionally carry a command topic.
const AnchorEntity ANCHOR_ENTITIES[] = {
    {"binary_sensor", "radio", "Radio fault", nullptr, "problem", nullptr,
     "{{ 'OFF' if value_json.radio else 'ON' }}", true, nullptr, nullptr},
    {"sensor", "exchanges", "Exchanges", nullptr, nullptr, "total_increasing",
     "{{ value_json.ok }}", true, nullptr, nullptr},
    {"sensor", "timeouts", "Timeouts", nullptr, nullptr, "total_increasing",
     "{{ value_json.timeout }}", true, nullptr, nullptr},
    {"sensor", "rx_errors", "RX errors", nullptr, nullptr, "total_increasing",
     "{{ value_json.rx_err }}", true, nullptr, nullptr},
    {"sensor", "bad_frames", "Bad frames", nullptr, nullptr, "total_increasing",
     "{{ value_json.bad }}", true, nullptr, nullptr},
    {"sensor", "queue_dropped", "Queue drops", nullptr, nullptr,
     "total_increasing", "{{ value_json.dropped }}", true, nullptr, nullptr},
    {"sensor", "tags_seen", "Tags seen", nullptr, nullptr, "measurement",
     "{{ value_json.tags }}", true, nullptr, nullptr},
    {"sensor", "tags_adopted", "Tags adopted", nullptr, nullptr, "measurement",
     "{{ value_json.adopted }}", true, nullptr, nullptr},
    {"sensor", "unknown_tags", "Unknown tags", nullptr, nullptr,
     "measurement", "{{ value_json.unknown }}", true, nullptr, nullptr},
    {"sensor", "last_unknown", "Last unknown tag", nullptr, nullptr, nullptr,
     "{{ value_json.last_unknown }}", true, nullptr, nullptr},
    {"sensor", "mqtt_published", "MQTT published", nullptr, nullptr,
     "total_increasing", "{{ value_json.pub }}", true, nullptr, nullptr},
    {"sensor", "mqtt_failed", "MQTT failures", nullptr, nullptr,
     "total_increasing", "{{ value_json.pubfail }}", true, nullptr, nullptr},
    {"sensor", "mqtt_reconnects", "MQTT reconnects", nullptr, nullptr,
     "total_increasing", "{{ value_json.reconnects }}", true, nullptr, nullptr},
    {"sensor", "heap", "Free heap", "B", nullptr, "measurement",
     "{{ value_json.heap }}", true, nullptr, nullptr},
    {"sensor", "wifi_rssi", "Wi-Fi signal", "dBm", "signal_strength",
     "measurement", "{{ value_json.rssi_wifi }}", true, nullptr, nullptr},
    {"sensor", "boot", "Started", nullptr, "timestamp", nullptr,
     "{{ value_json.boot }}", true, nullptr, nullptr},
    // Controls.
    {"number", "antenna_delay", "Antenna delay", nullptr, nullptr, nullptr,
     "{{ value_json.antdly }}", false, "antenna_delay", nullptr},
    {"select", "phy", "PHY profile", nullptr, nullptr, nullptr,
     "{{ value_json.phy }}", false, "phy", "\"Short\",\"Long\",\"Max\""},
    {"button", "reset_counters", "Reset counters", nullptr, nullptr, nullptr,
     nullptr, false, "reset_counters", nullptr},
    {"button", "restart_radio", "Restart radio", nullptr, nullptr, nullptr,
     nullptr, false, "restart_radio", nullptr},
};
const size_t ANCHOR_ENTITY_COUNT =
    sizeof(ANCHOR_ENTITIES) / sizeof(ANCHOR_ENTITIES[0]);

size_t discovery_anchor_entity(char *out, size_t n, const AnchorEntity &e,
                               const char *anchor, const char *fw) {
    char object_id[96], state_topic[96], avail_topic[96];
    snprintf(object_id, sizeof(object_id), "binrange_anchor_%s_%s", anchor, e.key);
    topic_anchor_state(state_topic, sizeof(state_topic), anchor);
    topic_anchor_status(avail_topic, sizeof(avail_topic), anchor);

    size_t len = 0;
    if (!append(out, n, &len, "{\"name\":\"%s\",\"uniq_id\":\"%s\",\"avty_t\":\"%s\"",
                e.name, object_id, avail_topic))
        return 0;

    // A button has nothing to report, only a command.
    if (e.value_template) {
        if (!append(out, n, &len, ",\"stat_t\":\"%s\",\"val_tpl\":\"%s\"",
                    state_topic, e.value_template))
            return 0;
    }
    if (e.cmd_key) {
        if (!append(out, n, &len, ",\"cmd_t\":\"" BINRANGE_TOPIC_BASE
                    "/anchor/%s/cmd/%s\"", anchor, e.cmd_key))
            return 0;
    }
    if (e.options && !append(out, n, &len, ",\"options\":[%s]", e.options))
        return 0;
    if (!strcmp(e.component, "number") &&
        !append(out, n, &len, ",\"min\":15800,\"max\":17000,\"step\":1,\"mode\":\"box\""))
        return 0;
    if (e.unit && !append(out, n, &len, ",\"unit_of_meas\":\"%s\"", e.unit))
        return 0;
    if (e.device_class &&
        !append(out, n, &len, ",\"dev_cla\":\"%s\"", e.device_class))
        return 0;
    if (e.state_class &&
        !append(out, n, &len, ",\"stat_cla\":\"%s\"", e.state_class))
        return 0;
    if (e.diagnostic && !append(out, n, &len, ",\"ent_cat\":\"diagnostic\""))
        return 0;
    if (!e.diagnostic && e.cmd_key &&
        !append(out, n, &len, ",\"ent_cat\":\"config\""))
        return 0;
    // Every anchor sensor is an integer - counters, bytes, seconds, dBm - so
    // Home Assistant should not render them as "163.00 s".
    if (!strcmp(e.component, "sensor") &&
        !append(out, n, &len, ",\"sug_dsp_prc\":0"))
        return 0;

    if (!append(out, n, &len,
                ",\"dev\":{\"ids\":[\"binrange_anchor_%s\"],"
                "\"name\":\"BinRange Anchor %s\",\"mf\":\"BinRange\","
                "\"mdl\":\"Makerfabs ESP32 UWB DW3000\",\"sw\":\"%s\"}",
                anchor, anchor, fw))
        return 0;
    if (!append(out, n, &len,
                ",\"o\":{\"name\":\"binrange-anchor-%s\",\"sw\":\"%s\"}}",
                anchor, fw))
        return 0;
    return len;
}

// Finds "key" and returns a pointer just past the following colon.
static const char *find_value(const char *json, const char *key) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return nullptr;
    p = strchr(p + strlen(pat), ':');
    if (!p) return nullptr;
    p++;
    while (*p == ' ') p++;
    return p;
}

bool json_str(const char *json, const char *key, char *out, size_t n) {
    const char *p = find_value(json, key);
    if (!p || *p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

bool json_float(const char *json, const char *key, float *out) {
    const char *p = find_value(json, key);
    if (!p) return false;
    char *end = nullptr;
    float v = strtof(p, &end);
    if (end == p) return false;
    *out = v;
    return true;
}
