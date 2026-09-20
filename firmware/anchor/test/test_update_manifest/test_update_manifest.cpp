#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "core/update_manifest.h"

namespace {

const char kManifest[] =
    "{\"schema\":1,\"target\":\"k4w/nrf52833/dw3110\",\"size\":1024,"
    "\"file_sha256\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\","
    "\"image_hash\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\","
    "\"version\":\"1.2.3+4\"}";

void assert_cleared(const UpdateRelease &release) {
    const UpdateRelease zero = {};
    TEST_ASSERT_EQUAL_MEMORY(&zero, &release, sizeof(release));
}

}  // namespace

void test_manifest_decodes_generated_shape() {
    UpdateRelease release = {};
    TEST_ASSERT_TRUE(parse_update_manifest(kManifest, sizeof(kManifest) - 1, release));
    TEST_ASSERT_EQUAL_UINT32(1024, release.size);
    TEST_ASSERT_EQUAL_UINT8(0x00, release.file_sha[0]);
    TEST_ASSERT_EQUAL_UINT8(0xff, release.image_hash[0]);
    TEST_ASSERT_EQUAL_STRING("1.2.3+4", release.version);
}

void test_manifest_accepts_reordered_fields_and_whitespace() {
    const char json[] =
        " \n { \"version\" : \"2.0+1\" , \"image_hash\" : "
        "\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\" ,"
        "\"size\" : 32 , \"target\" : \"k4w/nrf52833/dw3110\" ,"
        "\"file_sha256\" : \"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\" ,"
        "\"schema\" : 1 } \t";
    UpdateRelease release = {};
    TEST_ASSERT_TRUE(parse_update_manifest(json, sizeof(json) - 1, release));
    TEST_ASSERT_EQUAL_UINT32(32, release.size);
    TEST_ASSERT_EQUAL_STRING("2.0+1", release.version);
}

void test_manifest_rejects_wrong_target_or_schema() {
    const char wrong_target[] =
        "{\"schema\":1,\"target\":\"k4w/nrf52833/other\",\"size\":1024,"
        "\"file_sha256\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\","
        "\"image_hash\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\",\"version\":\"1\"}";
    const char wrong_schema[] =
        "{\"schema\":2,\"target\":\"k4w/nrf52833/dw3110\",\"size\":1024,"
        "\"file_sha256\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\","
        "\"image_hash\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\",\"version\":\"1\"}";
    for (const char *json : {wrong_target, wrong_schema}) {
        UpdateRelease release = {99, {1}, {1}, "stale"};
        TEST_ASSERT_FALSE(parse_update_manifest(json, std::strlen(json), release));
        assert_cleared(release);
    }
}

void test_manifest_rejects_duplicate_and_truncated_objects() {
    const char duplicate[] =
        "{\"schema\":1,\"schema\":1,\"target\":\"k4w/nrf52833/dw3110\",\"size\":1024,"
        "\"file_sha256\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\","
        "\"image_hash\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\",\"version\":\"1\"}";
    for (const char *json : {duplicate, "{\"schema\":1"}) {
        UpdateRelease release = {99, {1}, {1}, "stale"};
        TEST_ASSERT_FALSE(parse_update_manifest(json, std::strlen(json), release));
        assert_cleared(release);
    }
}

void test_manifest_rejects_numeric_overflow_and_bad_hash() {
    const char overflow[] =
        "{\"schema\":1,\"target\":\"k4w/nrf52833/dw3110\",\"size\":4294967296,"
        "\"file_sha256\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\","
        "\"image_hash\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\",\"version\":\"1\"}";
    const char bad_hash[] =
        "{\"schema\":1,\"target\":\"k4w/nrf52833/dw3110\",\"size\":1024,"
        "\"file_sha256\":\"00112233445566778899Aabbccddeeff00112233445566778899aabbccddeeff\","
        "\"image_hash\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\",\"version\":\"1\"}";
    for (const char *json : {overflow, bad_hash}) {
        UpdateRelease release = {99, {1}, {1}, "stale"};
        TEST_ASSERT_FALSE(parse_update_manifest(json, std::strlen(json), release));
        assert_cleared(release);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_manifest_decodes_generated_shape);
    RUN_TEST(test_manifest_accepts_reordered_fields_and_whitespace);
    RUN_TEST(test_manifest_rejects_wrong_target_or_schema);
    RUN_TEST(test_manifest_rejects_duplicate_and_truncated_objects);
    RUN_TEST(test_manifest_rejects_numeric_overflow_and_bad_hash);
    return UNITY_END();
}
