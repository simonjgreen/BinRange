#pragma once

#include <cstdint>

// Main-loop-only, status-only MQTT/HA diagnostics for the tag updater.
void update_mqtt_loop();

// Called before publisher_registry().forget(tag). Retained updater discovery
// and status are deleted asynchronously so broker outages do not lose cleanup.
void update_mqtt_forget(uint16_t tag);
