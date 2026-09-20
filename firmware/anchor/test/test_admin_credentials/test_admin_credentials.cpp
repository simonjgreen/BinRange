#include <unity.h>

#include <cstring>

#include "core/admin_credentials.h"

void setUp() {}
void tearDown() {}

class MemoryCredentials : public AdminCredentialIo {
  public:
    AdminCredentialRead read_result = AdminCredentialRead::NotFound;
    bool write_result = true;
    bool commit_result = true;
    char stored[65] = {};
    size_t stored_length = 0;
    unsigned writes = 0;
    unsigned commits = 0;

    AdminCredentialRead read(char *out, size_t *length) override {
        if (read_result != AdminCredentialRead::Found) return read_result;
        if (!length || *length < stored_length) return AdminCredentialRead::Error;
        memcpy(out, stored, stored_length);
        *length = stored_length;
        return AdminCredentialRead::Found;
    }
    bool write(const char *value, size_t length) override {
        ++writes;
        if (!write_result) return false;
        memcpy(stored, value, length);
        stored_length = length;
        return true;
    }
    bool commit() override { ++commits; return commit_result; }
};

class DisableRecorder : public AdminCredentialFailureIo {
  public:
    unsigned disables = 0;
    void disable_admin_and_ota() override { ++disables; }
};

void test_corrupt_existing_nvs_disables_without_rewrite() {
    MemoryCredentials io;
    io.read_result = AdminCredentialRead::Found;
    memcpy(io.stored, "bad\ncredential", 15);
    io.stored_length = 15;
    char password[65];
    size_t length = sizeof(password);
    TEST_ASSERT_FALSE(admin_load_credential(io, "valid bootstrap", 15, password, &length));
    TEST_ASSERT_EQUAL_UINT(0, io.writes);
    TEST_ASSERT_EQUAL_UINT(0, io.commits);
}

void test_bootstrap_write_failure_disables_without_commit() {
    MemoryCredentials io;
    io.write_result = false;
    char password[65];
    size_t length = sizeof(password);
    TEST_ASSERT_FALSE(admin_load_credential(io, "valid bootstrap", 15, password, &length));
    TEST_ASSERT_EQUAL_UINT(1, io.writes);
    TEST_ASSERT_EQUAL_UINT(0, io.commits);
}

void test_bootstrap_commit_failure_disables_and_does_not_retry_or_rewrite() {
    MemoryCredentials io;
    io.commit_result = false;
    char password[65];
    size_t length = sizeof(password);
    TEST_ASSERT_FALSE(admin_load_credential(io, "valid bootstrap", 15, password, &length));
    TEST_ASSERT_EQUAL_UINT(1, io.writes);
    TEST_ASSERT_EQUAL_UINT(1, io.commits);
}

void test_existing_valid_nvs_wins_without_bootstrap_write() {
    MemoryCredentials io;
    io.read_result = AdminCredentialRead::Found;
    memcpy(io.stored, "existing password", 18);
    io.stored_length = 18;
    char password[65];
    size_t length = sizeof(password);
    TEST_ASSERT_TRUE(admin_load_credential(io, "valid bootstrap", 15, password, &length));
    TEST_ASSERT_EQUAL_STRING("existing password", password);
    TEST_ASSERT_EQUAL_UINT(0, io.writes);
}

void test_password_change_never_activates_when_write_or_commit_fails() {
    MemoryCredentials io;
    io.write_result = false;
    TEST_ASSERT_FALSE(admin_store_credential(io, "replacement pass", 16));
    TEST_ASSERT_EQUAL_UINT(1, io.writes);
    TEST_ASSERT_EQUAL_UINT(0, io.commits);
    io.write_result = true;
    io.commit_result = false;
    TEST_ASSERT_FALSE(admin_store_credential(io, "replacement pass", 16));
    TEST_ASSERT_EQUAL_UINT(2, io.writes);
    TEST_ASSERT_EQUAL_UINT(1, io.commits);
}

void test_storage_failures_use_the_runtime_admin_and_ota_disable_boundary() {
    MemoryCredentials io;
    DisableRecorder disabled;
    io.read_result = AdminCredentialRead::Error;
    char password[65];
    size_t length = sizeof(password);
    TEST_ASSERT_FALSE(admin_load_credential_or_disable(
        io, "valid bootstrap", 15, password, &length, disabled));
    TEST_ASSERT_EQUAL_UINT(1, disabled.disables);
    io.read_result = AdminCredentialRead::NotFound;
    io.write_result = false;
    TEST_ASSERT_FALSE(admin_store_credential_or_disable(io, "replacement pass", 16, disabled));
    TEST_ASSERT_EQUAL_UINT(2, disabled.disables);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_corrupt_existing_nvs_disables_without_rewrite);
    RUN_TEST(test_bootstrap_write_failure_disables_without_commit);
    RUN_TEST(test_bootstrap_commit_failure_disables_and_does_not_retry_or_rewrite);
    RUN_TEST(test_existing_valid_nvs_wins_without_bootstrap_write);
    RUN_TEST(test_password_change_never_activates_when_write_or_commit_fails);
    RUN_TEST(test_storage_failures_use_the_runtime_admin_and_ota_disable_boundary);
    return UNITY_END();
}
