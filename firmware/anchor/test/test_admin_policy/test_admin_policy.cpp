#include <unity.h>
#include <initializer_list>
#include <string>

#include "core/admin_policy.h"

void setUp() {}
void tearDown() {}

void test_password_requires_printable_ascii_and_preserves_spaces() {
    TEST_ASSERT_TRUE(admin_password_valid("  valid pass  ", 14));
    TEST_ASSERT_TRUE(admin_password_valid("123456789012", 12));
    TEST_ASSERT_TRUE(admin_password_valid("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!!", 64));
    TEST_ASSERT_FALSE(admin_password_valid("short", 5));
    TEST_ASSERT_FALSE(admin_password_valid("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!!!", 65));
    TEST_ASSERT_FALSE(admin_password_valid("twelve\nchars", 12));
}

void test_digest_requires_unambiguous_qop_auth_and_actual_uri() {
    const char *good = "Digest username=\"admin\", realm=\"BinRange Admin\", nonce=\"0123456789abcdef0123456789abcdef\", uri=\"/api/admin\", response=\"0123456789abcdef0123456789abcdef\", opaque=\"0123456789abcdef0123456789abcdef\", qop=auth, nc=00000001, cnonce=\"abcdef0123456789\"";
    TEST_ASSERT_TRUE(admin_digest_syntax_valid(good, "/api/admin"));
    std::string quoted(good);
    quoted.replace(quoted.find("qop=auth"), 8, "qop=\"auth\"");
    TEST_ASSERT_TRUE(admin_digest_syntax_valid(quoted.c_str(), "/api/admin"));
    quoted.replace(quoted.find("qop=\"auth\""), 10, "qop=\"auth-int\"");
    TEST_ASSERT_FALSE(admin_digest_syntax_valid(quoted.c_str(), "/api/admin"));
    TEST_ASSERT_FALSE(admin_digest_syntax_valid("Basic YWRtaW46eA==", "/api/admin"));
    TEST_ASSERT_FALSE(admin_digest_syntax_valid("Digest username=\"admin\", realm=\"BinRange Admin\", nonce=\"n\", uri=\"/api/admin\", response=\"r\", opaque=\"o\"", "/api/admin"));
    TEST_ASSERT_FALSE(admin_digest_syntax_valid("Digest username=\"admin\", realm=\"BinRange Admin\", nonce=\"n\", uri=\"/api/admin/other\", response=\"r\", opaque=\"o\", qop=auth, nc=00000001, cnonce=\"c\"", "/api/admin"));
    TEST_ASSERT_FALSE(admin_digest_syntax_valid("Digest username=\"admin\", realm=\"BinRange Admin\", username=\"other\", nonce=\"n\", uri=\"/api/admin\", response=\"r\", opaque=\"o\", qop=auth, nc=00000001, cnonce=\"c\"", "/api/admin"));
    TEST_ASSERT_FALSE(admin_digest_syntax_valid("Digest username=\"admin\", realm=\"BinRange Admin\", nonce=\"n\", uri=\"/api/admin\", response=\"r\", opaque=\"o\", qop=auth-int, nc=00000001, cnonce=\"c\"", "/api/admin"));
    TEST_ASSERT_FALSE(admin_digest_syntax_valid("Digest username=\"admin\", realm=\"BinRange Admin\", nonce=\"n\", uri=\"/api/admin\", response=\"r\", opaque=\"o\", qop=auth, nc=00000001, cnonce=\"c\",", "/api/admin"));
}

void test_origin_is_exact_host_only_and_cli_without_origin_is_allowed() {
    TEST_ASSERT_TRUE(admin_origin_matches_host(nullptr, "anchor.local"));
    TEST_ASSERT_TRUE(admin_origin_matches_host("http://anchor.local", "anchor.local"));
    TEST_ASSERT_TRUE(admin_origin_matches_host("http://anchor.local:80", "anchor.local:80"));
    TEST_ASSERT_FALSE(admin_origin_matches_host("http://evil-anchor.local", "anchor.local"));
    TEST_ASSERT_FALSE(admin_origin_matches_host("http://anchor.local.evil", "anchor.local"));
    TEST_ASSERT_FALSE(admin_origin_matches_host("http://user@anchor.local", "anchor.local"));
    TEST_ASSERT_FALSE(admin_origin_matches_host("null", "anchor.local"));
}

void test_rate_limit_counts_only_credential_attempts_and_wraps_safely() {
    AdminRateLimit limit;
    const uint32_t start = 0xfffffff0U;
    limit.record_failure(false, start);
    TEST_ASSERT_FALSE(limit.blocked(start));
    for (unsigned i = 0; i != 5; ++i) limit.record_failure(true, start + i);
    TEST_ASSERT_TRUE(limit.blocked(start + 5));
    TEST_ASSERT_TRUE(limit.blocked(start + 60003U));
    TEST_ASSERT_FALSE(limit.blocked(start + 60004U));
    limit.record_success();
    TEST_ASSERT_FALSE(limit.blocked(101));
}

void test_mutations_require_current_csrf_and_matching_origin() {
    AdminRequestGate gate;
    gate.configure("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    TEST_ASSERT_EQUAL_INT((int)AdminRequestGate::Decision::Allow, (int)gate.authorize(true, true, false, nullptr, nullptr, "anchor.local", 1));
    TEST_ASSERT_EQUAL_INT((int)AdminRequestGate::Decision::Forbidden, (int)gate.authorize(true, true, true, nullptr, nullptr, "anchor.local", 1));
    TEST_ASSERT_EQUAL_INT((int)AdminRequestGate::Decision::Forbidden, (int)gate.authorize(true, true, true, "old", nullptr, "anchor.local", 1));
    TEST_ASSERT_EQUAL_INT((int)AdminRequestGate::Decision::Forbidden, (int)gate.authorize(true, true, true, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "http://evil.anchor.local", "anchor.local", 1));
    TEST_ASSERT_EQUAL_INT((int)AdminRequestGate::Decision::Allow, (int)gate.authorize(true, true, true, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", nullptr, "anchor.local", 1));
}

class CountingUpload : public AdminUploadIo {
  public:
    unsigned suspends = 0;
    unsigned resumes = 0;
    unsigned begins = 0;
    unsigned writes = 0;
    unsigned ends = 0;
    unsigned aborts = 0;
    bool suspend() override { ++suspends; return true; }
    void resume() override { ++resumes; }
    bool begin() override { ++begins; return true; }
    bool write(const uint8_t *, size_t) override { ++writes; return true; }
    bool finish() override { ++ends; return true; }
    void abort() override { ++aborts; }
};

void test_second_file_after_first_end_aborts_without_finalizing_boot_partition() {
    CountingUpload io;
    AdminUploadSession upload(io);
    uint8_t bytes[] = {1, 2};
    TEST_ASSERT_TRUE(upload.start(true, 1));
    TEST_ASSERT_TRUE(upload.write(true, bytes, sizeof(bytes), 2));
    TEST_ASSERT_TRUE(upload.end(true, 3));
    TEST_ASSERT_FALSE(upload.start(true, 4));
    TEST_ASSERT_FALSE(upload.finalize(true, 5));
    TEST_ASSERT_EQUAL_UINT(1, io.begins);
    TEST_ASSERT_EQUAL_UINT(1, io.aborts);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes);
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
}

void test_update_boundary_finishes_only_after_final_handler_accepts_one_file() {
    CountingUpload io;
    AdminUploadSession upload(io);
    uint8_t bytes[] = {1};
    TEST_ASSERT_TRUE(upload.start(true, 1));
    TEST_ASSERT_TRUE(upload.write(true, bytes, sizeof(bytes), 2));
    TEST_ASSERT_TRUE(upload.end(true, 3));
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
    TEST_ASSERT_TRUE(upload.finalize(true, 4));
    TEST_ASSERT_EQUAL_UINT(1, io.ends);
}

void test_upload_auth_loss_aborts_and_resumes_without_finishing() {
    CountingUpload io;
    AdminUploadSession upload(io);
    uint8_t bytes[] = {1};
    TEST_ASSERT_TRUE(upload.start(true, 1));
    TEST_ASSERT_FALSE(upload.write(false, bytes, sizeof(bytes), 2));
    TEST_ASSERT_FALSE(upload.finalize(true, 3));
    TEST_ASSERT_EQUAL_UINT(1, io.aborts);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes);
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
}

void test_upload_without_bytes_never_finishes_the_update() {
    CountingUpload io;
    AdminUploadSession upload(io);
    TEST_ASSERT_TRUE(upload.start(true, 1));
    TEST_ASSERT_FALSE(upload.end(true, 2));
    TEST_ASSERT_FALSE(upload.finalize(true, 3));
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
    TEST_ASSERT_EQUAL_UINT(1, io.aborts);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes);
}

void test_unauthorized_upload_never_pauses_or_writes_flash() {
    CountingUpload io;
    AdminUploadSession upload(io);
    uint8_t bytes[] = {1};
    TEST_ASSERT_FALSE(upload.start(false, 1));
    TEST_ASSERT_FALSE(upload.write(false, bytes, sizeof(bytes), 2));
    TEST_ASSERT_FALSE(upload.finalize(false, 3));
    TEST_ASSERT_EQUAL_UINT(0, io.suspends);
    TEST_ASSERT_EQUAL_UINT(0, io.begins);
    TEST_ASSERT_EQUAL_UINT(0, io.writes);
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
}

void test_rejected_callbacks_after_end_poison_request() {
    for (unsigned rejected = 0; rejected < 4; ++rejected) {
        CountingUpload io;
        AdminUploadSession upload(io);
        const uint8_t bytes[] = {1};
        TEST_ASSERT_TRUE(upload.start(true, 1));
        TEST_ASSERT_TRUE(upload.write(true, bytes, sizeof(bytes), 2));
        TEST_ASSERT_TRUE(upload.end(true, 3));
        if (rejected < 2) TEST_ASSERT_FALSE(upload.end(rejected == 0, 4));
        else TEST_ASSERT_FALSE(upload.write(rejected == 2, bytes, sizeof(bytes), 4));
        TEST_ASSERT_TRUE(upload.failed());
        TEST_ASSERT_FALSE(upload.finalize(true, 5));
        TEST_ASSERT_EQUAL_UINT(0, io.ends);
        TEST_ASSERT_EQUAL_UINT(1, io.aborts);
        TEST_ASSERT_EQUAL_UINT(1, io.resumes);
    }
}

// The body represents the synchronous callback sequence inside handleClient().
// It uses the same production scope as webui_loop, including early parser exits.
void test_end_then_parse_failure_cleans_up_before_next_fileless_post() {
    CountingUpload io;
    AdminUploadSession upload(io);
    {
        AdminUploadScope request(upload);
        const uint8_t bytes[] = {1};
        TEST_ASSERT_TRUE(upload.start(true, 1));
        TEST_ASSERT_TRUE(upload.write(true, bytes, 1, 2));
        TEST_ASSERT_TRUE(upload.end(true, 3));
        // Parser returns false: no ABORT callback and no final handler.
    }
    TEST_ASSERT_EQUAL_UINT(1, io.aborts);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes);
    {
        AdminUploadScope next_request(upload);
        TEST_ASSERT_FALSE(upload.finalize(true, 4));
    }
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
    TEST_ASSERT_EQUAL_UINT(1, io.begins);
}

void test_abort_then_next_request_accepts_valid_upload() {
    CountingUpload io;
    AdminUploadSession upload(io);
    {
        AdminUploadScope request(upload);
        TEST_ASSERT_TRUE(upload.start(true, 1));
        upload.abort();
        TEST_ASSERT_FALSE(upload.start(true, 2));
        // No final handler after aborted parser.
    }
    {
        AdminUploadScope next_request(upload);
        const uint8_t bytes[] = {1};
        TEST_ASSERT_TRUE(upload.start(true, 3));
        TEST_ASSERT_TRUE(upload.write(true, bytes, 1, 4));
        TEST_ASSERT_TRUE(upload.end(true, 5));
        TEST_ASSERT_TRUE(upload.finalize(true, 6));
        TEST_ASSERT_FALSE(upload.finalize(true, 7));
    }
    TEST_ASSERT_EQUAL_UINT(2, io.begins);
    TEST_ASSERT_EQUAL_UINT(1, io.aborts);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes); // success stays paused for restart
    TEST_ASSERT_EQUAL_UINT(1, io.ends);
}

void test_request_entry_discards_any_pending_image() {
    CountingUpload io;
    AdminUploadSession upload(io);
    const uint8_t bytes[] = {1};
    TEST_ASSERT_TRUE(upload.start(true, 1));
    TEST_ASSERT_TRUE(upload.write(true, bytes, 1, 2));
    TEST_ASSERT_TRUE(upload.end(true, 3));
    {
        AdminUploadScope new_request(upload);
        TEST_ASSERT_FALSE(upload.finalize(true, 4));
    }
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
    TEST_ASSERT_EQUAL_UINT(1, io.aborts);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes);
}

void test_duplicate_files_remain_rejected_until_request_exit() {
    CountingUpload io;
    AdminUploadSession upload(io);
    {
        AdminUploadScope request(upload);
        const uint8_t bytes[] = {1};
        TEST_ASSERT_TRUE(upload.start(true, 1));
        TEST_ASSERT_TRUE(upload.write(true, bytes, 1, 2));
        TEST_ASSERT_TRUE(upload.end(true, 3));
        TEST_ASSERT_FALSE(upload.start(true, 4));
        TEST_ASSERT_FALSE(upload.start(true, 5));
        TEST_ASSERT_FALSE(upload.write(true, bytes, 1, 6));
        TEST_ASSERT_FALSE(upload.end(true, 7));
        TEST_ASSERT_FALSE(upload.finalize(true, 8));
    }
    TEST_ASSERT_EQUAL_UINT(1, io.begins);
    TEST_ASSERT_EQUAL_UINT(1, io.writes);
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
    TEST_ASSERT_EQUAL_UINT(1, io.aborts);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes);
}

void test_disconnect_and_no_final_callback_resume_unfinished_upload() {
    CountingUpload io;
    AdminUploadSession upload(io);
    {
        AdminUploadScope request(upload);
        TEST_ASSERT_TRUE(upload.start(true, 1));
        // Disconnect / parse failure without any END or ABORT callback.
    }
    TEST_ASSERT_EQUAL_UINT(1, io.aborts);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes);
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
}

void test_inactive_upload_timeout_wraps_and_cannot_finalize() {
    CountingUpload io;
    AdminUploadSession upload(io);
    AdminUploadScope request(upload);
    const uint32_t start = 0xfffffff0U;
    const uint8_t bytes[] = {1};
    TEST_ASSERT_TRUE(upload.start(true, start));
    TEST_ASSERT_FALSE(upload.write(true, bytes, 1, start + 30000U));
    TEST_ASSERT_FALSE(upload.finalize(true, start + 30001U));
    TEST_ASSERT_EQUAL_UINT(0, io.writes);
    TEST_ASSERT_EQUAL_UINT(0, io.ends);
    TEST_ASSERT_EQUAL_UINT(1, io.resumes);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_password_requires_printable_ascii_and_preserves_spaces);
    RUN_TEST(test_digest_requires_unambiguous_qop_auth_and_actual_uri);
    RUN_TEST(test_origin_is_exact_host_only_and_cli_without_origin_is_allowed);
    RUN_TEST(test_rate_limit_counts_only_credential_attempts_and_wraps_safely);
    RUN_TEST(test_mutations_require_current_csrf_and_matching_origin);
    RUN_TEST(test_second_file_after_first_end_aborts_without_finalizing_boot_partition);
    RUN_TEST(test_update_boundary_finishes_only_after_final_handler_accepts_one_file);
    RUN_TEST(test_upload_auth_loss_aborts_and_resumes_without_finishing);
    RUN_TEST(test_upload_without_bytes_never_finishes_the_update);
    RUN_TEST(test_unauthorized_upload_never_pauses_or_writes_flash);
    RUN_TEST(test_rejected_callbacks_after_end_poison_request);
    RUN_TEST(test_end_then_parse_failure_cleans_up_before_next_fileless_post);
    RUN_TEST(test_abort_then_next_request_accepts_valid_upload);
    RUN_TEST(test_request_entry_discards_any_pending_image);
    RUN_TEST(test_duplicate_files_remain_rejected_until_request_exit);
    RUN_TEST(test_disconnect_and_no_final_callback_resume_unfinished_upload);
    RUN_TEST(test_inactive_upload_timeout_wraps_and_cannot_finalize);
    return UNITY_END();
}
