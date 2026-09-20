#include "smp_codec.h"

#include <cstring>

namespace {
constexpr uint32_t MaxImage = 212992;

struct WireCommand { uint8_t op; uint16_t group; uint8_t id; };
bool wire_command(SmpCommand command, WireCommand& wire) {
    switch (command) {
    case SmpCommand::ImageList: wire = {0, 1, 0}; return true;
    case SmpCommand::TagStatus: wire = {0, 64, 0}; return true;
    case SmpCommand::Upload: wire = {2, 1, 1}; return true;
    case SmpCommand::Trial: wire = {2, 64, 1}; return true;
    case SmpCommand::Reset: wire = {2, 0, 5}; return true;
    case SmpCommand::ConfigRead: wire = {0, 64, 2}; return true;
    case SmpCommand::ConfigWrite: wire = {2, 64, 2}; return true;
    }
    return false;
}

// The validated upload limits bound this local encoder well below 512 bytes.
struct Encoder {
    uint8_t bytes[512]{};
    size_t size = 8;
    void number(uint8_t major, uint32_t n) {
        if (n < 24) { bytes[size++] = static_cast<uint8_t>((major << 5) | n); return; }
        const unsigned width = n <= 0xff ? 1 : n <= 0xffff ? 2 : 4;
        bytes[size++] = static_cast<uint8_t>((major << 5) | (width == 1 ? 24 : width == 2 ? 25 : 26));
        for (unsigned i = width; i > 0; --i)
            bytes[size++] = static_cast<uint8_t>(n >> ((i - 1) * 8));
    }
    void blob(uint8_t major, const void* data, size_t count) {
        number(major, static_cast<uint32_t>(count));
        std::memcpy(bytes + size, data, count);
        size += count;
    }
    void key(const char* text) { blob(3, text, std::strlen(text)); }
};
} // namespace

bool smp_encode_request(SmpCommand command, uint8_t sequence,
                        const SmpUpload* upload, uint8_t* out,
                        size_t capacity, size_t& written) {
    written = 0;
    WireCommand wire{};
    if (!wire_command(command, wire) || !out || command == SmpCommand::ConfigWrite ||
        (command == SmpCommand::Upload) != (upload != nullptr)) return false;
    if (upload && (upload->total < 32 || upload->total > MaxImage ||
        !upload->data || upload->size == 0 || upload->size > 256 ||
        upload->offset > upload->total || upload->size > upload->total - upload->offset ||
        (upload->offset == 0 && (!upload->file_sha || upload->size < 32)))) return false;
    Encoder e;
    if (!upload) e.number(5, 0);
    else {
        e.number(5, upload->offset == 0 ? 4 : 2);
        e.key("off"); e.number(0, upload->offset);
        e.key("data"); e.blob(2, upload->data, upload->size);
        if (upload->offset == 0) {
            e.key("len"); e.number(0, upload->total);
            e.key("sha"); e.blob(2, upload->file_sha, 32);
        }
    }
    if (e.size > capacity || e.size > sizeof(e.bytes)) return false;
    e.bytes[0] = wire.op;
    e.bytes[2] = static_cast<uint8_t>((e.size - 8) >> 8);
    e.bytes[3] = static_cast<uint8_t>(e.size - 8);
    e.bytes[4] = static_cast<uint8_t>(wire.group >> 8);
    e.bytes[5] = static_cast<uint8_t>(wire.group);
    e.bytes[6] = sequence;
    e.bytes[7] = wire.id;
    std::memcpy(out, e.bytes, e.size);
    written = e.size;
    return true;
}

bool smp_motion_valid(const SmpMotionConfig &c) {
    return c.moving_ms >= 1000 && c.moving_ms <= 60000 &&
        c.idle_ms >= 60000 && c.idle_ms <= 3600000 && c.idle_ms >= c.moving_ms &&
        c.quiet_ms >= 5000 && c.quiet_ms <= 300000 &&
        c.threshold_mg >= 32 && c.threshold_mg <= 1000 &&
        c.duration_samples >= 1 && c.duration_samples <= 127;
}
bool smp_motion_equal(const SmpMotionConfig &a, const SmpMotionConfig &b) {
    return a.moving_ms == b.moving_ms && a.idle_ms == b.idle_ms &&
        a.quiet_ms == b.quiet_ms && a.threshold_mg == b.threshold_mg &&
        a.duration_samples == b.duration_samples;
}
bool smp_encode_config(uint8_t sequence, const SmpMotionConfig &c,
                       uint8_t *out, size_t capacity, size_t &written) {
    written = 0;
    if (!out || !smp_motion_valid(c)) return false;
    Encoder e;
    e.number(5, 5);
    e.key("moving_ms"); e.number(0, c.moving_ms);
    e.key("idle_ms"); e.number(0, c.idle_ms);
    e.key("quiet_ms"); e.number(0, c.quiet_ms);
    e.key("threshold_mg"); e.number(0, c.threshold_mg);
    e.key("duration_samples"); e.number(0, c.duration_samples);
    if (e.size > capacity) return false;
    e.bytes[0] = 2; e.bytes[3] = static_cast<uint8_t>(e.size - 8);
    e.bytes[5] = 64; e.bytes[6] = sequence; e.bytes[7] = 2;
    std::memcpy(out, e.bytes, e.size);
    written = e.size;
    return true;
}

bool smp_accept_upload_offset(uint32_t reply, uint32_t sent_offset,
                              uint32_t sent_end, uint32_t total,
                              bool first, uint8_t& stalled) {
    if (total < 32 || total > MaxImage || sent_offset > sent_end || sent_end > total ||
        (first && sent_offset != 0) || reply < sent_offset ||
        reply > (first ? total : sent_end)) return false;
    if (reply != sent_offset) stalled = 0;
    else if (stalled != UINT8_MAX) ++stalled;
    return stalled < 3;
}

namespace {
// A validating pass covers every byte, including unknown fields and denied
// replies. A second pass extracts the command schema without an allocated tree.
struct CborHead {
    uint8_t major, additional;
    uint64_t value;
    bool indefinite;
};
struct CborSlice { const uint8_t* data; size_t size; };
struct CborContainer { uint64_t remaining; bool indefinite; };

bool valid_utf8(const uint8_t* bytes, size_t size) {
    size_t i = 0;
    while (i < size) {
        const uint8_t first = bytes[i++];
        if (first < 0x80) continue;
        unsigned trailing;
        uint32_t value, minimum;
        if (first >= 0xc2 && first <= 0xdf) { trailing = 1; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { trailing = 2; value = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { trailing = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        if (trailing > size - i) return false;
        while (trailing--) {
            const uint8_t next = bytes[i++];
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

class CborReader {
public:
    CborReader(const uint8_t* bytes, size_t size) : bytes_(bytes), size_(size), pos_(0) {}
    bool done() const { return pos_ == size_; }
    bool head(CborHead& h) {
        if (pos_ == size_) return false;
        const uint8_t byte = bytes_[pos_++];
        h = {static_cast<uint8_t>(byte >> 5), static_cast<uint8_t>(byte & 31), 0, false};
        if (h.additional < 24) { h.value = h.additional; return true; }
        if (h.additional == 31) {
            h.indefinite = true;
            return h.major == 4 || h.major == 5;
        }
        if (h.additional > 27) return false;
        const unsigned width = 1u << (h.additional - 24);
        if (width > size_ - pos_) return false;
        for (unsigned i = 0; i < width; ++i) h.value = (h.value << 8) | bytes_[pos_++];
        // Container/string lengths must use their shortest representation.
        // Unsigned/negative integer values may use any valid CBOR width; the
        // outgoing encoder always uses minimal widths. Float bits are opaque.
        if (h.major >= 2 && h.major <= 5 &&
            ((width == 1 && h.value < 24) || (width == 2 && h.value <= UINT8_MAX) ||
             (width == 4 && h.value <= UINT16_MAX) || (width == 8 && h.value <= UINT32_MAX))) return false;
        return true;
    }
    bool slice(uint8_t major, CborSlice& slice) {
        CborHead h{};
        if (!head(h) || h.major != major || h.indefinite || h.value > size_ - pos_) return false;
        slice = {bytes_ + pos_, static_cast<size_t>(h.value)};
        pos_ += slice.size;
        return major != 3 || valid_utf8(slice.data, slice.size);
    }
    bool number(uint64_t maximum, uint64_t& value) {
        CborHead h{};
        if (!head(h) || h.major != 0 || h.value > maximum) return false;
        value = h.value;
        return true;
    }
    template<typename T> bool number(T& value) {
        uint64_t n;
        if (!number(static_cast<T>(~T(0)), n)) return false;
        value = static_cast<T>(n);
        return true;
    }
    bool boolean(bool& value) {
        if (pos_ == size_ || (bytes_[pos_] != 0xf4 && bytes_[pos_] != 0xf5)) return false;
        value = bytes_[pos_++] == 0xf5;
        return true;
    }
    bool string(char* out, size_t capacity, bool identity = false) {
        CborSlice s{};
        if (!slice(3, s) || s.size == 0 || s.size >= capacity ||
            std::memchr(s.data, 0, s.size) || (identity && s.size != 16)) return false;
        if (identity) for (size_t i = 0; i < s.size; ++i)
            if (!((s.data[i] >= '0' && s.data[i] <= '9') ||
                  (s.data[i] >= 'a' && s.data[i] <= 'f'))) return false;
        std::memcpy(out, s.data, s.size);
        out[s.size] = '\0';
        return true;
    }
    bool container(uint8_t major, CborContainer& c) {
        CborHead h{};
        if (!head(h) || h.major != major || (!h.indefinite && h.value > 64)) return false;
        c = {h.value, h.indefinite};
        return true;
    }
    // Only used for schema extraction after the complete structural pass.
    bool next(CborContainer& c) {
        if (!c.indefinite) {
            if (!c.remaining) return false;
            --c.remaining;
            return true;
        }
        if (pos_ == size_) return false;
        if (bytes_[pos_] == 0xff) { ++pos_; return false; }
        return true;
    }
    bool key(const char* const* names, size_t count, uint32_t& seen, int& index) {
        index = -1;
        if (pos_ == size_) return false;
        // Non-text keys cannot match a known field but must still be valid CBOR.
        if ((bytes_[pos_] >> 5) != 3) return skip();
        CborSlice s{};
        if (!slice(3, s)) return false;
        for (size_t i = 0; i < count; ++i) {
            if (s.size == std::strlen(names[i]) && std::memcmp(s.data, names[i], s.size) == 0) {
                const uint32_t bit = uint32_t(1) << i;
                if (seen & bit) return false;
                seen |= bit;
                index = static_cast<int>(i);
                break;
            }
        }
        return true;
    }
    bool skip(unsigned depth = 0) {
        CborHead h{};
        if (!head(h)) return false;
        switch (h.major) {
        case 0: case 1: return true;
        case 2: case 3: {
            if (h.value > size_ - pos_) return false;
            const auto length = static_cast<size_t>(h.value);
            if (h.major == 3 && !valid_utf8(bytes_ + pos_, length)) return false;
            pos_ += length;
            return true;
        }
        case 4: case 5: {
            if (depth >= 8 || (!h.indefinite && h.value > 64)) return false;
            size_t entries = 0;
            for (;;) {
                if (h.indefinite) {
                    if (pos_ == size_) return false;
                    if (bytes_[pos_] == 0xff) { ++pos_; return true; }
                } else if (entries == h.value) return true;
                if (entries == 64 || !skip(depth + 1) || (h.major == 5 && !skip(depth + 1))) return false;
                ++entries;
            }
        }
        case 7:
            return h.additional == 20 || h.additional == 21 || h.additional == 22 ||
                   h.additional == 25 || h.additional == 26 || h.additional == 27;
        default: return false; // CBOR tags and unsupported simple values
        }
    }
private:
    const uint8_t* bytes_;
    size_t size_, pos_;
};

bool parse_image(CborReader& c, SmpImage& image) {
    static const char* const keys[] = {"slot", "version", "hash", "bootable", "pending",
                                      "confirmed", "active", "permanent", "image"};
    CborContainer map{};
    if (!c.container(5, map)) return false;
    uint32_t seen = 0;
    while (c.next(map)) {
        int key;
        if (!c.key(keys, 9, seen, key)) return false;
        switch (key) {
        case 0: if (!c.number(image.slot) || image.slot > 1) return false; break;
        case 1: if (!c.string(image.version, sizeof(image.version))) return false; break;
        case 2: {
            CborSlice hash{};
            if (!c.slice(2, hash) || hash.size != sizeof(image.hash)) return false;
            std::memcpy(image.hash, hash.data, hash.size);
            break;
        }
        case 3: if (!c.boolean(image.bootable)) return false; break;
        case 4: if (!c.boolean(image.pending)) return false; break;
        case 5: if (!c.boolean(image.confirmed)) return false; break;
        case 6: if (!c.boolean(image.active)) return false; break;
        case 7: if (!c.boolean(image.permanent)) return false; break;
        case 8: { uint64_t index; if (!c.number(0, index)) return false; break; }
        default: if (!c.skip()) return false;
        }
    }
    return (seen & 0xff) == 0xff;
}

bool parse_images(CborReader& c, SmpReply& reply) {
    CborContainer array{};
    if (!c.container(4, array)) return false;
    uint8_t slots = 0;
    bool active = false;
    while (c.next(array)) {
        if (reply.image_count == 2) return false;
        auto& image = reply.images[reply.image_count++];
        if (!parse_image(c, image) || (slots & (1u << image.slot)) || (active && image.active)) return false;
        slots |= static_cast<uint8_t>(1u << image.slot);
        active = active || image.active;
    }
    return true;
}

bool parse_error(CborReader& c, SmpReply& reply) {
    static const char* const keys[] = {"group", "rc"};
    CborContainer map{};
    if (!c.container(5, map)) return false;
    uint32_t seen = 0;
    while (c.next(map)) {
        int key;
        if (!c.key(keys, 2, seen, key)) return false;
        if (key == 0) { if (!c.number(reply.error_group)) return false; }
        else if (key == 1) { if (!c.number(reply.error_code) || reply.error_code == 0) return false; }
        else if (!c.skip()) return false;
    }
    return seen == 3;
}

bool decode_reply(SmpCommand command, const uint8_t* bytes, size_t size, SmpReply& reply) {
    CborReader validation(bytes, size);
    if (!validation.skip() || !validation.done()) return false;
    CborReader c(bytes, size);
    CborContainer map{};
    if (!c.container(5, map)) return false;
    static const char* const tag_keys[] = {"rc", "err", "id", "version", "tag", "uptime_ms",
                                          "reset_reason", "confirmed", "maintenance", "radio_ok", "ble_ok",
                                          "config_pending", "sensor_error", "config_schema"};
    static const char* const config_keys[] = {"rc", "err", "moving_ms", "idle_ms", "quiet_ms",
                                             "threshold_mg", "duration_samples"};
    static const char* const image_keys[] = {"rc", "err", "images"};
    static const char* const upload_keys[] = {"rc", "err", "off"};
    const char* const* keys = tag_keys;
    size_t key_count = 2;
    const bool config = command == SmpCommand::ConfigRead || command == SmpCommand::ConfigWrite;
    if (command == SmpCommand::TagStatus) key_count = 14;
    else if (config) { keys = config_keys; key_count = 7; }
    else if (command == SmpCommand::ImageList) { keys = image_keys; key_count = 3; }
    else if (command == SmpCommand::Upload) { keys = upload_keys; key_count = 3; }
    uint32_t seen = 0;
    uint32_t rc = 0, config_schema = 0;
    while (c.next(map)) {
        int key;
        if (!c.key(keys, key_count, seen, key)) return false;
        if (key == 0) { if (!c.number(rc)) return false; }
        else if (key == 1) { if (!parse_error(c, reply)) return false; }
        else if (key == -1) { if (!c.skip()) return false; }
        else if (command == SmpCommand::ImageList) { if (!parse_images(c, reply)) return false; }
        else if (command == SmpCommand::Upload) { if (!c.number(reply.offset)) return false; }
        else if (config) {
            switch (key) {
            case 2: if (!c.number(reply.config.moving_ms)) return false; break;
            case 3: if (!c.number(reply.config.idle_ms)) return false; break;
            case 4: if (!c.number(reply.config.quiet_ms)) return false; break;
            case 5: if (!c.number(reply.config.threshold_mg)) return false; break;
            case 6: if (!c.number(reply.config.duration_samples)) return false; break;
            default: return false;
            }
        } else {
            switch (key) {
            case 2: if (!c.string(reply.tag.id, sizeof(reply.tag.id), true)) return false; break;
            case 3: if (!c.string(reply.tag.version, sizeof(reply.tag.version))) return false; break;
            case 4: if (!c.number(reply.tag.tag) || reply.tag.tag == 0 || reply.tag.tag == 0xffff) return false; break;
            case 5: if (!c.number(reply.tag.uptime_ms)) return false; break;
            case 6: if (!c.number(reply.tag.reset_reason)) return false; break;
            case 7: if (!c.boolean(reply.tag.confirmed)) return false; break;
            case 8: if (!c.boolean(reply.tag.maintenance)) return false; break;
            case 9: if (!c.boolean(reply.tag.radio_ok)) return false; break;
            case 10: if (!c.boolean(reply.tag.ble_ok)) return false; break;
            case 11: if (!c.boolean(reply.tag.config_pending)) return false; break;
            case 13: if (!c.number(config_schema)) return false; break;
            case 12: {
                CborHead h{};
                if (!c.head(h) || h.indefinite || h.major > 1 || h.value > INT32_MAX) return false;
                reply.tag.sensor_error = h.major == 0 ? static_cast<int32_t>(h.value) :
                    -1 - static_cast<int32_t>(h.value);
                break;
            }
            default: return false;
            }
        }
    }
    if (!c.done() || (seen & 3) == 3) return false;
    if ((seen & 2) || rc != 0) {
        reply.outcome = SmpOutcome::Denied;
        if (rc != 0) { reply.error_group = 0; reply.error_code = rc; }
        return true;
    }
    reply.outcome = SmpOutcome::Ok;
    if (command == SmpCommand::TagStatus) {
        reply.tag.config_status_known = (seen & 0x3800) == 0x3800 && config_schema == 1;
        return (seen & 0x7fc) == 0x7fc;
    }
    if (config) return (seen & 0x7c) == 0x7c && smp_motion_valid(reply.config);
    if (command == SmpCommand::ImageList || command == SmpCommand::Upload) return (seen & 4) != 0;
    return true;
}
} // namespace

SmpAssembler::SmpAssembler() { abort(); }

void SmpAssembler::abort() {
    state_ = State::Idle;
    command_ = SmpCommand::ImageList;
    sequence_ = 0;
    used_ = 0;
    reply_ = {};
}

SmpFeed SmpAssembler::fail() {
    used_ = 0;
    reply_ = {};
    state_ = State::Failed;
    return SmpFeed::Error;
}

bool SmpAssembler::begin(SmpCommand command, uint8_t sequence) {
    if (state_ == State::Receiving || state_ == State::Failed) return false;
    WireCommand wire{};
    if (!wire_command(command, wire)) { fail(); return false; }
    used_ = 0;
    reply_ = {};
    command_ = command;
    sequence_ = sequence;
    state_ = State::Receiving;
    return true;
}

const SmpReply* SmpAssembler::reply() const {
    return state_ == State::Complete ? &reply_ : nullptr;
}

SmpFeed SmpAssembler::feed(const uint8_t* bytes, size_t size) {
    if (state_ != State::Receiving || (!bytes && size != 0) ||
        size > sizeof(buffer_) - used_) return fail();
    if (size == 0) return SmpFeed::More;
    std::memcpy(buffer_ + used_, bytes, size);
    used_ += size;
    if (used_ < 8) return SmpFeed::More;
    WireCommand wire{};
    if (!wire_command(command_, wire)) return fail();
    const size_t length = (size_t(buffer_[2]) << 8) | buffer_[3];
    if (buffer_[0] != wire.op + 1 || buffer_[1] != 0 ||
        buffer_[4] != (wire.group >> 8) || buffer_[5] != (wire.group & 0xff) ||
        buffer_[6] != sequence_ || buffer_[7] != wire.id || length == 0 ||
        length > sizeof(buffer_) - 8 || used_ > length + 8) return fail();
    if (used_ < length + 8) return SmpFeed::More;
    if (!decode_reply(command_, buffer_ + 8, length, reply_)) return fail();
    state_ = State::Complete;
    return SmpFeed::Complete;
}
