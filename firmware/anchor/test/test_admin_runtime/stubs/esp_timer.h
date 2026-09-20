#pragma once
#include <cstdint>
namespace fake_sdk { inline int64_t uptime_us = 1000; }
inline int64_t esp_timer_get_time() { return fake_sdk::uptime_us; }
