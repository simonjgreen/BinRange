#include "core/update_snapshot.h"

#include <cstring>

namespace {
constexpr size_t kHeader = 24;
constexpr size_t kAssociation = 26;
constexpr size_t kJob = 212;

uint32_t crc_byte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    return crc;
}

uint32_t wire_crc(const uint8_t *bytes, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i)
        if (i < 20 || i >= 24) crc = crc_byte(crc, bytes[i]);
    return crc ^ 0xffffffffu;
}

void put(uint8_t *bytes, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) {
        bytes[i] = static_cast<uint8_t>(value);
        value >>= 8;
    }
}

uint64_t get(const uint8_t *bytes, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    return value;
}

bool valid_snapshot(const UpdateSnapshot &s) {
    if (s.association_count > UPDATE_SNAPSHOT_MAX_ASSOCIATIONS ||
        !UpdateQueue::valid_restore(s.jobs, s.job_count)) return false;
    for (size_t i = 0; i < s.association_count; ++i) {
        const UpdateAssociation &a = s.associations[i];
        if (!a.target.tag || a.target.tag == 0xffff || a.target.hardware_id[16] ||
            a.address_type > 1 ||
            (a.address_type == 1 && (a.address[0] & 0xc0) != 0xc0))
            return false;
        for (size_t j = 0; j < 16; ++j) {
            const char c = a.target.hardware_id[j];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        }
        bool zero = true, ff = true;
        for (uint8_t byte : a.address) {
            zero = zero && byte == 0;
            ff = ff && byte == 0xff;
        }
        if (zero || ff) return false;
        for (size_t j = 0; j < i; ++j) {
            const UpdateAssociation &b = s.associations[j];
            if (a.target.tag == b.target.tag ||
                memcmp(a.target.hardware_id, b.target.hardware_id, 16) == 0 ||
                (a.address_type == b.address_type && memcmp(a.address, b.address, 6) == 0))
                return false;
        }
    }
    for (size_t i = 0; i < s.job_count; ++i) {
        if (update_terminal(s.jobs[i].phase)) continue;
        bool matched = false;
        for (size_t j = 0; j < s.association_count; ++j) {
            const UpdateTarget &target = s.associations[j].target;
            if (target.tag == s.jobs[i].target.tag &&
                memcmp(target.hardware_id, s.jobs[i].target.hardware_id, 16) == 0)
                matched = true;
        }
        if (!matched) return false;
    }
    return true;
}

// Destination records are pre-zeroed; only copy bytes preceding the first NUL.
void put_string(uint8_t *out, const char *value, size_t width) {
    for (size_t i = 0; i < width && value[i]; ++i)
        out[i] = static_cast<uint8_t>(value[i]);
}

bool canonical_string(const uint8_t *bytes, size_t width) {
    bool ended = false;
    for (size_t i = 0; i < width; ++i) {
        if (bytes[i] == 0) ended = true;
        else if (ended) return false;
    }
    return ended;
}
}  // namespace

uint32_t update_snapshot_crc32(const uint8_t *bytes, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) crc = crc_byte(crc, bytes[i]);
    return crc ^ 0xffffffffu;
}

bool update_snapshot_encode(const UpdateSnapshot &s, uint8_t *output,
                            size_t capacity, size_t &written) {
    written = 0;
    if (!valid_snapshot(s) || !output) return false;
    // Counts have been bounded to 16, so these products and sum cannot overflow.
    const size_t length = kHeader + s.association_count * kAssociation + s.job_count * kJob;
    if (capacity < length) return false;
    memset(output, 0, length);
    memcpy(output, "USNP", 4);
    put(output + 4, 1, 2);
    put(output + 6, kHeader, 2);
    put(output + 8, length, 4);
    put(output + 12, s.restart_count, 4);
    output[16] = static_cast<uint8_t>(s.association_count);
    output[17] = static_cast<uint8_t>(s.job_count);
    size_t offset = kHeader;
    for (size_t i = 0; i < s.association_count; ++i, offset += kAssociation) {
        const UpdateAssociation &a = s.associations[i];
        uint8_t *p = output + offset;
        put(p, a.target.tag, 2);
        memcpy(p + 2, a.target.hardware_id, 16);
        memcpy(p + 18, a.address, 6);
        p[24] = a.address_type;
    }
    for (size_t i = 0; i < s.job_count; ++i, offset += kJob) {
        const UpdateJobState &j = s.jobs[i];
        uint8_t *p = output + offset;
        put(p, j.id, 4);
        put(p + 4, j.target.tag, 2);
        memcpy(p + 6, j.target.hardware_id, 16);
        p[22] = static_cast<uint8_t>(j.phase);
        p[23] = j.attempts;
        p[24] = j.trial_may_be_armed ? 1 : 0;
        put(p + 28, j.acknowledged, 4);
        put(p + 32, j.release.size, 4);
        memcpy(p + 36, j.release.file_sha, 32);
        memcpy(p + 68, j.release.image_hash, 32);
        put_string(p + 100, j.release.version, 24);
        put_string(p + 124, j.error, 80);
        put(p + 204, s.last_wall_seconds[i], 8);
    }
    put(output + 20, wire_crc(output, length), 4);
    written = length;
    return true;
}

bool update_snapshot_decode(const uint8_t *bytes, size_t length,
                            UpdateSnapshot &output) {
    if (!bytes || length < kHeader || length > UPDATE_SNAPSHOT_MAX_ENCODED_SIZE ||
        memcmp(bytes, "USNP", 4) != 0 ||
        get(bytes + 4, 2) != 1 || get(bytes + 6, 2) != kHeader ||
        get(bytes + 8, 4) != length || bytes[16] > UPDATE_SNAPSHOT_MAX_ASSOCIATIONS ||
        bytes[17] > UPDATE_QUEUE_MAX || get(bytes + 18, 2) != 0 ||
        length != kHeader + bytes[16] * kAssociation + bytes[17] * kJob ||
        get(bytes + 20, 4) != wire_crc(bytes, length))
        return false;
    UpdateSnapshot decoded = {};
    decoded.restart_count = static_cast<uint32_t>(get(bytes + 12, 4));
    decoded.association_count = bytes[16];
    decoded.job_count = bytes[17];
    size_t offset = kHeader;
    for (size_t i = 0; i < decoded.association_count; ++i, offset += kAssociation) {
        const uint8_t *p = bytes + offset;
        if (p[25]) return false;
        UpdateAssociation &a = decoded.associations[i];
        a.target.tag = static_cast<uint16_t>(get(p, 2));
        memcpy(a.target.hardware_id, p + 2, 16);
        memcpy(a.address, p + 18, 6);
        a.address_type = p[24];
    }
    for (size_t i = 0; i < decoded.job_count; ++i, offset += kJob) {
        const uint8_t *p = bytes + offset;
        if (p[24] > 1 || p[25] || p[26] || p[27] ||
            !canonical_string(p + 100, 24) || !canonical_string(p + 124, 80))
            return false;
        UpdateJobState &j = decoded.jobs[i];
        j.id = static_cast<uint32_t>(get(p, 4));
        j.target.tag = static_cast<uint16_t>(get(p + 4, 2));
        memcpy(j.target.hardware_id, p + 6, 16);
        j.phase = static_cast<UpdatePhase>(p[22]);
        j.attempts = p[23];
        j.trial_may_be_armed = p[24] != 0;
        j.acknowledged = static_cast<uint32_t>(get(p + 28, 4));
        j.release.size = static_cast<uint32_t>(get(p + 32, 4));
        memcpy(j.release.file_sha, p + 36, 32);
        memcpy(j.release.image_hash, p + 68, 32);
        memcpy(j.release.version, p + 100, 24);
        memcpy(j.error, p + 124, 80);
        decoded.last_wall_seconds[i] = get(p + 204, 8);
    }
    if (!valid_snapshot(decoded)) return false;
    output = decoded;
    return true;
}
