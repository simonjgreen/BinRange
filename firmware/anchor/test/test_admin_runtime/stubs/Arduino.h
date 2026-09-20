#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Only SDK operations used by admin.cpp. No admin policy lives in these stubs.
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string &value) : std::string(value) {}
};

namespace fake_sdk {
inline uint32_t now = 1;
inline uint32_t random_word = 0;
inline unsigned restarts = 0;
inline bool http_mdns = true;
}
inline uint32_t millis() { return fake_sdk::now; }
inline uint32_t esp_random() { return ++fake_sdk::random_word; }
inline void delay(unsigned long ms) { fake_sdk::now += ms; }
struct FakeEsp {
  void restart() { ++fake_sdk::restarts; }
};
inline FakeEsp ESP;
