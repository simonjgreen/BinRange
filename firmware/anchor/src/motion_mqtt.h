#pragma once
#include <cstdint>

void motion_mqtt_loop();
bool motion_mqtt_message(const char *topic, const char *payload);
void motion_mqtt_forget(uint16_t tag);
