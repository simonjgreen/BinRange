#pragma once
#include <Arduino.h>
#include "core/registry.h"

void publisher_begin();
void publisher_loop();
void publisher_on_message(const char *topic, const char *payload);

Registry &publisher_registry();
uint16_t publisher_last_unknown();
uint32_t publisher_unknown_count();
