#pragma once
#include <Arduino.h>

// Broker settings live in NVS so a board can be pointed at a different broker
// without a rebuild. secrets.h supplies first-boot defaults only.
struct BrokerCfg {
    char host[64];
    uint16_t port;
    char user[32];
    char pass[64];
    char client_id[32];
};

void mqttcfg_load(BrokerCfg *c);
void mqttcfg_save(const BrokerCfg *c);
// Whether a non-empty password is stored. The value itself is never exposed.
bool mqttcfg_has_password();
