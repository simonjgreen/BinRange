#pragma once

#include <cstddef>
#include <cstdint>

#include "core/update_job.h"

// Successful parsing resets and fills every field. Any failure resets out to
// zeroes, including an empty version, so callers never retain stale metadata.
struct ImageInfo {
    uint32_t signed_region_size;
    uint8_t image_hash[32];
    char version[24];
};

// Validates an uncompressed, directly executable MCUboot image structurally.
// It extracts its SHA-256 TLV but deliberately does not verify its signature.
bool parse_update_image(const uint8_t *data, size_t size, ImageInfo &out);

// Binds a parsed image to caller-computed file and signed-region SHA-256
// values and an expected public release record.
bool validate_release(const ImageInfo &parsed, uint32_t actual_size,
                      const uint8_t actual_file_sha[32],
                      const uint8_t actual_image_sha[32],
                      const UpdateRelease &expected);
