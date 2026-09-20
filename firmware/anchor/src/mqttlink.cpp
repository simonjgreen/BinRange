#include "mqttlink.h"
#include "mqttcfg.h"
#include "config.h"
#include "core/topics.h"
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient net;
static PubSubClient client(net);
static BrokerCfg cfg;
static MqttActivity act_;
static void (*user_cb)(const char *, const char *) = nullptr;
static char status_topic[96];
static char broker_desc[96];

// Reconnect attempts are rate limited and backed off. A connect to an
// unreachable host blocks the calling task for the socket timeout, and this
// runs on core 0 alongside the web server and OTA — so an unreachable broker
// would otherwise make the local diagnostic UI unusable precisely when it is
// needed. Bound the stall, and back off so the gaps stay short.
#define MQTT_RETRY_MIN_MS 5000
#define MQTT_RETRY_MAX_MS 60000
#define MQTT_SOCKET_TIMEOUT_S 2
#define MQTT_PROBE_TIMEOUT_MS 1200
static uint32_t next_attempt_ms;
static uint32_t retry_ms = MQTT_RETRY_MIN_MS;

struct LogEntry { char text[96]; uint32_t at_ms; };
static LogEntry log_[MQTT_LOG_LEN];
static size_t log_head_, log_count_;

static void log_line(const char *topic, const char *payload) {
    LogEntry &e = log_[log_head_];
    snprintf(e.text, sizeof(e.text), "%s  %.40s", topic, payload ? payload : "");
    e.at_ms = millis();
    log_head_ = (log_head_ + 1) % MQTT_LOG_LEN;
    if (log_count_ < MQTT_LOG_LEN) log_count_++;
}

static void on_message(char *topic, byte *payload, unsigned int len) {
    // PubSubClient hands us a non-terminated buffer.
    static char buf[512];
    // Never execute a truncated command or accept a NUL-hidden suffix.
    if (len >= sizeof(buf) || memchr(payload, 0, len)) return;
    unsigned int n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    log_line(topic, buf);
    if (user_cb) user_cb(topic, buf);
}

void mqtt_begin() {
    mqttcfg_load(&cfg);
    topic_anchor_status(status_topic, sizeof(status_topic), ANCHOR_ID);
    snprintf(broker_desc, sizeof(broker_desc), "%s:%u", cfg.host, cfg.port);
    client.setServer(cfg.host, cfg.port);
    client.setCallback(on_message);
    // The default 256 byte buffer silently drops discovery payloads.
    client.setBufferSize(2048);
    // Caps how long a failed connect can block core 0.
    client.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
    next_attempt_ms = 0;
    retry_ms = MQTT_RETRY_MIN_MS;
}

void mqtt_set_callback(void (*cb)(const char *, const char *)) { user_cb = cb; }
bool mqtt_connected() { return client.connected(); }
const char *mqtt_broker_desc() { return broker_desc; }

void mqtt_subscribe(const char *topic) {
    if (client.connected()) client.subscribe(topic);
}

static void on_connect() {
    act_.reconnects++;
    act_.last_rc = 0;      // clear a stale failure, or the UI shows an error
                           // while plainly connected
    client.publish(status_topic, "online", true);
    char t[96];
    topic_tag_config_wildcard(t, sizeof(t));
    client.subscribe(t);
    topic_anchor_cmd_wildcard(t, sizeof(t), ANCHOR_ID);
    client.subscribe(t);
    snprintf(t, sizeof(t), BINRANGE_TOPIC_BASE "/tag/+/anchor/%s/motion/+/set", ANCHOR_ID);
    client.subscribe(t);
    Serial.printf("[mqtt] connected to %s\n", broker_desc);
}

void mqtt_loop() {
    if (client.connected()) {
        client.loop();
        act_.connected = true;
        return;
    }
    act_.connected = false;
    if (WiFi.status() != WL_CONNECTED) return;

    uint32_t now = millis();
    if ((int32_t)(now - next_attempt_ms) < 0) return;   // not due yet
    next_attempt_ms = now + retry_ms;

    // PubSubClient calls the two-argument connect(), whose TCP timeout we
    // cannot set, so an unreachable host blocks core 0 for many seconds and
    // takes the web server and OTA down with it. Probe first with an explicit
    // short timeout and only proceed when the host actually answers.
    {
        WiFiClient probe;
        if (!probe.connect(cfg.host, cfg.port, MQTT_PROBE_TIMEOUT_MS)) {
            probe.stop();
            act_.last_rc = -2;   // connect failed (network)
            retry_ms = retry_ms * 2 > MQTT_RETRY_MAX_MS ? MQTT_RETRY_MAX_MS
                                                        : retry_ms * 2;
            Serial.printf("[mqtt] %s unreachable, retrying in %us\n",
                          broker_desc, (unsigned)(retry_ms / 1000));
            return;
        }
        probe.stop();
    }

    // Last will: the broker announces our death if we drop off.
    bool ok = client.connect(cfg.client_id, cfg.user, cfg.pass, status_topic, 0,
                             true, "offline");
    if (ok) {
        retry_ms = MQTT_RETRY_MIN_MS;
        on_connect();
    } else {
        retry_ms = retry_ms * 2 > MQTT_RETRY_MAX_MS ? MQTT_RETRY_MAX_MS
                                                    : retry_ms * 2;
        act_.last_rc = client.state();
        Serial.printf("[mqtt] connect failed: %s, retrying in %us\n",
                      mqtt_state_text(act_.last_rc), (unsigned)(retry_ms / 1000));
    }
}

bool mqtt_publish(const char *topic, const char *payload, bool retain) {
    bool ok = client.publish(topic, payload, retain);
    if (ok) {
        act_.published++;
        act_.last_pub_ms = millis();
    } else {
        act_.failed++;
        act_.last_rc = client.state();
    }
    log_line(topic, payload);
    return ok;
}

void mqtt_reconnect_now() {
    client.disconnect();
    mqtt_begin();          // re-read settings; they may have changed
    next_attempt_ms = 0;   // retry immediately
    retry_ms = MQTT_RETRY_MIN_MS;
}

void mqtt_activity(MqttActivity *out) {
    *out = act_;
    out->connected = client.connected();
}

size_t mqtt_log_size() { return log_count_; }

const char *mqtt_log_at(size_t i, uint32_t *age_ms) {
    if (i >= log_count_) return nullptr;
    size_t start = (log_count_ == MQTT_LOG_LEN) ? log_head_ : 0;
    const LogEntry &e = log_[(start + i) % MQTT_LOG_LEN];
    if (age_ms) *age_ms = millis() - e.at_ms;
    return e.text;
}

const char *mqtt_state_text(int rc) {
    switch (rc) {
        case -4: return "timed out";
        case -3: return "connection lost";
        case -2: return "connect failed (network)";
        case -1: return "disconnected";
        case 0:  return "connected";
        case 1:  return "bad protocol version";
        case 2:  return "client id rejected";
        case 3:  return "server unavailable";
        case 4:  return "bad credentials";
        case 5:  return "not authorised";
        default: return "unknown";
    }
}
