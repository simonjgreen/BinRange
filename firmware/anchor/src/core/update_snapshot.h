#pragma once

#include "core/update_job.h"

constexpr size_t UPDATE_SNAPSHOT_MAX_ASSOCIATIONS = 16;
constexpr size_t UPDATE_SNAPSHOT_MAX_ENCODED_SIZE = 3832;

struct UpdateAssociation {
    UpdateTarget target;
    // Human-readable address order, MSB first: D1:2A:83:8C:F0:B4 starts D1.
    uint8_t address[6];
    uint8_t address_type;  // 0 public, 1 random static (address[0] top bits 11).
};

// Fixed caller-owned storage. Jobs and last_wall_seconds are parallel arrays;
// only entries below their counts are meaningful. No uptime is persisted.
struct UpdateSnapshot {
    uint32_t restart_count;
    size_t association_count;
    size_t job_count;
    UpdateAssociation associations[UPDATE_SNAPSHOT_MAX_ASSOCIATIONS];
    UpdateJobState jobs[UPDATE_QUEUE_MAX];
    uint64_t last_wall_seconds[UPDATE_QUEUE_MAX];  // Unix seconds, 0 unknown.
};

// v1 wire layout: all integers unsigned little-endian; offsets in bytes.
// Header (24): 0 magic "USNP"; 4 schema u16=1; 6 header size u16=24;
// 8 exact total size u32; 12 restart_count u32; 16 association_count u8;
// 17 job_count u8; 18 reserved u16=0; 20 CRC32 u32.
// Then association_count records (26): 0 tag u16; 2 hardware ID 16 ASCII
// lowercase hex bytes (no NUL); 18 address[6]; 24 address_type u8; 25 zero.
// Then job_count records (212): 0 ID u32; 4 tag u16; 6 hardware ID[16];
// 22 phase u8 (Queued=0, Waiting=1, Connecting=2, Uploading=3, Rebooting=4,
// Checking=5, Successful=6, Failed=7, Cancelled=8, NeedsRelease=9,
// NeedsAction=10); 23 attempts u8; 24 trial flag u8 (0/1); 25..27 zero;
// 28 acknowledged u32; 32 release size u32; 36 file SHA[32];
// 68 image hash[32]; 100 version[24]; 124 error[80]; 204 wall seconds u64.
// Strings include a NUL and zero padding thereafter; version is nonempty.
// CRC is IEEE reflected 0xedb88320, init/final xor 0xffffffff, over bytes
// [0,20) followed by [24,total_size): header/version/counts are protected.
// Maximum length = 24 + 16*26 + 16*212 = 3832. No struct dumps or host sizes.
// Unknown schemas, flags, reserved data, padding and trailing bytes fail.
// Counts are bounded before size arithmetic. A valid empty snapshot succeeds.
// Live jobs require an exact association; history may use old associations.
// Association tag, hardware ID and BLE (address,type) must each be unique.
// All functions are pure: no heap, storage, radio or runtime side effects.
uint32_t update_snapshot_crc32(const uint8_t *bytes, size_t length);
// On failure written=0 and the buffer is untouched. Output buffers must not
// overlap input objects. Encoder canonicalizes string padding to zero.
bool update_snapshot_encode(const UpdateSnapshot &snapshot, uint8_t *output,
                            size_t capacity, size_t &written);
// On failure output is untouched. Successful decode preserves saved policy
// phases/offsets; explicitly call queue.restore(jobs, job_count, boot_now) to
// apply restart policy. Output must not overlap input bytes.
bool update_snapshot_decode(const uint8_t *bytes, size_t length,
                            UpdateSnapshot &output);
