#include "core/update_image.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kImageMagic = 0x96f3b83d;
constexpr uint16_t kRegularTlvMagic = 0x6907;
constexpr uint16_t kProtectedTlvMagic = 0x6908;
constexpr uint16_t kSha256Type = 0x10;
constexpr uint16_t kKeyhashType = 0x01;
constexpr uint16_t kEcdsaP256Type = 0x22;
constexpr size_t kHeaderMinimum = 32;
constexpr uint32_t kMaximumSignedSize = 212992;

uint16_t read16(const uint8_t *data, size_t off) {
    return static_cast<uint16_t>(data[off]) |
           (static_cast<uint16_t>(data[off + 1]) << 8);
}

uint32_t read32(const uint8_t *data, size_t off) {
    return static_cast<uint32_t>(data[off]) |
           (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

bool valid_ecdsa_p256_der(const uint8_t *value, size_t length) {
    if (length < 8 || length > 72 || value[0] != 0x30 ||
        value[1] != length - 2 || value[1] >= 0x80)
        return false;
    size_t offset = 2;
    for (int integer = 0; integer < 2; ++integer) {
        if (offset >= length || value[offset++] != 0x02 || offset >= length)
            return false;
        const size_t integer_length = value[offset++];
        if (integer_length == 0 || integer_length > 33 ||
            integer_length > length - offset)
            return false;
        const uint8_t first = value[offset];
        if ((first & 0x80) != 0 ||
            (first == 0 && (integer_length == 1 ||
                            (value[offset + 1] & 0x80) == 0)))
            return false;
        offset += integer_length;
    }
    return offset == length;
}

bool parse_tlv_area(const uint8_t *data, size_t area_size, uint16_t magic,
                    bool require_release_tlvs, ImageInfo &out) {
    if (area_size < 4 || read16(data, 0) != magic || read16(data, 2) != area_size)
        return false;
    bool found_sha = false;
    bool found_keyhash = false;
    bool found_signature = false;
    size_t offset = 4;
    while (offset < area_size) {
        if (area_size - offset < 4)
            return false;
        const uint16_t type = read16(data, offset);
        const uint16_t value_size = read16(data, offset + 2);
        offset += 4;
        if (value_size > area_size - offset)
            return false;
        const uint8_t *value = data + offset;
        if (require_release_tlvs && type == kSha256Type) {
            if (found_sha || value_size != sizeof(out.image_hash)) return false;
            std::memcpy(out.image_hash, value, sizeof(out.image_hash));
            found_sha = true;
        } else if (require_release_tlvs && type == kKeyhashType) {
            if (found_keyhash || value_size != 32) return false;
            found_keyhash = true;
        } else if (require_release_tlvs && type == kEcdsaP256Type) {
            if (found_signature || !valid_ecdsa_p256_der(value, value_size)) return false;
            found_signature = true;
        }
        offset += value_size;
    }
    return !require_release_tlvs || (found_sha && found_keyhash && found_signature);
}

bool terminated(const char *text, size_t size) {
    for (size_t i = 0; i < size; ++i)
        if (text[i] == '\0') return true;
    return false;
}

}  // namespace

bool parse_update_image(const uint8_t *data, size_t size, ImageInfo &out) {
    std::memset(&out, 0, sizeof(out));
    if (!data || size < kHeaderMinimum || read32(data, 0) != kImageMagic ||
        read32(data, 4) != 0 || read32(data, 16) != 0)
        return false;

    const uint16_t header_size = read16(data, 8);
    const uint16_t protected_size = read16(data, 10);
    const uint32_t payload_size = read32(data, 12);
    if (header_size < kHeaderMinimum || header_size > size || payload_size == 0)
        return false;
    size_t remaining = size - header_size;
    if (payload_size > remaining) return false;
    const size_t payload_end = static_cast<size_t>(header_size) + payload_size;
    remaining -= payload_size;
    if (protected_size > remaining) return false;
    const size_t signed_size = payload_end + protected_size;
    if (size > kMaximumSignedSize || signed_size > kMaximumSignedSize ||
        signed_size > size)
        return false;

    if (protected_size != 0 &&
        !parse_tlv_area(data + payload_end, protected_size, kProtectedTlvMagic,
                        false, out)) {
        std::memset(&out, 0, sizeof(out));
        return false;
    }
    const size_t regular_size = size - signed_size;
    if (!parse_tlv_area(data + signed_size, regular_size, kRegularTlvMagic, true,
                        out)) {
        std::memset(&out, 0, sizeof(out));
        return false;
    }

    const int formatted = std::snprintf(
        out.version, sizeof(out.version), "%u.%u.%u+%lu",
        static_cast<unsigned int>(data[20]), static_cast<unsigned int>(data[21]),
        static_cast<unsigned int>(read16(data, 22)),
        static_cast<unsigned long>(read32(data, 24)));
    if (formatted < 0 || static_cast<size_t>(formatted) >= sizeof(out.version)) {
        std::memset(&out, 0, sizeof(out));
        return false;
    }
    out.signed_region_size = static_cast<uint32_t>(signed_size);
    return true;
}

bool validate_release(const ImageInfo &parsed, uint32_t actual_size,
                      const uint8_t actual_file_sha[32],
                      const uint8_t actual_image_sha[32],
                      const UpdateRelease &expected) {
    if (!actual_file_sha || !actual_image_sha ||
        !terminated(parsed.version, sizeof(parsed.version)) ||
        !terminated(expected.version, sizeof(expected.version)) ||
        actual_size != expected.size ||
        std::memcmp(actual_file_sha, expected.file_sha, sizeof(expected.file_sha)) != 0 ||
        std::memcmp(actual_image_sha, parsed.image_hash, sizeof(parsed.image_hash)) != 0 ||
        std::memcmp(actual_image_sha, expected.image_hash,
                    sizeof(expected.image_hash)) != 0 ||
        std::strcmp(parsed.version, expected.version) != 0)
        return false;
    return true;
}
