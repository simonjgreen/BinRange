#include "admin.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <nvs.h>

#include "anchor_admin_bootstrap.h"
#include "core/admin_credentials.h"
#include "core/admin_policy.h"

namespace {
constexpr char kNamespace[] = "binadmin";
constexpr char kPasswordKey[] = "password";
constexpr char kUsername[] = "admin";
constexpr char kRealm[] = "BinRange Admin";
constexpr size_t kPasswordBytes = 65;
constexpr size_t kHeaderBytes = 768;
constexpr size_t kHostBytes = 128;

String password;
String csrf;
bool configured = false;
AdminRequestGate request_gate;

void send_disabled(WebServer &server) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(503, "text/plain", "Admin controls are unavailable");
}

void send_forbidden(WebServer &server) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(403, "text/plain", "Forbidden");
}

void send_limited(WebServer &server) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Retry-After", "60");
  server.send(429, "text/plain", "Too many authentication attempts");
}

void send_challenge(WebServer &server) {
  server.sendHeader("Cache-Control", "no-store");
  server.requestAuthentication(DIGEST_AUTH, kRealm);
}

void rotate_csrf() {
  char token[65];
  for (size_t i = 0; i < 8; ++i)
    snprintf(token + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
  csrf = token;
  request_gate.configure(token);
}

void disable_admin() {
  password = "";
  csrf = "";
  configured = false;
  request_gate.disable();
  ArduinoOTA.end();
}

class RuntimeCredentialFailure : public AdminCredentialFailureIo {
 public:
  void disable_admin_and_ota() override { disable_admin(); }
};

class NvsCredentialIo : public AdminCredentialIo {
 public:
  NvsCredentialIo() : handle_(0), open_(false) {}
  ~NvsCredentialIo() { close(); }
  AdminCredentialRead read(char *out, size_t *length) override {
    nvs_handle_t handle;
    if (!out || !length || nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
      return AdminCredentialRead::Error;
    size_t stored_length = 0;
    esp_err_t result = nvs_get_blob(handle, kPasswordKey, nullptr, &stored_length);
    if (result == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return AdminCredentialRead::NotFound; }
    if (result != ESP_OK || stored_length > *length || stored_length > kPasswordBytes) {
      nvs_close(handle); return AdminCredentialRead::Error;
    }
    result = nvs_get_blob(handle, kPasswordKey, out, &stored_length);
    nvs_close(handle);
    if (result != ESP_OK) return AdminCredentialRead::Error;
    *length = stored_length;
    return AdminCredentialRead::Found;
  }
  bool write(const char *value, size_t length) override {
    close();
    if (nvs_open(kNamespace, NVS_READWRITE, &handle_) != ESP_OK) return false;
    open_ = true;
    if (nvs_set_blob(handle_, kPasswordKey, value, length) == ESP_OK) return true;
    close();
    return false;
  }
  bool commit() override {
    if (!open_) return false;
    const bool committed = nvs_commit(handle_) == ESP_OK;
    close();
    return committed;
  }
 private:
  void close() { if (open_) nvs_close(handle_); open_ = false; handle_ = 0; }
  nvs_handle_t handle_;
  bool open_;
};

bool load_password() {
  NvsCredentialIo io;
  RuntimeCredentialFailure failure;
  char stored[kPasswordBytes] = {};
  size_t length = sizeof(stored);
  const size_t bootstrap_length = strnlen(binrange::admin_bootstrap, kPasswordBytes);
  // Even an invalid/oversized bootstrap must not mask valid persisted NVS.
  // admin_load_credential validates it only after a definite NotFound result.
  if (!admin_load_credential_or_disable(io, binrange::admin_bootstrap, bootstrap_length,
                                        stored, &length, failure))
    return false;
  password = stored;
  return true;
}

bool persist_password(const char *value, size_t length) {
  NvsCredentialIo io;
  RuntimeCredentialFailure failure;
  return admin_store_credential_or_disable(io, value, length, failure);
}

bool valid_password_form(WebServer &server, String &next) {
  if (server.args() != 2) return false;
  bool have_password = false;
  bool have_confirm = false;
  String confirm;
  for (int i = 0; i < server.args(); ++i) {
    const String name = server.argName(i);
    const String value = server.arg(i);
    if (name.length() > 16 || value.length() > 64) return false;
    if (name == "password" && !have_password) {
      next = value;
      have_password = true;
    } else if (name == "confirm" && !have_confirm) {
      confirm = value;
      have_confirm = true;
    } else {
      return false;
    }
  }
  return have_password && have_confirm && next == confirm &&
         admin_password_valid(next.c_str(), next.length());
}

void handle_admin_get(WebServer &server) {
  if (!admin_authorize(server, false)) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"configured\":true,\"csrf\":\"" + csrf + "\"}");
}

void handle_password(WebServer &server) {
  if (!admin_authorize(server, true)) return;
  String next;
  if (!valid_password_form(server, next)) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(400, "text/plain", "Invalid password form");
    return;
  }
  if (next == password) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(204, "text/plain", "");
    return;
  }
  if (!persist_password(next.c_str(), next.length())) {
    // A failed commit can leave the durable state uncertain. Stop both admin
    // transports instead of retaining a possibly stale OTA credential.
    send_disabled(server);
    return;
  }
  password = next;
  rotate_csrf();
  // Force a fresh Digest challenge. ArduinoOTA stores its password hash only
  // at begin(), so rebooting after this response applies the persisted secret
  // to OTA without ever exposing it through an accessor.
  send_challenge(server);
  delay(300);
  ESP.restart();
}
}  // namespace

void admin_begin(WebServer &server) {
  if (load_password()) {
    configured = true;
    rotate_csrf();
  }
  server.on("/api/admin", HTTP_GET, [&server] { handle_admin_get(server); });
  server.on("/api/admin/password", HTTP_POST, [&server] { handle_password(server); });
}

bool admin_configured() { return configured; }

void admin_apply_ota_credentials() {
  if (configured && password.length()) ArduinoOTA.setPassword(password.c_str());
}

bool admin_authorize(WebServer &server, bool mutation) {
  const String authorization = server.header("Authorization");
  const bool supplied = authorization.length() != 0;
  const String token = server.header("X-BinRange-CSRF");
  const String origin = server.header("Origin");
  const String host = server.header("Host");
  const bool context_bounds = token.length() <= 64 &&
      origin.length() <= kHostBytes + 7 && host.length() <= kHostBytes;
  const bool authenticated = configured && authorization.length() <= kHeaderBytes &&
      server.uri().length() <= kHostBytes &&
      admin_digest_syntax_valid(authorization.c_str(), server.uri().c_str()) &&
      server.authenticate(kUsername, password.c_str());
  const AdminRequestGate::Decision decision = request_gate.authorize(
      authenticated, supplied, mutation, context_bounds ? token.c_str() : nullptr,
      context_bounds && origin.length() ? origin.c_str() : nullptr,
      context_bounds ? host.c_str() : nullptr, millis());
  switch (decision) {
    case AdminRequestGate::Decision::Allow: return true;
    case AdminRequestGate::Decision::Disabled: send_disabled(server); break;
    case AdminRequestGate::Decision::Challenge: send_challenge(server); break;
    case AdminRequestGate::Decision::RateLimited: send_limited(server); break;
    case AdminRequestGate::Decision::Forbidden: send_forbidden(server); break;
  }
  return false;
}
