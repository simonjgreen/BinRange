#include "core/update_manifest.h"

#include <cstring>
#include <limits>

namespace {

constexpr size_t kMaxManifestBytes = 512;
constexpr char kTarget[] = "k4w/nrf52833/dw3110";

bool whitespace(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

void skip_whitespace(const char *&cursor, const char *end) {
    while (cursor != end && whitespace(*cursor)) ++cursor;
}

bool string(const char *&cursor, const char *end, const char *&value, size_t &length) {
    if (cursor == end || *cursor++ != '\"') return false;
    value = cursor;
    while (cursor != end && *cursor != '\"') {
        const unsigned char character = static_cast<unsigned char>(*cursor);
        if (character < 0x20 || *cursor == '\\') return false;
        ++cursor;
    }
    if (cursor == end) return false;
    length = static_cast<size_t>(cursor - value);
    ++cursor;
    return true;
}

bool equals(const char *value, size_t length, const char *wanted) {
    return std::strlen(wanted) == length && std::memcmp(value, wanted, length) == 0;
}

bool number(const char *&cursor, const char *end, uint32_t &out) {
    if (cursor == end || *cursor < '0' || *cursor > '9') return false;
    if (*cursor == '0' && cursor + 1 != end && cursor[1] >= '0' && cursor[1] <= '9')
        return false;
    uint32_t value = 0;
    do {
        const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
        if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
        ++cursor;
    } while (cursor != end && *cursor >= '0' && *cursor <= '9');
    out = value;
    return true;
}

bool hex_hash(const char *value, size_t length, uint8_t out[32]) {
    if (length != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        const char high = value[i * 2];
        const char low = value[i * 2 + 1];
        if (!((high >= '0' && high <= '9') || (high >= 'a' && high <= 'f')) ||
            !((low >= '0' && low <= '9') || (low >= 'a' && low <= 'f')))
            return false;
        const uint8_t high_value = static_cast<uint8_t>(high <= '9' ? high - '0' : high - 'a' + 10);
        const uint8_t low_value = static_cast<uint8_t>(low <= '9' ? low - '0' : low - 'a' + 10);
        out[i] = static_cast<uint8_t>((high_value << 4) | low_value);
    }
    return true;
}

bool version(const char *value, size_t length, char out[24]) {
    if (length == 0 || length >= 24) return false;
    for (size_t i = 0; i < length; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || value[i] == '.' || value[i] == '+'))
            return false;
    std::memcpy(out, value, length);
    out[length] = '\0';
    return true;
}

}  // namespace

bool parse_update_manifest(const char *json, size_t length, UpdateRelease &out) {
    std::memset(&out, 0, sizeof(out));
    if (!json || length == 0 || length > kMaxManifestBytes) return false;
    UpdateRelease parsed = {};
    for (size_t i = 0; i < length; ++i)
        if (json[i] == '\0') return false;

    const char *cursor = json;
    const char *const end = json + length;
    skip_whitespace(cursor, end);
    if (cursor == end || *cursor++ != '{') return false;

    uint8_t seen = 0;
    while (true) {
        skip_whitespace(cursor, end);
        if (cursor == end || *cursor != '\"') return false;
        const char *key;
        size_t key_length;
        if (!string(cursor, end, key, key_length)) return false;
        skip_whitespace(cursor, end);
        if (cursor == end || *cursor++ != ':') return false;
        skip_whitespace(cursor, end);

        uint8_t field;
        if (equals(key, key_length, "schema")) field = 1;
        else if (equals(key, key_length, "target")) field = 2;
        else if (equals(key, key_length, "size")) field = 4;
        else if (equals(key, key_length, "file_sha256")) field = 8;
        else if (equals(key, key_length, "image_hash")) field = 16;
        else if (equals(key, key_length, "version")) field = 32;
        else return false;
        if (seen & field) return false;

        if (field == 1 || field == 4) {
            uint32_t value;
            if (!number(cursor, end, value)) return false;
            if ((field == 1 && value != 1) ||
                (field == 4 && (value < 32 || value > 212992))) return false;
            if (field == 4) parsed.size = value;
        } else {
            const char *value;
            size_t value_length;
            if (!string(cursor, end, value, value_length)) return false;
            if ((field == 2 && !equals(value, value_length, kTarget)) ||
                (field == 8 && !hex_hash(value, value_length, parsed.file_sha)) ||
                (field == 16 && !hex_hash(value, value_length, parsed.image_hash)) ||
                (field == 32 && !version(value, value_length, parsed.version))) return false;
        }
        seen |= field;
        skip_whitespace(cursor, end);
        if (cursor == end) return false;
        if (*cursor == '}') {
            ++cursor;
            break;
        }
        if (*cursor++ != ',') return false;
    }
    skip_whitespace(cursor, end);
    if (cursor != end || seen != 63) return false;
    out = parsed;
    return true;
}
