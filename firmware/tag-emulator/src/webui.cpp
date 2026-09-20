#include "webui.h"
#include "webui_page.h"
#include "ranging.h"
#include "stats.h"
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <math.h>

static WebServer server(80);
static Preferences net_prefs;
static String hostname;

String webui_hostname() { return hostname; }
void webui_set_hostname(const String &h) {
  hostname = h;
  net_prefs.putString("host", h);
}

// JSON has no NaN literal; absent values are emitted as null so the UI can
// distinguish "no data" from a real zero.
static void num(String &s, const char *k, float v, int dp) {
  s += "\"";
  s += k;
  s += "\":";
  if (isfinite(v)) { char b[24]; snprintf(b, sizeof(b), "%.*f", dp, v); s += b; }
  else s += "null";
  s += ",";
}

static void handle_stats() {
  Snapshot t;
  stats_snapshot(&t);

  String j;
  j.reserve(2600);
  j = "{";
  j += "\"host\":\"" + hostname + "\",";
  j += "\"role\":" + String((int)ranging_role()) + ",";
  j += "\"radio\":" + String(ranging_radio_ok() ? "true" : "false") + ",";
  j += "\"antdly\":" + String(ranging_antdly()) + ",";
  j += "\"interval\":" + String(ranging_interval()) + ",";
  j += "\"xtrim\":" + String(ranging_xtrim()) + ",";
  j += "\"phy\":" + String((int)ranging_phy()) + ",";
  j += "\"tag_addr\":" + String(ranging_tag_addr()) + ",";
  j += "\"drain_mult\":" + String(ranging_drain_mult()) + ",";
  j += "\"motion_tick_ms\":" + String(ranging_motion_tick_ms()) + ",";
  j += "\"idle_tick_ms\":" + String(ranging_idle_tick_ms()) + ",";
  j += "\"motion_hold_ms\":" + String(ranging_motion_hold_ms()) + ",";
  j += "\"tags\":[";
  for (uint8_t i = 0; i < ranging_tag_id_count(); i++) {
    if (i) j += ",";
    char h[8]; snprintf(h, sizeof(h), "%04x", ranging_tag_id_at(i));
    j += "{\"id\":\"" + String(h) + "\",\"moving\":" +
         String(ranging_tag_moving(i) ? "true" : "false") +
         ",\"wakes\":" + String(ranging_tag_wakes(i)) +
         ",\"batt_mv\":" + String(ranging_tag_batt_mv(i)) +
         ",\"uah\":" + String((uint32_t)ranging_tag_uah(i)) + "}";
  }
  j += "],";
  j += "\"burst_size\":" + String(ranging_burst_size()) + ",";
  j += "\"event_ms\":" + String(ranging_event_ms()) + ",";
  j += "\"ok\":" + String(t.ok) + ",";
  j += "\"timeout\":" + String(t.fail[FAIL_TIMEOUT]) + ",";
  j += "\"rx_error\":" + String(t.fail[FAIL_RX_ERROR]) + ",";
  j += "\"bad_frame\":" + String(t.fail[FAIL_BAD_FRAME]) + ",";
  num(j, "last_dist", t.last_dist, 3);
  num(j, "mean", t.mean, 4);
  num(j, "sd", t.sd, 4);
  num(j, "min", t.dmin, 3);
  num(j, "max", t.dmax, 3);
  num(j, "success", t.success_pct, 2);
  num(j, "rate", t.rate, 2);
  extern volatile uint32_t dbg_acc, dbg_cir, dbg_f1, dbg_f2, dbg_f3;
  j += "\"d_acc\":" + String(dbg_acc) + ",\"d_cir\":" + String(dbg_cir) +
       ",\"d_f1\":" + String(dbg_f1) + ",\"d_f2\":" + String(dbg_f2) +
       ",\"d_f3\":" + String(dbg_f3) + ",";
  num(j, "rssi", t.mean_rssi, 2);
  num(j, "fp", t.mean_fp, 2);
  num(j, "ppm", t.mean_ppm, 3);
  num(j, "rssi_fast", t.fast_rssi, 2);
  num(j, "fp_fast", t.fast_fp, 2);
  num(j, "rssi_last", t.last_rssi, 2);
  num(j, "fp_last", t.last_fp, 2);
  j += "\"last_age\":";
  j += (t.last_age_ms == 0xFFFFFFFF) ? "null" : String(t.last_age_ms);
  j += ",\"chart\":[";
  for (uint16_t i = 0; i < t.chart_n; i++) {
    if (i) j += ",";
    if (isfinite(t.chart[i])) { char b[16]; snprintf(b, sizeof(b), "%.3f", t.chart[i]); j += b; }
    else j += "null";
  }
  j += "]}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static void handle_config() {
  if (server.hasArg("role")) {
    Role r = server.arg("role").toInt() ? ROLE_RESPONDER : ROLE_INITIATOR;
    if (r != ranging_role()) ranging_set_role(r);
  }
  if (server.hasArg("antdly")) {
    long v = server.arg("antdly").toInt();
    if (v >= 0 && v <= 65535 && v != ranging_antdly()) ranging_set_antdly((uint16_t)v);
  }
  if (server.hasArg("phy")) {
    long v = server.arg("phy").toInt();
    if (v >= 0 && v < PHY_COUNT && (Phy)v != ranging_phy()) ranging_set_phy((Phy)v);
  }
  if (server.hasArg("drain_mult")) ranging_set_drain_mult(server.arg("drain_mult").toInt());
  if (server.hasArg("battery_reset")) ranging_reset_battery();
  if (server.hasArg("motion_tick_ms")) ranging_set_motion_tick_ms(server.arg("motion_tick_ms").toInt());
  if (server.hasArg("idle_tick_ms"))   ranging_set_idle_tick_ms(server.arg("idle_tick_ms").toInt());
  if (server.hasArg("motion_hold_ms")) ranging_set_motion_hold_ms(server.arg("motion_hold_ms").toInt());
  if (server.hasArg("tag_ids")) {
    ranging_set_tag_ids(server.arg("tag_ids").c_str());
  }
  if (server.hasArg("tag_addr")) {
    long v = server.arg("tag_addr").toInt();
    if (v > 0 && v <= 0xFFFF) ranging_set_tag_addr((uint16_t)v);
  }
  if (server.hasArg("burst_size")) {
    long v = server.arg("burst_size").toInt();
    if (v >= 1 && v <= 64) ranging_set_burst_size((uint16_t)v);
  }
  if (server.hasArg("event_ms")) {
    long v = server.arg("event_ms").toInt();
    if (v >= 200 && v <= 3600000) ranging_set_event_ms((uint32_t)v);
  }
  if (server.hasArg("xtrim")) {
    long v = server.arg("xtrim").toInt();
    if (v >= 0 && v <= 0x7F && v != ranging_xtrim()) ranging_set_xtrim((uint8_t)v);
  }
  if (server.hasArg("interval")) {
    long v = server.arg("interval").toInt();
    if (v >= 20 && v <= 5000) ranging_set_interval((uint16_t)v);
  }
  if (server.hasArg("host")) {
    String h = server.arg("host");
    h.trim();
    if (h.length() && h != hostname) webui_set_hostname(h);   // applied on next boot
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_reset() {
  stats_reset();
  server.send(200, "application/json", "{\"ok\":true}");
}

// A hostname change only takes effect on the next boot, and a remote board
// has no reset button within reach.
static void handle_reboot() {
  server.send(200, "application/json", "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

static void handle_update_done() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok ? "Update OK — rebooting" : "Update FAILED");
  if (ok) { delay(500); ESP.restart(); }
  else ranging_resume();
}

static void handle_update_upload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] http update: %s\n", up.filename.c_str());
    ranging_suspend();   // stop the radio before any flash write
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    ranging_resume();
  }
}

void webui_load_prefs() {
  net_prefs.begin("net", false);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char dflt[20];
  snprintf(dflt, sizeof(dflt), "uwb-%02x%02x%02x", mac[3], mac[4], mac[5]);
  hostname = net_prefs.getString("host", dflt);
}

void webui_begin() {
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/api/stats", HTTP_GET, handle_stats);
  server.on("/api/config", HTTP_POST, handle_config);
  server.on("/api/reset", HTTP_POST, handle_reset);
  // Stands in for the accelerometer interrupt.
  server.on("/api/motion", HTTP_POST, [] {
    uint16_t a = (uint16_t)strtoul(server.arg("id").c_str(), nullptr, 16);
    bool settle = server.hasArg("settle") && server.arg("settle") == "1";
    bool ok = settle ? ranging_settle(a) : ranging_trigger_motion(a);
    server.send(200, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"unknown id\"}");
  });
  server.on("/api/reboot", HTTP_POST, handle_reboot);
  server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
  server.begin();
}

void webui_loop() { server.handleClient(); }
