#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "secrets.h"
#include "config.h"
#include "ranging.h"
#include "stats.h"
#include "webui.h"

static void wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(webui_hostname().c_str());
  WiFi.setSleep(false);            // keep latency predictable for the web UI
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("[wifi] joining %s", WIFI_SSID);
  uint32_t giveup = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < giveup) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] %s  http://%s/  (http://%s.local/)\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.localIP().toString().c_str(),
                  webui_hostname().c_str());
  } else {
    // Ranging is the point of the board; it must not depend on the network.
    Serial.println("[wifi] not connected — ranging continues, will retry in background");
  }
}

static void ota_begin() {
  ArduinoOTA.setHostname(webui_hostname().c_str());
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([] {
    Serial.println("[ota] starting — suspending radio");
    ranging_suspend();
  });
  ArduinoOTA.onEnd([] { Serial.println("\n[ota] done"); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    Serial.printf("[ota] %u%%\r", (p * 100) / t);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] error %u — resuming radio\n", e);
    ranging_resume();
  });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== UWB link test ===");

  stats_init();
  webui_load_prefs();     // hostname must be known before Wi-Fi starts
  ranging_init();
  ranging_start();        // radio task on core 1

#ifdef NO_WIFI
  // Control build: radio silence on the Wi-Fi side, to test whether the
  // network stack is disturbing the ranging measurements.
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println("[sys] NO_WIFI control build");
#else
  wifi_connect();
  webui_begin();          // TCP/IP stack is up now
  if (MDNS.begin(webui_hostname().c_str())) MDNS.addService("http", "tcp", 80);
  ota_begin();
#endif

  Serial.printf("[sys] role=%s antdly=%u interval=%ums free heap=%u\n",
                ranging_role() == ROLE_INITIATOR ? "INITIATOR" : "RESPONDER",
                ranging_antdly(), ranging_interval(), ESP.getFreeHeap());
}

void loop() {
#ifdef NO_WIFI
  delay(100);
  return;
#endif
  // Core 0: network only. The radio never waits on any of this.
  ArduinoOTA.handle();
  webui_loop();

  static uint32_t last_try = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - last_try > 15000) {
    last_try = millis();
    WiFi.reconnect();
  }
  delay(2);
}
