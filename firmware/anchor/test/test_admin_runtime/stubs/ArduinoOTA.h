#pragma once
#include "Arduino.h"

struct FakeArduinoOta {
  bool listening = false;
  bool mdns_enabled = true;
  unsigned end_calls = 0;
  unsigned password_calls = 0;
  String applied_password;
  void setMdnsEnabled(bool enabled) { mdns_enabled = enabled; }
  void setPassword(const char *value) {
    ++password_calls;
    applied_password = value;
  }
  void end() {
    ++end_calls;
    listening = false;
    if (mdns_enabled) fake_sdk::http_mdns = false;
  }
};
inline FakeArduinoOta ArduinoOTA;
