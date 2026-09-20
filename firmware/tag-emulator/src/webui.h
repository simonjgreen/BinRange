#pragma once
#include <Arduino.h>

void webui_load_prefs();   // hostname from NVS; safe before Wi-Fi
void webui_begin();       // starts the HTTP server; requires an initialised TCP/IP stack
void webui_loop();
String webui_hostname();
void   webui_set_hostname(const String &h);
