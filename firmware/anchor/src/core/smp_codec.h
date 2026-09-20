#pragma once

#include <cstddef>
#include <cstdint>

enum class SmpCommand { ImageList, TagStatus, Upload, Trial, Reset };
enum class SmpFeed { More, Complete, Error };
enum class SmpOutcome { Ok, Denied };

struct SmpUpload {
    uint32_t offset, total;
    const uint8_t* file_sha;
    const uint8_t* data;
    size_t size;
};
struct SmpImage {
    uint8_t slot;
    char version[24];
    uint8_t hash[32];
    bool bootable, pending, confirmed, active, permanent;
};
struct SmpTag {
    char id[17], version[24];
    uint16_t tag;
    uint64_t uptime_ms;
    uint32_t reset_reason;
    bool confirmed, maintenance, radio_ok, ble_ok;
};
struct SmpReply {
    SmpOutcome outcome;
    uint16_t error_group;
    uint32_t error_code;
    SmpTag tag;
    SmpImage images[2];
    size_t image_count;
    uint32_t offset;
};

// No writes to out on failure; written is always zero on failure.
bool smp_encode_request(SmpCommand command, uint8_t sequence,
                        const SmpUpload* upload, uint8_t* out,
                        size_t capacity, size_t& written);

class SmpAssembler {
public:
    SmpAssembler();
    bool begin(SmpCommand command, uint8_t sequence);
    SmpFeed feed(const uint8_t* bytes, size_t size);
    // Valid only after Complete, and only until begin/abort/feed. Copy fields
    // needed by the caller before reusing the assembler for another exchange.
    const SmpReply* reply() const;
    // Timeout, disconnect and cancellation must explicitly clear this session.
    void abort();
private:
    enum class State { Idle, Receiving, Complete, Failed };
    State state_;
    SmpCommand command_;
    uint8_t sequence_;
    uint8_t buffer_[4096];
    size_t used_;
    SmpReply reply_;
    SmpFeed fail();
};

bool smp_accept_upload_offset(uint32_t reply, uint32_t sent_offset,
                              uint32_t sent_end, uint32_t total,
                              bool first, uint8_t& stalled);
