#include <unity.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/update_image.h"

namespace {

void put16(std::vector<uint8_t> &bytes, size_t off, uint16_t value) {
    bytes[off] = static_cast<uint8_t>(value);
    bytes[off + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::vector<uint8_t> &bytes, size_t off, uint32_t value) {
    for (size_t i = 0; i < 4; ++i)
        bytes[off + i] = static_cast<uint8_t>(value >> (8 * i));
}

void append16(std::vector<uint8_t> &bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void append_tlv(std::vector<uint8_t> &bytes, uint16_t type,
                const std::vector<uint8_t> &value) {
    append16(bytes, type);
    append16(bytes, static_cast<uint16_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<uint8_t> sha(uint8_t value = 0xa5) {
    return std::vector<uint8_t>(32, value);
}

std::vector<uint8_t> signature() {
    return {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
}

std::vector<uint8_t> make_image(uint16_t header_size = 32,
                                uint32_t payload_size = 32,
                                const std::vector<uint8_t> &protected_values = {},
                                bool include_sha = true, bool include_keyhash = true,
                                bool include_signature = true) {
    std::vector<uint8_t> protected_tlvs;
    if (!protected_values.empty()) {
        append16(protected_tlvs, 0x6908);
        append16(protected_tlvs,
                 static_cast<uint16_t>(4 + protected_values.size() + 4));
        append_tlv(protected_tlvs, 0x60, protected_values);
    }
    std::vector<uint8_t> image(header_size + payload_size, 0);
    put32(image, 0, 0x96f3b83d);
    put16(image, 8, header_size);
    put16(image, 10, static_cast<uint16_t>(protected_tlvs.size()));
    put32(image, 12, payload_size);
    image[20] = 1;
    image[21] = 2;
    put16(image, 22, 3);
    put32(image, 24, 4);
    image.insert(image.end(), protected_tlvs.begin(), protected_tlvs.end());

    std::vector<uint8_t> regular;
    append16(regular, 0x6907);
    append16(regular, 0);
    if (include_sha) append_tlv(regular, 0x10, sha());
    if (include_keyhash) append_tlv(regular, 0x01, sha(0xb6));
    if (include_signature) append_tlv(regular, 0x22, signature());
    put16(regular, 2, static_cast<uint16_t>(regular.size()));
    image.insert(image.end(), regular.begin(), regular.end());
    return image;
}

void assert_reset(const ImageInfo &info) {
    TEST_ASSERT_EQUAL_UINT32(0, info.signed_region_size);
    for (size_t i = 0; i < sizeof(info.image_hash); ++i)
        TEST_ASSERT_EQUAL_UINT8(0, info.image_hash[i]);
    TEST_ASSERT_EQUAL_UINT8(0, info.version[0]);
}

ImageInfo parsed_image(const std::vector<uint8_t> &image) {
    ImageInfo info = {};
    TEST_ASSERT_TRUE(parse_update_image(image.data(), image.size(), info));
    return info;
}

UpdateRelease expected_release(const ImageInfo &info) {
    UpdateRelease release = {};
    release.size = info.signed_region_size + 88;
    std::memset(release.file_sha, 0x11, sizeof(release.file_sha));
    std::memcpy(release.image_hash, info.image_hash, sizeof(release.image_hash));
    std::strcpy(release.version, info.version);
    return release;
}

}  // namespace

void test_parse_accepts_32_and_512_byte_headers() {
    for (uint16_t header_size : {static_cast<uint16_t>(32), static_cast<uint16_t>(512)}) {
        const std::vector<uint8_t> image = make_image(header_size);
        const ImageInfo info = parsed_image(image);
        TEST_ASSERT_EQUAL_UINT32(header_size + 32, info.signed_region_size);
        TEST_ASSERT_EQUAL_STRING("1.2.3+4", info.version);
        TEST_ASSERT_EQUAL_UINT8(0xa5, info.image_hash[0]);
    }
}

void test_parse_accepts_bounded_protected_tlvs() {
    const std::vector<uint8_t> image = make_image(32, 32, {0x12, 0x34});
    ImageInfo info = parsed_image(image);
    TEST_ASSERT_EQUAL_UINT32(74, info.signed_region_size);
}

void test_parse_rejects_null_and_short_input_and_resets_output() {
    ImageInfo info = {99, {1}, "stale"};
    TEST_ASSERT_FALSE(parse_update_image(nullptr, 32, info));
    assert_reset(info);
    const std::vector<uint8_t> short_image(31, 0);
    TEST_ASSERT_FALSE(parse_update_image(short_image.data(), short_image.size(), info));
    assert_reset(info);
}

void test_parse_rejects_bad_header_modes_and_zero_payload() {
    const std::vector<uint8_t> valid = make_image();
    for (const size_t offset : {static_cast<size_t>(0), static_cast<size_t>(4),
                                static_cast<size_t>(8), static_cast<size_t>(16)}) {
        std::vector<uint8_t> image = valid;
        image[offset] ^= 1;
        ImageInfo info = {};
        TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
        assert_reset(info);
    }
    std::vector<uint8_t> zero_payload = valid;
    put32(zero_payload, 12, 0);
    ImageInfo info = {};
    TEST_ASSERT_FALSE(parse_update_image(zero_payload.data(), zero_payload.size(), info));
}

void test_parse_rejects_payload_overflow_outside_image_and_oversized_signed_region() {
    std::vector<uint8_t> image = make_image();
    put32(image, 12, 0xffffffffU);
    ImageInfo info = {};
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));

    image = make_image();
    put32(image, 12, 1000);
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));

    image = make_image(32, 212961);
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
}

void test_parse_enforces_total_image_capacity_including_regular_tlvs() {
    std::vector<uint8_t> image = make_image(32, 212872);
    TEST_ASSERT_EQUAL_UINT32(212992, image.size());
    ImageInfo info = {};
    TEST_ASSERT_TRUE(parse_update_image(image.data(), image.size(), info));

    image = make_image(32, 212873);
    TEST_ASSERT_EQUAL_UINT32(212993, image.size());
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
}

void test_parse_rejects_missing_or_duplicate_required_tlvs() {
    for (int missing = 0; missing < 3; ++missing) {
        const std::vector<uint8_t> image = make_image(32, 32, {}, missing != 0,
                                                      missing != 1, missing != 2);
        ImageInfo info = {};
        TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
    }
    const size_t regular = 64;
    for (uint16_t duplicate_type : {static_cast<uint16_t>(0x10),
                                    static_cast<uint16_t>(0x01),
                                    static_cast<uint16_t>(0x22)}) {
        std::vector<uint8_t> image = make_image();
        std::vector<uint8_t> entry;
        append_tlv(entry, duplicate_type,
                   duplicate_type == 0x22 ? signature() : sha(0xc7));
        put16(image, regular + 2, static_cast<uint16_t>(88 + entry.size()));
        image.insert(image.end(), entry.begin(), entry.end());
        ImageInfo info = {};
        TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
    }
}

void test_parse_rejects_malformed_der_and_tlv_bounds_and_trailing_junk() {
    std::vector<uint8_t> image = make_image();
    image.back() = 0;
    ImageInfo info = {99, {1}, "stale"};
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
    assert_reset(info);

    image = make_image();
    put16(image, 66, 0xffff);
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));

    image = make_image();
    put16(image, 64 + 4 + 2, 0xffff);
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));

    image = make_image();
    image.push_back(0);
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
}

void test_parse_rejects_protected_total_mismatch() {
    std::vector<uint8_t> image = make_image(32, 32, {0x12, 0x34});
    put16(image, 64 + 2, 9);
    ImageInfo info = {};
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
}

void test_parse_accepts_23_and_rejects_24_character_versions() {
    std::vector<uint8_t> image = make_image();
    image[20] = 255;
    image[21] = 255;
    put16(image, 22, 65535);
    put32(image, 24, 999999999);
    ImageInfo info = {};
    TEST_ASSERT_TRUE(parse_update_image(image.data(), image.size(), info));
    TEST_ASSERT_EQUAL_STRING("255.255.65535+999999999", info.version);

    put32(image, 24, 0xffffffffU);
    std::memset(&info, 1, sizeof(info));
    TEST_ASSERT_FALSE(parse_update_image(image.data(), image.size(), info));
    assert_reset(info);
}

void test_validate_requires_exact_hashes_size_and_version() {
    const std::vector<uint8_t> image = make_image();
    ImageInfo info = parsed_image(image);
    UpdateRelease release = expected_release(info);
    uint8_t file_sha[32];
    uint8_t computed_hash[32];
    std::memset(file_sha, 0x11, sizeof(file_sha));
    std::memcpy(computed_hash, info.image_hash, sizeof(computed_hash));
    TEST_ASSERT_TRUE(validate_release(info, static_cast<uint32_t>(image.size()), file_sha,
                                      computed_hash, release));
    file_sha[0] ^= 1;
    TEST_ASSERT_FALSE(validate_release(info, static_cast<uint32_t>(image.size()), file_sha,
                                       computed_hash, release));
    file_sha[0] ^= 1;
    release.size--;
    TEST_ASSERT_FALSE(validate_release(info, static_cast<uint32_t>(image.size()), file_sha,
                                       computed_hash, release));
    release.size++;
    release.image_hash[0] ^= 1;
    TEST_ASSERT_FALSE(validate_release(info, static_cast<uint32_t>(image.size()), file_sha,
                                       computed_hash, release));
    release.image_hash[0] ^= 1;
    info.image_hash[0] ^= 1;
    TEST_ASSERT_FALSE(validate_release(info, static_cast<uint32_t>(image.size()), file_sha,
                                       computed_hash, release));
    info.image_hash[0] ^= 1;
    release.version[0] = '9';
    TEST_ASSERT_FALSE(validate_release(info, static_cast<uint32_t>(image.size()), file_sha,
                                       computed_hash, release));
}

void test_validate_rejects_null_hashes_and_unterminated_expected_version() {
    const std::vector<uint8_t> image = make_image();
    const ImageInfo info = parsed_image(image);
    UpdateRelease release = expected_release(info);
    uint8_t file_sha[32];
    std::memset(file_sha, 0x11, sizeof(file_sha));
    TEST_ASSERT_FALSE(validate_release(info, static_cast<uint32_t>(image.size()), nullptr,
                                       info.image_hash, release));
    TEST_ASSERT_FALSE(validate_release(info, static_cast<uint32_t>(image.size()), file_sha,
                                       nullptr, release));
    std::memset(release.version, 'x', sizeof(release.version));
    TEST_ASSERT_FALSE(validate_release(info, static_cast<uint32_t>(image.size()), file_sha,
                                       info.image_hash, release));
}

void test_archived_c_fixture_matches_expected_parser_metadata_when_available() {
    const char *paths[] = {
        "local-backups/k4w-first-tag/recovery-c-0.2.2/zephyr.signed.bin",
        "../../local-backups/k4w-first-tag/recovery-c-0.2.2/zephyr.signed.bin",
    };
    std::ifstream fixture;
    for (const char *path : paths) {
        fixture.open(path, std::ios::binary);
        if (fixture.is_open()) break;
        fixture.clear();
    }
    // clear() resets stream flags, not open state. A failed final open must
    // not masquerade as an available zero-byte image after clearing failbit.
    if (!fixture.is_open()) TEST_IGNORE_MESSAGE("Private signed-image fixture not installed");
    const std::vector<uint8_t> image((std::istreambuf_iterator<char>(fixture)),
                                     std::istreambuf_iterator<char>());
    const ImageInfo info = parsed_image(image);
    const uint8_t expected_hash[32] = {
        0x41, 0xf7, 0xb2, 0xff, 0x41, 0x8f, 0x0c, 0x57,
        0x0d, 0x41, 0x3b, 0x55, 0x48, 0xcb, 0xb1, 0x28,
        0x32, 0x18, 0x71, 0x50, 0x19, 0x90, 0x77, 0xa7,
        0x92, 0x67, 0x05, 0xfa, 0x6f, 0xd8, 0xcc, 0x4f,
    };
    TEST_ASSERT_EQUAL_UINT32(203120, info.signed_region_size);
    TEST_ASSERT_EQUAL_STRING("0.2.2+0", info.version);
    TEST_ASSERT_EQUAL_MEMORY(expected_hash, info.image_hash, sizeof(expected_hash));
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_accepts_32_and_512_byte_headers);
    RUN_TEST(test_parse_accepts_bounded_protected_tlvs);
    RUN_TEST(test_parse_rejects_null_and_short_input_and_resets_output);
    RUN_TEST(test_parse_rejects_bad_header_modes_and_zero_payload);
    RUN_TEST(test_parse_rejects_payload_overflow_outside_image_and_oversized_signed_region);
    RUN_TEST(test_parse_enforces_total_image_capacity_including_regular_tlvs);
    RUN_TEST(test_parse_rejects_missing_or_duplicate_required_tlvs);
    RUN_TEST(test_parse_rejects_malformed_der_and_tlv_bounds_and_trailing_junk);
    RUN_TEST(test_parse_rejects_protected_total_mismatch);
    RUN_TEST(test_parse_accepts_23_and_rejects_24_character_versions);
    RUN_TEST(test_validate_requires_exact_hashes_size_and_version);
    RUN_TEST(test_validate_rejects_null_hashes_and_unterminated_expected_version);
    RUN_TEST(test_archived_c_fixture_matches_expected_parser_metadata_when_available);
    return UNITY_END();
}
