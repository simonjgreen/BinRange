#pragma once
#include <Arduino.h>

#define MQTT_LOG_LEN 20

struct MqttActivity {
    uint32_t published, failed, reconnects;
    int last_rc;
    uint32_t last_pub_ms;
    bool connected;
};

void mqtt_begin();
void mqtt_loop();
bool mqtt_connected();
bool mqtt_publish(const char *topic, const char *payload, bool retain);
void mqtt_set_callback(void (*cb)(const char *topic, const char *payload));
void mqtt_subscribe(const char *topic);
void mqtt_reconnect_now();

void mqtt_activity(MqttActivity *out);
size_t mqtt_log_size();
// Oldest first. Returns the text and sets age_ms.
const char *mqtt_log_at(size_t i, uint32_t *age_ms);
const char *mqtt_state_text(int rc);
const char *mqtt_broker_desc();
