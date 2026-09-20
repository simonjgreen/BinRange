#include <unity.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>
#include "core/smp_codec.h"
#include "../fixtures/k4w_smp_status_c.h"

using Bytes = std::vector<uint8_t>;
static_assert(std::is_same<decltype(std::declval<SmpAssembler&>().reply()),
                           const SmpReply*>::value, "parsed replies are immutable");
static_assert(!std::is_assignable<decltype(*std::declval<SmpAssembler&>().reply()),
                                 SmpReply>::value, "no whole-reply replacement");

void setUp() {}
void tearDown() {}

static Bytes hex(const char* text) {
    Bytes out;
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (; *text; text += 2) out.push_back((digit(text[0]) << 4) | digit(text[1]));
    return out;
}

static void request_mappings_and_capacity() {
    struct Case { SmpCommand command; const char* wire; };
    const Case cases[] = {
        {SmpCommand::ImageList, "0000000100010000a0"},
        {SmpCommand::TagStatus, "0000000100400000a0"},
        {SmpCommand::Trial, "0200000100400001a0"},
        {SmpCommand::Reset, "0200000100000005a0"},
    };
    for (const auto& c : cases) {
        const auto expected = hex(c.wire);
        uint8_t out[512];
        size_t written = 99;
        TEST_ASSERT_TRUE(smp_encode_request(c.command, 0, nullptr, out, 9, written));
        TEST_ASSERT_EQUAL_UINT(9, written);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), out, written);
        std::memset(out, 0x55, sizeof(out));
        TEST_ASSERT_FALSE(smp_encode_request(c.command, 0, nullptr, out, 8, written));
        TEST_ASSERT_EQUAL_UINT(0, written);
        for (auto b : out) TEST_ASSERT_EQUAL_HEX8(0x55, b);
        SmpUpload inappropriate{};
        TEST_ASSERT_FALSE(smp_encode_request(c.command, 0, &inappropriate, out, 512, written));
        TEST_ASSERT_FALSE(smp_encode_request(c.command, 0, nullptr, nullptr, 512, written));
    }
    uint8_t out[512];
    size_t written = 99;
    TEST_ASSERT_FALSE(smp_encode_request(static_cast<SmpCommand>(99), 0, nullptr, out, 512, written));
    TEST_ASSERT_EQUAL_UINT(0, written);
    TEST_ASSERT_FALSE(smp_encode_request(SmpCommand::Upload, 0, nullptr, out, 512, written));
}

static void upload_wire_and_boundaries() {
    uint8_t out[512], data[257], sha[32];
    std::memset(data, 0x42, sizeof(data));
    std::memset(sha, 0x11, sizeof(sha));
    SmpUpload u{0, 32, sha, data, 32};
    size_t written = 0;
    TEST_ASSERT_TRUE(smp_encode_request(SmpCommand::Upload, 0xff, &u, out, 512, written));
    auto expected = hex("020000590001ff01a4636f66660064646174615820");
    expected.insert(expected.end(), 32, 0x42);
    auto tail = hex("636c656e1820637368615820");
    expected.insert(expected.end(), tail.begin(), tail.end());
    expected.insert(expected.end(), 32, 0x11);
    TEST_ASSERT_EQUAL_UINT(expected.size(), written);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), out, written);
    const size_t exact = written;
    TEST_ASSERT_TRUE(smp_encode_request(SmpCommand::Upload, 0xff, &u, out, exact, written));
    std::memset(out, 0x55, sizeof(out));
    TEST_ASSERT_FALSE(smp_encode_request(SmpCommand::Upload, 0, &u, out, exact - 1, written));
    TEST_ASSERT_EQUAL_UINT(0, written);
    for (auto b : out) TEST_ASSERT_EQUAL_HEX8(0x55, b);
    u = {212991, 212992, nullptr, data, 1};
    expected = hex("0200001100010701a2636f66661a00033fff64646174614142");
    TEST_ASSERT_TRUE(smp_encode_request(SmpCommand::Upload, 7, &u, out, 512, written));
    TEST_ASSERT_EQUAL_UINT(expected.size(), written);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), out, written);
    u = {0, 212992, sha, data, 256};
    TEST_ASSERT_TRUE(smp_encode_request(SmpCommand::Upload, 0, &u, out, 512, written));
    TEST_ASSERT_LESS_OR_EQUAL_UINT(512, written);
    const SmpUpload invalid[] = {
        {0, 31, sha, data, 32}, {0, 212993, sha, data, 32},
        {0, 32, nullptr, data, 32}, {0, 32, sha, nullptr, 32},
        {0, 64, sha, data, 0}, {0, 64, sha, data, 31},
        {0, 512, sha, data, 257}, {32, 32, nullptr, data, 1},
        {UINT32_MAX, 32, nullptr, data, 2}, {31, 32, nullptr, data, 2},
        {0, UINT32_MAX, sha, data, 32},
    };
    for (const auto& bad : invalid) {
        written = 99;
        TEST_ASSERT_FALSE(smp_encode_request(SmpCommand::Upload, 0, &bad, out, 512, written));
        TEST_ASSERT_EQUAL_UINT(0, written);
    }
    // Integer and byte-string width boundaries, independently specified.
    struct Width { uint32_t off; const char* wire; };
    const Width widths[] = {{23, "17"}, {24, "1818"}, {255, "18ff"},
                            {256, "190100"}, {65535, "19ffff"}, {65536, "1a00010000"}};
    for (const auto& w : widths) {
        u = {w.off, 212992, nullptr, data, 1};
        TEST_ASSERT_TRUE(smp_encode_request(SmpCommand::Upload, 0, &u, out, 512, written));
        auto integer = hex(w.wire);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(integer.data(), out + 13, integer.size());
    }
    for (size_t size : {size_t(23), size_t(24), size_t(255), size_t(256)}) {
        u = {32, 512, nullptr, data, size};
        TEST_ASSERT_TRUE(smp_encode_request(SmpCommand::Upload, 0, &u, out, 512, written));
        auto length = hex(size == 23 ? "57" : size == 24 ? "5818" : size == 255 ? "58ff" : "590100");
        TEST_ASSERT_EQUAL_UINT8_ARRAY(length.data(), out + 20, length.size());
    }
}

static void offsets_resume_stall_and_overflow() {
    uint8_t stalled = 0;
    TEST_ASSERT_TRUE(smp_accept_upload_offset(1000, 0, 32, 2000, true, stalled));
    TEST_ASSERT_EQUAL_UINT8(0, stalled);
    TEST_ASSERT_TRUE(smp_accept_upload_offset(2000, 0, 32, 2000, true, stalled));
    TEST_ASSERT_TRUE(smp_accept_upload_offset(0, 0, 32, 2000, true, stalled));
    TEST_ASSERT_TRUE(smp_accept_upload_offset(0, 0, 32, 2000, false, stalled));
    TEST_ASSERT_FALSE(smp_accept_upload_offset(0, 0, 32, 2000, false, stalled));
    TEST_ASSERT_EQUAL_UINT8(3, stalled);
    stalled = 255;
    TEST_ASSERT_FALSE(smp_accept_upload_offset(32, 32, 64, 2000, false, stalled));
    TEST_ASSERT_EQUAL_UINT8(255, stalled);
    TEST_ASSERT_TRUE(smp_accept_upload_offset(64, 32, 64, 2000, false, stalled));
    TEST_ASSERT_EQUAL_UINT8(0, stalled);
    struct Bad { uint32_t reply, offset, end, total; bool first; };
    const Bad cases[] = {{31,32,64,2000,false}, {65,32,64,2000,false},
        {2001,0,32,2000,true}, {32,32,64,2000,true}, {0,0,32,31,true},
        {0,0,32,212993,true}, {0,64,32,2000,false}, {0,0,2001,2000,true},
        {UINT32_MAX,0,32,2000,true}, {0,UINT32_MAX,0,2000,false}};
    for (const auto& c : cases) {
        stalled = 2;
        TEST_ASSERT_FALSE(smp_accept_upload_offset(c.reply,c.offset,c.end,c.total,c.first,stalled));
        TEST_ASSERT_EQUAL_UINT8(2, stalled);
    }
    TEST_ASSERT_TRUE(smp_accept_upload_offset(32, 0, 32, 32, true, stalled));
    TEST_ASSERT_TRUE(smp_accept_upload_offset(212992, 212991, 212992, 212992, false, stalled));
}

static void append(Bytes& to, const Bytes& from) { to.insert(to.end(), from.begin(), from.end()); }
static Bytes text(const char* s) {
    const size_t n = std::strlen(s);
    Bytes b;
    if (n < 24) b.push_back(0x60 + n);
    else { b.push_back(0x78); b.push_back(n); }
    b.insert(b.end(), s, s + n);
    return b;
}
using Fields = std::vector<std::pair<const char*, Bytes>>;
static Bytes map(const Fields& fields, bool indefinite = false) {
    Bytes b;
    if (indefinite) b.push_back(0xbf);
    else if (fields.size() < 24) b.push_back(0xa0 + fields.size());
    else { b.push_back(0xb8); b.push_back(fields.size()); }
    for (const auto& f : fields) { append(b, text(f.first)); append(b, f.second); }
    if (indefinite) b.push_back(0xff);
    return b;
}
static Bytes array(const std::vector<Bytes>& items, bool indefinite = false) {
    Bytes b;
    if (indefinite) b.push_back(0x9f);
    else if (items.size() < 24) b.push_back(0x80 + items.size());
    else { b.push_back(0x98); b.push_back(items.size()); }
    for (const auto& item : items) append(b, item);
    if (indefinite) b.push_back(0xff);
    return b;
}
static Bytes packet(SmpCommand command, const Bytes& body, uint8_t sequence = 0) {
    // Literal response mappings independent of the encoder under test.
    Bytes b = command == SmpCommand::ImageList ? hex("0100000000010000") :
              command == SmpCommand::TagStatus ? hex("0100000000400000") :
              command == SmpCommand::Upload ? hex("0300000000010001") :
              command == SmpCommand::Trial ? hex("0300000000400001") : hex("0300000000000005");
    b[2] = body.size() >> 8; b[3] = body.size(); b[6] = sequence;
    append(b, body);
    return b;
}
static SmpReply parse(SmpCommand command, const Bytes& body) {
    auto wire = packet(command, body);
    SmpAssembler a;
    TEST_ASSERT_TRUE(a.begin(command, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Complete), static_cast<int>(a.feed(wire.data(), wire.size())));
    TEST_ASSERT_NOT_NULL(a.reply());
    return *a.reply();
}
static void reject(SmpCommand command, const Bytes& body) {
    auto wire = packet(command, body);
    SmpAssembler a;
    TEST_ASSERT_TRUE(a.begin(command, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(wire.data(), wire.size())));
    TEST_ASSERT_NULL(a.reply());
    TEST_ASSERT_FALSE(a.begin(command, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(nullptr, 0)));
    a.abort();
    TEST_ASSERT_TRUE(a.begin(command, 0));
}
static Fields tag_fields() {
    return {{"id", text("f00dbaad12345678")}, {"tag", hex("19b100")},
        {"version", text("0.2.2")}, {"confirmed", hex("f5")},
        {"uptime_ms", hex("1a00480dd7")}, {"reset_reason", hex("02")},
        {"maintenance", hex("f5")}, {"radio_ok", hex("f5")}, {"ble_ok", hex("f5")}};
}
static Fields image_fields() {
    return {{"slot", hex("00")}, {"version", text("0.2.2")},
        {"hash", hex("582000112233445566778899aabbccddeeff00112233445566778899aabbccddeeff")},
        {"bootable", hex("f5")}, {"pending", hex("f4")}, {"confirmed", hex("f5")},
        {"active", hex("f5")}, {"permanent", hex("f4")}};
}
static Bytes image_body(const Fields& fields) { return map({{"images", array({map(fields)})}}); }
static void captured_values(const SmpReply& r, bool status) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpOutcome::Ok), static_cast<int>(r.outcome));
    if (status) {
        TEST_ASSERT_EQUAL_STRING("f00dbaad12345678", r.tag.id);
        TEST_ASSERT_EQUAL_STRING("0.2.2", r.tag.version);
        TEST_ASSERT_EQUAL_HEX16(0xb100, r.tag.tag);
        TEST_ASSERT_EQUAL_UINT64(4722135, r.tag.uptime_ms);
        TEST_ASSERT_EQUAL_UINT32(2, r.tag.reset_reason);
        TEST_ASSERT_TRUE(r.tag.confirmed && r.tag.maintenance && r.tag.radio_ok && r.tag.ble_ok);
    } else {
        TEST_ASSERT_EQUAL_UINT(1, r.image_count);
        const auto hash_bytes = hex("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
        TEST_ASSERT_EQUAL_UINT8_ARRAY(hash_bytes.data(), r.images[0].hash, 32);
        TEST_ASSERT_EQUAL_STRING("0.2.2", r.images[0].version);
        TEST_ASSERT_EQUAL_UINT8(0, r.images[0].slot);
        TEST_ASSERT_TRUE(r.images[0].bootable && r.images[0].confirmed && r.images[0].active);
        TEST_ASSERT_FALSE(r.images[0].pending || r.images[0].permanent);
    }
}
static void captured_packets_every_split_and_byte() {
    for (bool status : {false, true}) {
        const auto wire = hex(status ? k4w_smp_fixture::status_response_hex : k4w_smp_fixture::image_response_hex);
        const auto command = status ? SmpCommand::TagStatus : SmpCommand::ImageList;
        for (size_t split = 0; split <= wire.size(); ++split) {
            SmpAssembler a;
            TEST_ASSERT_TRUE(a.begin(command, status ? 1 : 0));
            const auto first = a.feed(wire.data(), split);
            if (split < wire.size()) {
                TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::More), static_cast<int>(first));
                TEST_ASSERT_NULL(a.reply());
                TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Complete),
                    static_cast<int>(a.feed(wire.data() + split, wire.size() - split)));
            } else TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Complete), static_cast<int>(first));
            TEST_ASSERT_NOT_NULL(a.reply());
            captured_values(*a.reply(), status);
        }
        SmpAssembler a;
        TEST_ASSERT_TRUE(a.begin(command, status ? 1 : 0));
        for (size_t i = 0; i < wire.size(); ++i) {
            TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::More), static_cast<int>(a.feed(nullptr, 0)));
            auto got = a.feed(&wire[i], 1);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(i + 1 == wire.size() ? SmpFeed::Complete : SmpFeed::More), static_cast<int>(got));
            if (i + 1 < wire.size()) TEST_ASSERT_NULL(a.reply());
        }
        captured_values(*a.reply(), status);
    }
}
static void definite_equivalents_and_mixed_containers() {
    for (bool root : {false, true}) for (bool arr : {false, true}) for (bool item : {false, true}) {
        auto body = map({{"images", array({map(image_fields(), item)}, arr)}, {"splitStatus", hex("00")}}, root);
        captured_values(parse(SmpCommand::ImageList, body), false);
    }
    captured_values(parse(SmpCommand::TagStatus, map(tag_fields())), true);
}
static void session_lifecycle_and_stale_success() {
    auto wire = packet(SmpCommand::Reset, hex("a0"));
    SmpAssembler a;
    TEST_ASSERT_NULL(a.reply());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(nullptr, 0)));
    TEST_ASSERT_FALSE(a.begin(SmpCommand::Reset, 0));
    a.abort();
    TEST_ASSERT_TRUE(a.begin(SmpCommand::Reset, 0));
    TEST_ASSERT_FALSE(a.begin(SmpCommand::Trial, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::More), static_cast<int>(a.feed(wire.data(), 3)));
    a.abort(); // timeout/disconnect drops the partial header
    TEST_ASSERT_NULL(a.reply());
    TEST_ASSERT_TRUE(a.begin(SmpCommand::Reset, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Complete), static_cast<int>(a.feed(wire.data(), wire.size())));
    TEST_ASSERT_NOT_NULL(a.reply());
    TEST_ASSERT_TRUE(a.begin(SmpCommand::Reset, 0));
    TEST_ASSERT_NULL(a.reply());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(nullptr, 1)));
    TEST_ASSERT_NULL(a.reply());
    TEST_ASSERT_FALSE(a.begin(SmpCommand::Reset, 0));
    a.abort();
    TEST_ASSERT_TRUE(a.begin(SmpCommand::Reset, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Complete), static_cast<int>(a.feed(wire.data(), wire.size())));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(nullptr, 0)));
    TEST_ASSERT_NULL(a.reply());
    a.abort();
    TEST_ASSERT_FALSE(a.begin(static_cast<SmpCommand>(99), 0));
    TEST_ASSERT_NULL(a.reply());
    a.abort();
    TEST_ASSERT_TRUE(a.begin(SmpCommand::Reset, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Complete), static_cast<int>(a.feed(wire.data(), wire.size())));
    TEST_ASSERT_FALSE(a.begin(static_cast<SmpCommand>(99), 0));
    TEST_ASSERT_NULL(a.reply());
}
static void framing_mismatches_lengths_and_extra_packets() {
    const auto valid = packet(SmpCommand::Trial, hex("a0"), 255);
    for (size_t index : {size_t(0),size_t(1),size_t(4),size_t(5),size_t(6),size_t(7)}) {
        auto wire = valid; wire[index] ^= 1;
        SmpAssembler a; TEST_ASSERT_TRUE(a.begin(SmpCommand::Trial, 255));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(wire.data(), wire.size())));
        TEST_ASSERT_NULL(a.reply());
    }
    for (const char* length : {"0000", "0ff9", "ffff"}) {
        auto wire = valid; auto bytes = hex(length); wire[2]=bytes[0]; wire[3]=bytes[1];
        SmpAssembler a; TEST_ASSERT_TRUE(a.begin(SmpCommand::Trial, 255));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(wire.data(), 8)));
    }
    for (bool two : {false, true}) {
        auto wire = valid;
        if (two) append(wire, valid); else wire.push_back(0);
        SmpAssembler a; TEST_ASSERT_TRUE(a.begin(SmpCommand::Trial, 255));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(wire.data(), wire.size())));
        TEST_ASSERT_NULL(a.reply());
    }
    // A valid 4096-byte packet: map + one-byte key + byte-string header + 4082 bytes.
    Bytes body = hex("a16178590ff2"); body.insert(body.end(), 4082, 0);
    auto wire = packet(SmpCommand::Reset, body);
    TEST_ASSERT_EQUAL_UINT(4096, wire.size());
    auto r = parse(SmpCommand::Reset, body);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpOutcome::Ok), static_cast<int>(r.outcome));
    wire.push_back(0); wire[3]++;
    SmpAssembler a; TEST_ASSERT_TRUE(a.begin(SmpCommand::Reset, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(wire.data(), wire.size())));
    a.abort(); TEST_ASSERT_TRUE(a.begin(SmpCommand::Reset, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error), static_cast<int>(a.feed(valid.data(), SIZE_MAX)));
    a.abort(); TEST_ASSERT_TRUE(a.begin(SmpCommand::Reset, 0));
    auto incomplete = packet(SmpCommand::Reset, hex("a0")); incomplete[3] = 2;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::More), static_cast<int>(a.feed(incomplete.data(), incomplete.size())));
    TEST_ASSERT_NULL(a.reply()); a.abort(); // incomplete payload cannot be exposed
}
static void known_required_fields_duplicates_and_types() {
    for (bool tag : {false, true}) {
        const auto fields = tag ? tag_fields() : image_fields();
        const auto command = tag ? SmpCommand::TagStatus : SmpCommand::ImageList;
        for (size_t i = 0; i < fields.size(); ++i) {
            auto missing = fields; missing.erase(missing.begin() + i);
            reject(command, tag ? map(missing) : image_body(missing));
            auto duplicate = fields; duplicate.push_back(fields[i]);
            reject(command, tag ? map(duplicate) : image_body(duplicate));
            for (const char* wrong : {"f6", "20", "80", "a0"}) {
                auto bad = fields; bad[i].second = hex(wrong);
                reject(command, tag ? map(bad) : image_body(bad));
            }
            auto bad = fields;
            bad[i].second = hex((fields[i].second[0] == 0xf4 || fields[i].second[0] == 0xf5) ? "00" : "f5");
            reject(command, tag ? map(bad) : image_body(bad));
        }
    }
    reject(SmpCommand::TagStatus, hex("a0"));
    reject(SmpCommand::ImageList, hex("a0"));
    reject(SmpCommand::Upload, hex("a0"));
    reject(SmpCommand::Upload, map({{"off", hex("00")}, {"off", hex("00")}}));
    for (const char* bad : {"20", "f5", "1b0000000100000000", "60"}) reject(SmpCommand::Upload, map({{"off", hex(bad)}}));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, parse(SmpCommand::Upload, map({{"off",hex("1affffffff")}})).offset);
    TEST_ASSERT_EQUAL_UINT32(0, parse(SmpCommand::Upload, map({{"off",hex("00")}})).offset);
    reject(SmpCommand::ImageList, map({{"images", hex("80")}, {"images", hex("80")}}));
}
static void identity_version_utf8_and_numeric_bounds() {
    for (const char* id : {"", "f00dbaad1234567", "f00dbaad123456789", "f00DBAAD12345678", "g00dbaad12345678"}) {
        auto f = tag_fields(); f[0].second = text(id); reject(SmpCommand::TagStatus, map(f));
    }
    auto f = tag_fields(); f[0].second[5] = 0; reject(SmpCommand::TagStatus, map(f));
    for (const char* bad : {"00", "19ffff", "1a00010000", "1bffffffffffffffff"}) {
        f = tag_fields(); f[1].second = hex(bad); reject(SmpCommand::TagStatus, map(f));
    }
    for (const char* good : {"01", "19fffe"}) {
        f = tag_fields(); f[1].second = hex(good); parse(SmpCommand::TagStatus, map(f));
    }
    for (bool tag : {false, true}) {
        for (const char* bad : {"60", "7800", "6161ff", "6180", "62c080", "62c2ff", "63eda080", "64f4908080", "63e28200", "62f580", "63e282ac00"}) {
            auto v = tag ? tag_fields() : image_fields(); v[tag ? 2 : 1].second = hex(bad);
            reject(tag ? SmpCommand::TagStatus : SmpCommand::ImageList, tag ? map(v) : image_body(v));
        }
        auto v = tag ? tag_fields() : image_fields();
        v[tag ? 2 : 1].second = text("123456789012345678901234");
        reject(tag ? SmpCommand::TagStatus : SmpCommand::ImageList, tag ? map(v) : image_body(v));
        for (const auto& good : {text("12345678901234567890123"), hex("6bc2a2e282acf09f98806162")}) {
            v[tag ? 2 : 1].second = good;
            parse(tag ? SmpCommand::TagStatus : SmpCommand::ImageList, tag ? map(v) : image_body(v));
        }
        v[tag ? 2 : 1].second = hex("63610062");
        reject(tag ? SmpCommand::TagStatus : SmpCommand::ImageList, tag ? map(v) : image_body(v));
    }
    f = tag_fields(); f[4].second = hex("00"); f[5].second = hex("00");
    auto r = parse(SmpCommand::TagStatus, map(f));
    TEST_ASSERT_EQUAL_UINT64(0, r.tag.uptime_ms); TEST_ASSERT_EQUAL_UINT32(0, r.tag.reset_reason);
    f[4].second = hex("1bffffffffffffffff"); f[5].second = hex("1affffffff");
    r = parse(SmpCommand::TagStatus, map(f));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, r.tag.uptime_ms); TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, r.tag.reset_reason);
    f[5].second = hex("1b0000000100000000"); reject(SmpCommand::TagStatus, map(f));
}
static void image_array_slots_hash_and_active_bounds() {
    auto first = image_fields(), second = image_fields();
    second[0].second = hex("01"); second[6].second = hex("f4");
    auto r = parse(SmpCommand::ImageList, map({{"images", array({map(first),map(second)})}}));
    TEST_ASSERT_EQUAL_UINT(2, r.image_count); TEST_ASSERT_EQUAL_UINT8(1, r.images[1].slot);
    first[6].second = hex("f4");
    parse(SmpCommand::ImageList, image_body(first)); // no active image is representable
    TEST_ASSERT_EQUAL_UINT(0, parse(SmpCommand::ImageList, map({{"images",hex("80")}})).image_count);
    TEST_ASSERT_EQUAL_UINT(0, parse(SmpCommand::ImageList, map({{"images",hex("9fff")}})).image_count);
    reject(SmpCommand::ImageList, map({{"images", array({map(first),map(first)})}}));
    reject(SmpCommand::ImageList, map({{"images", array({map(first),map(second),map(second)})}}));
    first[6].second = hex("f5"); second[6].second = hex("f5");
    reject(SmpCommand::ImageList, map({{"images", array({map(first),map(second)})}}));
    for (const char* bad : {"02", "18ff", "1bffffffffffffffff"}) {
        first = image_fields(); first[0].second = hex(bad); reject(SmpCommand::ImageList, image_body(first));
    }
    for (size_t n : {size_t(0),size_t(31),size_t(33)}) {
        first = image_fields(); first[2].second = {0x58, static_cast<uint8_t>(n)};
        first[2].second.insert(first[2].second.end(),n,0); reject(SmpCommand::ImageList,image_body(first));
    }
    first = image_fields(); first.push_back({"image",hex("00")}); parse(SmpCommand::ImageList,image_body(first));
    first.back().second = hex("01"); reject(SmpCommand::ImageList,image_body(first));
    first.back().second = hex("f4"); reject(SmpCommand::ImageList,image_body(first));
    first.back().second = hex("00"); first.push_back(first.back()); reject(SmpCommand::ImageList,image_body(first));
    reject(SmpCommand::ImageList,map({{"images",hex("81a0")}}));
    reject(SmpCommand::ImageList,map({{"images",hex("8100")}}));
}
static void denial_variants_and_conflicts() {
    for (auto command : {SmpCommand::ImageList,SmpCommand::TagStatus,SmpCommand::Upload,SmpCommand::Trial,SmpCommand::Reset}) {
        for (bool indefinite : {false,true}) {
            auto r = parse(command,map({{"rc",hex("1affffffff")}},indefinite));
            TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpOutcome::Denied),static_cast<int>(r.outcome));
            TEST_ASSERT_EQUAL_UINT16(0,r.error_group); TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,r.error_code);
            r = parse(command,map({{"err",map({{"group",hex("19ffff")},{"rc",hex("01")}},indefinite)}},indefinite));
            TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpOutcome::Denied),static_cast<int>(r.outcome));
            TEST_ASSERT_EQUAL_UINT16(65535,r.error_group); TEST_ASSERT_EQUAL_UINT32(1,r.error_code);
        }
    }
    for (auto command : {SmpCommand::Trial,SmpCommand::Reset}) {
        parse(command,hex("a0")); parse(command,map({{"rc",hex("00")}}));
    }
    auto success = tag_fields(); success.push_back({"rc",hex("00")});
    captured_values(parse(SmpCommand::TagStatus,map(success)),true);
    for (const char* bad : {"20","f5","60","1b0000000100000000"}) reject(SmpCommand::Reset,map({{"rc",hex(bad)}}));
    reject(SmpCommand::Reset,map({{"rc",hex("00")},{"rc",hex("00")}}));
    const auto error = map({{"group",hex("00")},{"rc",hex("01")}});
    for (const char* rc : {"00","01"}) reject(SmpCommand::Reset,map({{"rc",hex(rc)},{"err",error}}));
    reject(SmpCommand::Reset,map({{"err",error},{"err",error}}));
    const Fields bad_errors[] = {{},{{"group",hex("00")}},{{"rc",hex("01")}},
        {{"group",hex("00")},{"rc",hex("00")}},
        {{"group",hex("1a00010000")},{"rc",hex("01")}},
        {{"group",hex("20")},{"rc",hex("01")}},
        {{"group",hex("00")},{"rc",hex("f5")}},
        {{"group",hex("00")},{"rc",hex("20")}},
        {{"group",hex("00")},{"rc",hex("1b0000000100000000")}},
        {{"group",hex("00")},{"group",hex("00")},{"rc",hex("01")}},
        {{"group",hex("00")},{"rc",hex("01")},{"rc",hex("01")}}};
    for (const auto& fields : bad_errors) reject(SmpCommand::Reset,map({{"err",map(fields)}}));
    reject(SmpCommand::Reset,map({{"err",hex("00")}}));
    reject(SmpCommand::Reset,map({{"rc",hex("01")},{"unknown",hex("9f00")}}));
    reject(SmpCommand::Reset,hex("a162726301a0"));
}
static void cbor_malformed_and_unknown_value_types() {
    // Every malformed value is reached as an unknown field, including after denial.
    const char* bad[] = {"", "ff", "1c", "1d", "1e", "1f", "18", "1901", "1a000001", "1b00000000000000",
        "580201", "780261", "5f40ff", "7f60ff", "9f00", "bf616100", "bf6161ff", "a16161", "81", "a100",
        "d80000", "c000", "f0", "f7", "f814", "f818", "f9ff", "fa000000", "fb00000000000000",
        "5bffffffffffffffff", "7bffffffffffffffff", "9bffffffffffffffff", "bbffffffffffffffff",
        "59000100", "79000161", "9800", "b800", "6180", "62c080", "63eda080", "64f4908080"};
    for (const char* value : bad) {
        reject(SmpCommand::Trial,map({{"unknown",hex(value)}}));
        reject(SmpCommand::Trial,map({{"rc",hex("01")},{"unknown",hex(value)}}));
    }
    for (const char* root : {"", "80", "00", "ff", "a0a0", "bf", "bf6161ff", "a100", "a06161"}) reject(SmpCommand::Trial,hex(root));
    for (const char* good : {"00", "1bffffffffffffffff", "20", "3bffffffffffffffff", "f4", "f5", "f6", "40", "60",
        "430001ff", "63e282ac", "f93c00", "fa3f800000", "fb3ff0000000000000", "a1019ff6ff", "9fbf616100ffff"}) {
        parse(SmpCommand::Trial,map({{"unknown",hex(good)}}));
    }
}
static void cbor_depth_and_entry_limits() {
    for (bool indefinite : {false,true}) {
        Bytes nested = hex("00");
        for (int i=0;i<7;++i) nested = array({nested},indefinite);
        parse(SmpCommand::Reset,map({{"x",nested}},indefinite)); // root counts as depth one
        nested = array({nested},indefinite);
        reject(SmpCommand::Reset,map({{"x",nested}},indefinite));
        std::vector<Bytes> entries(64,hex("00"));
        parse(SmpCommand::Reset,map({{"x",array(entries,indefinite)}}));
        entries.push_back(hex("00")); reject(SmpCommand::Reset,map({{"x",array(entries,indefinite)}}));
        Fields fields(64,{"x",hex("00")});
        parse(SmpCommand::Reset,map(fields,indefinite));
        parse(SmpCommand::Reset,map({{"x",map(fields,indefinite)}}));
        fields.push_back({"x",hex("00")});
        reject(SmpCommand::Reset,map(fields,indefinite));
        reject(SmpCommand::Reset,map({{"x",map(fields,indefinite)}}));
    }
}

static void every_captured_payload_truncation_is_rejected() {
    for (bool status : {false, true}) {
        const auto wire = hex(status ? k4w_smp_fixture::status_response_hex : k4w_smp_fixture::image_response_hex);
        const Bytes payload(wire.begin() + 8, wire.end());
        for (size_t n = 0; n < payload.size(); ++n)
            reject(status ? SmpCommand::TagStatus : SmpCommand::ImageList,
                   Bytes(payload.begin(), payload.begin() + n));
    }
}

static void success_zero_false_and_nonminimal_integer_values() {
    auto tag = tag_fields();
    tag[1].second = hex("1b0000000000000001"); // valid uint64 representation of tag 1
    tag[3].second = hex("f4");
    tag[4].second = hex("00");
    tag[5].second = hex("00");
    for (size_t i = 6; i < 9; ++i) tag[i].second = hex("f4");
    auto r = parse(SmpCommand::TagStatus, map(tag));
    TEST_ASSERT_EQUAL_UINT16(1, r.tag.tag);
    TEST_ASSERT_FALSE(r.tag.confirmed || r.tag.maintenance || r.tag.radio_ok || r.tag.ble_ok);
    TEST_ASSERT_EQUAL_UINT64(0, r.tag.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(0, r.tag.reset_reason);
    auto img = image_fields();
    for (size_t i = 3; i < 8; ++i) img[i].second = hex("f4");
    r = parse(SmpCommand::ImageList, image_body(img));
    TEST_ASSERT_EQUAL_UINT8(0, r.images[0].slot);
    TEST_ASSERT_FALSE(r.images[0].bootable || r.images[0].pending || r.images[0].confirmed ||
                      r.images[0].active || r.images[0].permanent);
    parse(SmpCommand::Reset, map({{"rc",hex("1800")}}));
    for (auto command : {SmpCommand::TagStatus, SmpCommand::ImageList, SmpCommand::Upload})
        reject(command, map({{"rc", hex("00")}}));
}

static void nested_map_depth_error_extensions_and_utf8_truncations() {
    for (bool indefinite : {false, true}) {
        Bytes nested = hex("00");
        for (int i = 0; i < 7; ++i) nested = map({{"x", nested}}, indefinite);
        parse(SmpCommand::Reset, map({{"x", nested}}, indefinite));
        reject(SmpCommand::Reset, map({{"x", map({{"x", nested}}, indefinite)}}, indefinite));
        auto err = map({{"extra", array({hex("f6"), hex("fa3f800000")}, indefinite)},
                        {"rc", hex("18ff")}, {"group", hex("00")}}, indefinite);
        auto r = parse(SmpCommand::Reset, map({{"err", err}}, indefinite));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpOutcome::Denied), static_cast<int>(r.outcome));
        TEST_ASSERT_EQUAL_UINT32(255, r.error_code);
        reject(SmpCommand::Reset, map({{"err", err}, {"rc", hex("00")}}, indefinite));
        auto img = image_fields();
        reject(SmpCommand::ImageList, map({{"images",array({map(img),map(img),map(img)}, indefinite)}}));
    }
    for (const char* bad : {"61c2", "62e282", "63f09f98", "63e08080", "64f0808080", "61ff"}) {
        auto tag = tag_fields(); tag[2].second = hex(bad);
        reject(SmpCommand::TagStatus, map(tag));
        reject(SmpCommand::Trial, map({{"x", hex(bad)}}));
    }
    parse(SmpCommand::Trial, hex("a10000")); // unknown non-text map key
    parse(SmpCommand::Trial, map({{"x",hex("80")},{"x",hex("a0")}})); // only known keys are unique
}

static void abort_during_payload_and_late_packets_cannot_reuse_reply() {
    const auto wire = hex(k4w_smp_fixture::image_response_hex);
    SmpAssembler a;
    TEST_ASSERT_TRUE(a.begin(SmpCommand::ImageList, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::More),static_cast<int>(a.feed(wire.data(),wire.size()-1)));
    a.abort();
    TEST_ASSERT_NULL(a.reply());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error),static_cast<int>(a.feed(&wire.back(),1)));
    TEST_ASSERT_FALSE(a.begin(SmpCommand::ImageList, 0));
    a.abort();
    TEST_ASSERT_TRUE(a.begin(SmpCommand::ImageList, 0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Complete),static_cast<int>(a.feed(wire.data(),wire.size())));
    const SmpReply copied = *a.reply();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Error),static_cast<int>(a.feed(wire.data(),wire.size())));
    TEST_ASSERT_NULL(a.reply());
    captured_values(copied,false);
    a.abort();
    TEST_ASSERT_TRUE(a.begin(SmpCommand::Trial, 200));
    const auto trial = packet(SmpCommand::Trial,hex("a0"),200);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SmpFeed::Complete),static_cast<int>(a.feed(trial.data(),trial.size())));
    TEST_ASSERT_EQUAL_UINT(0,a.reply()->image_count);
    TEST_ASSERT_EQUAL_UINT8(0,a.reply()->images[0].hash[0]);
    a.abort();
    TEST_ASSERT_NULL(a.reply());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(request_mappings_and_capacity);
    RUN_TEST(upload_wire_and_boundaries);
    RUN_TEST(offsets_resume_stall_and_overflow);
    RUN_TEST(captured_packets_every_split_and_byte);
    RUN_TEST(definite_equivalents_and_mixed_containers);
    RUN_TEST(session_lifecycle_and_stale_success);
    RUN_TEST(framing_mismatches_lengths_and_extra_packets);
    RUN_TEST(known_required_fields_duplicates_and_types);
    RUN_TEST(identity_version_utf8_and_numeric_bounds);
    RUN_TEST(image_array_slots_hash_and_active_bounds);
    RUN_TEST(denial_variants_and_conflicts);
    RUN_TEST(cbor_malformed_and_unknown_value_types);
    RUN_TEST(cbor_depth_and_entry_limits);
    RUN_TEST(every_captured_payload_truncation_is_rejected);
    RUN_TEST(success_zero_false_and_nonminimal_integer_values);
    RUN_TEST(nested_map_depth_error_extensions_and_utf8_truncations);
    RUN_TEST(abort_during_payload_and_late_packets_cannot_reuse_reply);
    return UNITY_END();
}
