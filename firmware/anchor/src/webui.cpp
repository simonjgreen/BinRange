#include "webui.h"
#include "admin.h"
#include "core/admin_policy.h"
#include "webui_page.h"
#include "ranging.h"
#include "stats.h"
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include "mqttlink.h"
#include "mqttcfg.h"
#include "tag_updater.h"
#include "update_web.h"
#include <math.h>

static UpdateWebServer server(80);
static Preferences net_prefs;
static String hostname;

class HttpUpdateIo : public AdminUploadIo {
 public:
  bool suspend() override {
    if (tag_updater_busy()) return false;
    ranging_suspend(); return true;
  }
  void resume() override { ranging_resume(); }
  bool begin() override { return Update.begin(UPDATE_SIZE_UNKNOWN); }
  bool write(const uint8_t *bytes, size_t length) override {
    // Pinned Arduino-ESP32 2.0.14 requires uint8_t*, but write() only memcpy-reads
    // the input. Keep the const contract above this SDK adapter.
    return Update.write(const_cast<uint8_t *>(bytes), length) == length;
  }
  bool finish() override { return Update.end(true); }
  void abort() override { Update.abort(); }
};

static HttpUpdateIo http_update_io;
static AdminUploadSession http_upload(http_update_io);

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

// Broker settings and live MQTT activity. The password is deliberately
// write-only: the API reports only whether one is set. Screenshots of this
// page end up in the build log.
static void handle_mqtt_get() {
    BrokerCfg c;
    mqttcfg_load(&c);
    MqttActivity a;
    mqtt_activity(&a);

    String j;
    j.reserve(2400);
    j = "{";
    j += "\"host\":\"" + String(c.host) + "\",";
    j += "\"port\":" + String(c.port) + ",";
    j += "\"user\":\"" + String(c.user) + "\",";
    j += "\"client_id\":\"" + String(c.client_id) + "\",";
    j += "\"pass_set\":" + String(mqttcfg_has_password() ? "true" : "false") + ",";
    j += "\"connected\":" + String(a.connected ? "true" : "false") + ",";
    j += "\"broker\":\"" + String(mqtt_broker_desc()) + "\",";
    j += "\"published\":" + String(a.published) + ",";
    j += "\"failed\":" + String(a.failed) + ",";
    j += "\"reconnects\":" + String(a.reconnects) + ",";
    j += "\"last_rc\":" + String(a.last_rc) + ",";
    j += "\"last_rc_text\":\"" + String(mqtt_state_text(a.last_rc)) + "\",";
    j += "\"last_pub_age\":";
    j += a.last_pub_ms ? String(millis() - a.last_pub_ms) : String("null");
    j += ",\"log\":[";
    for (size_t i = 0; i < mqtt_log_size(); i++) {
        uint32_t age = 0;
        const char *line = mqtt_log_at(i, &age);
        if (!line) continue;
        if (i) j += ",";
        // Escape the few characters that can appear in a topic or payload.
        String t(line);
        t.replace("\\", "\\\\");
        t.replace("\"", "\\\"");
        j += "{\"t\":\"" + t + "\",\"age\":" + String(age) + "}";
    }
    j += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

static void handle_mqtt_post() {
    BrokerCfg c;
    mqttcfg_load(&c);
    if (server.hasArg("host"))
        strlcpy(c.host, server.arg("host").c_str(), sizeof(c.host));
    if (server.hasArg("port")) {
        long v = server.arg("port").toInt();
        if (v > 0 && v <= 65535) c.port = (uint16_t)v;
    }
    if (server.hasArg("user"))
        strlcpy(c.user, server.arg("user").c_str(), sizeof(c.user));
    if (server.hasArg("client_id") && server.arg("client_id").length())
        strlcpy(c.client_id, server.arg("client_id").c_str(), sizeof(c.client_id));
    // Blank means "leave unchanged", so the form never has to echo the secret.
    if (server.hasArg("pass") && server.arg("pass").length())
        strlcpy(c.pass, server.arg("pass").c_str(), sizeof(c.pass));
    mqttcfg_save(&c);
    server.send(200, "application/json", "{\"ok\":true}");
    mqtt_reconnect_now();
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
  if (!admin_authorize(server, true)) {
    http_upload.abort();
    return;
  }
  const bool ok = http_upload.finalize(true, millis());
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok ? "Update OK — rebooting" : "Update FAILED");
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

static void handle_update_upload() {
  HTTPUpload &up = server.upload();
  const bool authorized = admin_authorize(server, true);
  if (up.status == UPLOAD_FILE_START) {
    http_upload.start(authorized, millis());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    http_upload.write(authorized, up.buf, up.currentSize, millis());
  } else if (up.status == UPLOAD_FILE_END) {
    http_upload.end(authorized, millis());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    http_upload.abort();
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
  const char *admin_headers[] = {"X-BinRange-CSRF", "Origin", "Host"};
  server.collectHeaders(admin_headers, sizeof(admin_headers) / sizeof(admin_headers[0]));
  admin_begin(server);
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/api/stats", HTTP_GET, handle_stats);
  server.on("/api/config", HTTP_POST, handle_config);
  server.on("/api/reset", HTTP_POST, handle_reset);
  server.on("/api/reboot", HTTP_POST, handle_reboot);
  server.on("/api/mqtt", HTTP_GET, handle_mqtt_get);
  server.on("/api/mqtt", HTTP_POST, handle_mqtt_post);
  server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
  update_web_begin(server);
  server.begin();
}

void webui_loop() {
  // Pinned Arduino-ESP32 2.0.14 WebServer.cpp:286-351 parses at most one request
  // synchronously per call. Once parsing starts, every file callback and the
  // final handler (or parser failure) occur before it returns. Scope exit also
  // cleans up failures which skip both the final handler and FILE_ABORTED.
  AdminUploadScope request(http_upload);
  update_web_request_begin();
  server.handleClient();
  update_web_request_end();
}
