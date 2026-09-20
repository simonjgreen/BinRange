#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "secrets.h"
#include "config.h"
#include "ranging.h"
#include "stats.h"
#include "webui.h"
#include "mqttlink.h"
#include "publisher.h"
#include "admin.h"
#include "tag_updater.h"
#include "update_mqtt.h"

static void wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(webui_hostname().c_str());
  // ESP-IDF 4.4 coexistence aborts when BLE starts with WIFI_PS_NONE.
  // Modem sleep lets the shared 2.4 GHz radio schedule Wi-Fi alongside BLE.
  WiFi.setSleep(WIFI_PS_MIN_MODEM);
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
  if (!admin_configured()) return;
  ArduinoOTA.setHostname(webui_hostname().c_str());
  ArduinoOTA.setMdnsEnabled(false);  // HTTP owns the shared mDNS responder.
  admin_apply_ota_credentials();
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
  ranging_init();         // bring up the radio, but do not start ranging yet

#ifdef NO_WIFI
  // Control build: radio silence on the Wi-Fi side, to test whether the
  // network stack is disturbing the ranging measurements.
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println("[sys] NO_WIFI control build");
#else
  wifi_connect();
  // Wall-clock time, so "last seen" can be a real timestamp in Home
  // Assistant rather than an uptime counter.
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  mqtt_begin();
  publisher_begin();
  tag_updater_begin();
  webui_begin();          // TCP/IP stack is up now
  if (MDNS.begin(webui_hostname().c_str())) MDNS.addService("http", "tcp", 80);
  ota_begin();
#endif

  Serial.printf("[sys] role=%s antdly=%u interval=%ums free heap=%u\n",
                ranging_role() == ROLE_INITIATOR ? "INITIATOR" : "RESPONDER",
                ranging_antdly(), ranging_interval(), ESP.getFreeHeap());
}

// Ranging starts only once MQTT has connected, so retained tag adoptions have
// arrived before the first exchange. Without this the queue fills during the
// Wi-Fi connect with nothing draining it, and adopted tags are briefly counted
// as unknown. The timeout guarantees ranging never depends on the broker.
#define RADIO_START_TIMEOUT_MS 15000
// Connecting is not the same as having received retained messages: the broker
// delivers those over the following loop iterations. Wait a moment after the
// connection so adoptions land before the first exchange.
#define RETAINED_SETTLE_MS 1500
static bool radio_started;
static uint32_t connected_since;

static void maybe_start_radio() {
    if (radio_started) return;
    if (mqtt_connected() && !connected_since) connected_since = millis();
    if (!mqtt_connected()) connected_since = 0;

    bool settled = connected_since && (millis() - connected_since > RETAINED_SETTLE_MS);
    if (settled || millis() > RADIO_START_TIMEOUT_MS) {
        radio_started = true;
        ranging_start();
        Serial.printf("[sys] ranging started (%s)\n",
                      settled ? "broker connected, retained messages settled"
                              : "broker timeout, starting anyway");
    }
}

void loop() {
#ifdef NO_WIFI
  delay(100);
  return;
#endif
  // Core 0: network only. The radio never waits on any of this.
  // A second firmware writer must not interrupt an owned tag BLE session.
  if (!tag_updater_busy()) ArduinoOTA.handle();
  webui_loop();
  mqtt_loop();
  maybe_start_radio();
  publisher_loop();
  tag_updater_loop();
  update_mqtt_loop();

  static uint32_t last_try = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - last_try > 15000) {
    last_try = millis();
    WiFi.reconnect();
  }
  delay(2);
}
