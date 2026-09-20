#include <unity.h>

// Compile the actual adapter into this test TU. This permits inspection of its
// private state without adding any production accessor or test-only hook.
#include "admin.cpp"

namespace {
constexpr char kFixturePassword[] = "fixture password";
constexpr char kFixtureCsrf[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void seed_nvs(const char *value = kFixturePassword) {
  fake_sdk::nvs = {};
  fake_sdk::nvs.blob.assign(value, value + std::strlen(value) + 1);
}

String digest(const char *uri) {
  return String("Digest username=\"admin\", realm=\"BinRange Admin\", nonce=\"") +
      "0123456789abcdef0123456789abcdef\", uri=\"" + uri +
      "\", response=\"0123456789abcdef0123456789abcdef\", opaque=\"" +
      "0123456789abcdef0123456789abcdef\", qop=auth, nc=00000001, cnonce=\"abcdef\"";
}

void authorize_fixture(WebServer &server, const char *uri, const String &token) {
  server.request_uri = uri;
  server.headers["Host"] = "anchor.local";
  server.headers["Authorization"] = digest(uri);
  server.headers["X-BinRange-CSRF"] = token;
}

// Real upload session: disabled authorization must prevent side effects even
// when a previously configured device receives a fresh upload attempt.
struct FlashRadio : AdminUploadIo {
  unsigned pauses = 0, begins = 0, writes = 0, finishes = 0, aborts = 0, resumes = 0;
  bool suspend() override { ++pauses; return true; }
  void resume() override { ++resumes; }
  bool begin() override { ++begins; return true; }
  bool write(const uint8_t *, size_t) override { ++writes; return true; }
  bool finish() override { ++finishes; return true; }
  void abort() override { ++aborts; }
};

void assert_disabled(WebServer &server, unsigned expected_sets, unsigned expected_commits) {
  TEST_ASSERT_FALSE(admin_configured());
  TEST_ASSERT_TRUE(password.empty());
  TEST_ASSERT_TRUE(csrf.empty());
  TEST_ASSERT_FALSE(request_gate.configured());
  TEST_ASSERT_FALSE(ArduinoOTA.listening);
  TEST_ASSERT_EQUAL_UINT(1, ArduinoOTA.end_calls);
  TEST_ASSERT_TRUE(fake_sdk::http_mdns);
  TEST_ASSERT_EQUAL_UINT(expected_sets, fake_sdk::nvs.sets);
  TEST_ASSERT_EQUAL_UINT(expected_commits, fake_sdk::nvs.commits);
  TEST_ASSERT_EQUAL_UINT(0, fake_sdk::nvs.live_handles);
  TEST_ASSERT_FALSE(fake_sdk::nvs.invalid_arguments);
  TEST_ASSERT_EQUAL_UINT(0, fake_sdk::restarts);

  admin_apply_ota_credentials();
  TEST_ASSERT_EQUAL_UINT(0, ArduinoOTA.password_calls);
  TEST_ASSERT_EQUAL_INT((int)AdminRequestGate::Decision::Disabled,
      (int)request_gate.authorize(true, true, true, kFixtureCsrf, nullptr, "anchor.local", 1));
  authorize_fixture(server, "/api/admin", kFixtureCsrf);
  TEST_ASSERT_FALSE(admin_authorize(server, false));
  TEST_ASSERT_EQUAL_INT(503, server.status);
  TEST_ASSERT_EQUAL_UINT(0, server.response.find("Admin controls"));
  TEST_ASSERT_EQUAL_STRING("no-store", server.response_headers["Cache-Control"].c_str());

  FlashRadio io;
  AdminUploadSession upload(io);
  AdminUploadScope request(upload);
  const uint8_t bytes[] = {1};
  authorize_fixture(server, "/update", kFixtureCsrf);
  TEST_ASSERT_FALSE(upload.start(admin_authorize(server, true), 1));
  TEST_ASSERT_FALSE(upload.write(admin_authorize(server, true), bytes, 1, 2));
  TEST_ASSERT_FALSE(upload.end(admin_authorize(server, true), 3));
  TEST_ASSERT_FALSE(upload.finalize(admin_authorize(server, true), 4));
  TEST_ASSERT_EQUAL_UINT(0, io.pauses + io.begins + io.writes + io.finishes + io.aborts + io.resumes);
  TEST_ASSERT_EQUAL_UINT(expected_sets, fake_sdk::nvs.sets);
  TEST_ASSERT_EQUAL_UINT(expected_commits, fake_sdk::nvs.commits);
}
} // namespace

void setUp() {
  seed_nvs();
  std::memset(binrange::admin_bootstrap, 0, sizeof(binrange::admin_bootstrap));
  std::strcpy(binrange::admin_bootstrap, "fixture bootstrap");
  fake_sdk::now = 1;
  fake_sdk::random_word = 0;
  fake_sdk::restarts = 0;
  fake_sdk::http_mdns = true;
  ArduinoOTA = {};
  ArduinoOTA.setMdnsEnabled(false); // main.cpp gives HTTP mDNS ownership
  ArduinoOTA.listening = true;
  // Seed actual prior runtime state: failures must clear all of it.
  password = kFixturePassword;
  csrf = kFixtureCsrf;
  configured = true;
  request_gate = AdminRequestGate();
  request_gate.configure(kFixtureCsrf);
}
void tearDown() {}

void test_valid_nvs_wins_over_oversized_bootstrap_in_actual_adapter() {
  std::memset(binrange::admin_bootstrap, 'x', sizeof(binrange::admin_bootstrap) - 1);
  WebServer server;
  admin_begin(server);
  TEST_ASSERT_TRUE(admin_configured());
  TEST_ASSERT_EQUAL_STRING(kFixturePassword, password.c_str());
  TEST_ASSERT_EQUAL_UINT(1, fake_sdk::nvs.reads);
  TEST_ASSERT_EQUAL_UINT(0, fake_sdk::nvs.sets);
  TEST_ASSERT_EQUAL_UINT(0, ArduinoOTA.end_calls);
  admin_apply_ota_credentials();
  TEST_ASSERT_EQUAL_STRING(kFixturePassword, ArduinoOTA.applied_password.c_str());
}

void test_corrupt_nvs_clears_actual_state_stops_ota_and_never_rewrites() {
  WebServer server;
  seed_nvs("bad\ncredential");
  admin_begin(server);
  assert_disabled(server, 0, 0);
}

void test_open_size_and_read_errors_clear_actual_state_without_bootstrap() {
  for (unsigned failure = 0; failure < 3; ++failure) {
    setUp();
    WebServer server;
    fake_sdk::nvs.open_error = failure == 0;
    fake_sdk::nvs.size_error = failure == 1;
    fake_sdk::nvs.read_error = failure == 2;
    admin_begin(server);
    assert_disabled(server, 0, 0);
  }
}

void test_bootstrap_set_and_commit_failures_clear_actual_state_and_stop_ota() {
  for (bool commit_failure : {false, true}) {
    setUp();
    WebServer server;
    fake_sdk::nvs.missing = true;
    fake_sdk::nvs.set_error = !commit_failure;
    fake_sdk::nvs.commit_error = commit_failure;
    admin_begin(server);
    assert_disabled(server, 1, commit_failure ? 1 : 0);
  }
}

void test_password_set_and_commit_failures_clear_actual_state_and_stop_ota() {
  for (bool commit_failure : {false, true}) {
    setUp();
    WebServer server;
    admin_begin(server);
    TEST_ASSERT_TRUE(admin_configured());
    const String old_token = csrf;
    authorize_fixture(server, "/api/admin/password", old_token);
    server.form = {{"password", "replacement password"}, {"confirm", "replacement password"}};
    fake_sdk::nvs.set_error = !commit_failure;
    fake_sdk::nvs.commit_error = commit_failure;
    server.dispatch("/api/admin/password", HTTP_POST);
    TEST_ASSERT_EQUAL_INT(503, server.status);
    TEST_ASSERT_EQUAL_UINT(0, server.challenges);
    assert_disabled(server, 1, commit_failure ? 1 : 0);
    TEST_ASSERT_FALSE(admin_authorize(server, true));
  }
}

void test_invalid_bootstrap_only_disables_when_nvs_missing() {
  for (bool missing : {false, true}) {
    setUp();
    WebServer server;
    fake_sdk::nvs.missing = missing;
    binrange::admin_bootstrap[0] = '\0';
    admin_begin(server);
    if (missing) assert_disabled(server, 0, 0);
    else TEST_ASSERT_TRUE(admin_configured());
  }
}

void test_successful_bootstrap_is_persisted_before_token_and_ota_are_available() {
  fake_sdk::nvs.missing = true;
  WebServer server;
  admin_begin(server);
  TEST_ASSERT_TRUE(admin_configured());
  TEST_ASSERT_EQUAL_UINT(1, fake_sdk::nvs.sets);
  TEST_ASSERT_EQUAL_UINT(1, fake_sdk::nvs.commits);
  TEST_ASSERT_EQUAL_UINT(0, fake_sdk::nvs.live_handles);
  TEST_ASSERT_EQUAL_UINT(64, csrf.length());
  admin_apply_ota_credentials();
  TEST_ASSERT_EQUAL_STRING("fixture bootstrap", ArduinoOTA.applied_password.c_str());
  TEST_ASSERT_EQUAL_UINT(0, ArduinoOTA.end_calls);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_nvs_wins_over_oversized_bootstrap_in_actual_adapter);
  RUN_TEST(test_corrupt_nvs_clears_actual_state_stops_ota_and_never_rewrites);
  RUN_TEST(test_open_size_and_read_errors_clear_actual_state_without_bootstrap);
  RUN_TEST(test_bootstrap_set_and_commit_failures_clear_actual_state_and_stop_ota);
  RUN_TEST(test_password_set_and_commit_failures_clear_actual_state_and_stop_ota);
  RUN_TEST(test_invalid_bootstrap_only_disables_when_nvs_missing);
  RUN_TEST(test_successful_bootstrap_is_persisted_before_token_and_ota_are_available);
  return UNITY_END();
}
