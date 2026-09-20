#include "core/admin_policy.h"

#include <cstring>

namespace {
constexpr uint32_t kRateWindowMs = 60000;
constexpr uint32_t kUploadIdleMs = 30000;
constexpr size_t kMaxHeader = 768;
constexpr size_t kMaxHost = 128;

bool elapsed(uint32_t now, uint32_t then, uint32_t duration) {
    return static_cast<uint32_t>(now - then) >= duration;
}

bool printable_ascii(const char *value, size_t length) {
    for (size_t i = 0; i < length; ++i)
        if (value[i] < 0x20 || value[i] > 0x7e) return false;
    return true;
}

bool hex8(const char *value) {
    if (strlen(value) != 8) return false;
    for (size_t i = 0; i < 8; ++i) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

struct DigestField {
    const char *name;
    char value[129];
    bool quoted;
    bool seen;
};

int digest_field(DigestField *fields, size_t count, const char *name,
                 size_t length) {
    for (size_t i = 0; i < count; ++i)
        if (strlen(fields[i].name) == length &&
            memcmp(fields[i].name, name, length) == 0)
            return static_cast<int>(i);
    return -1;
}

bool valid_host(const char *host) {
    const size_t length = host ? strlen(host) : 0;
    if (!length || length > kMaxHost) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = host[i];
        if (c <= 0x20 || c >= 0x7f || c == '@' || c == '/' || c == '?' ||
            c == '#')
            return false;
    }
    return true;
}
}  // namespace

bool admin_password_valid(const char *password, size_t length) {
    return password && length >= 12 && length <= 64 &&
           printable_ascii(password, length);
}

bool admin_constant_time_equal(const char *left, const char *right) {
    if (!left || !right) return false;
    const size_t left_len = strlen(left);
    const size_t right_len = strlen(right);
    unsigned char different = static_cast<unsigned char>(left_len ^ right_len);
    const size_t max_len = left_len > right_len ? left_len : right_len;
    for (size_t i = 0; i < max_len; ++i) {
        const unsigned char a = i < left_len ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right_len ? static_cast<unsigned char>(right[i]) : 0;
        different |= static_cast<unsigned char>(a ^ b);
    }
    return different == 0;
}

bool admin_digest_syntax_valid(const char *header, const char *actual_uri) {
    if (!header || !actual_uri || strncmp(header, "Digest ", 7) != 0 ||
        strlen(header) > kMaxHeader || strlen(actual_uri) > kMaxHost)
        return false;
    DigestField fields[] = {
        {"username", {}, true, false}, {"realm", {}, true, false},
        {"nonce", {}, true, false},    {"uri", {}, true, false},
        {"response", {}, true, false}, {"opaque", {}, true, false},
        {"qop", {}, false, false},     {"nc", {}, false, false},
        {"cnonce", {}, true, false},
    };
    const char *cursor = header + 7;
    while (*cursor) {
        while (*cursor == ' ') ++cursor;
        const char *name = cursor;
        while ((*cursor >= 'a' && *cursor <= 'z') ||
               (*cursor >= 'A' && *cursor <= 'Z') || *cursor == '-')
            ++cursor;
        if (cursor == name || *cursor++ != '=') return false;
        const int index = digest_field(fields, sizeof(fields) / sizeof(fields[0]),
                                       name, static_cast<size_t>(cursor - name - 1));
        if (index < 0 || fields[index].seen) return false;
        const bool quoted = *cursor == '"';
        if (quoted) ++cursor;
        // WebServer and common Digest clients accept both qop=auth and
        // qop="auth". The decoded value must still be exactly auth below.
        if (quoted != fields[index].quoted && strcmp(fields[index].name, "qop") != 0)
            return false;
        const char *value = cursor;
        while (*cursor && (quoted ? *cursor != '"' : *cursor != ',' && *cursor != ' ')) {
            if (static_cast<unsigned char>(*cursor) < 0x20 || *cursor == '\\') return false;
            ++cursor;
        }
        const size_t length = static_cast<size_t>(cursor - value);
        if (!length || length > kMaxHost) return false;
        if (quoted && *cursor++ != '"') return false;
        if (!quoted && *cursor == ' ') while (*cursor == ' ') ++cursor;
        if (*cursor && *cursor != ',') return false;
        memcpy(fields[index].value, value, length);
        fields[index].value[length] = '\0';
        fields[index].seen = true;
        if (*cursor == ',') {
            ++cursor;
            if (!*cursor) return false;
        }
        else if (*cursor) return false;
    }
    bool valid = true;
    for (const DigestField &field : fields) valid = valid && field.seen;
    valid = valid && strcmp(fields[0].value, "admin") == 0 &&
            strcmp(fields[1].value, "BinRange Admin") == 0 &&
            strcmp(fields[3].value, actual_uri) == 0 &&
            strcmp(fields[6].value, "auth") == 0 && hex8(fields[7].value);
    return valid;
}

bool admin_origin_matches_host(const char *origin, const char *host) {
    if (!origin || !*origin) return true;
    if (!valid_host(host) || strncmp(origin, "http://", 7) != 0) return false;
    const char *origin_host = origin + 7;
    return valid_host(origin_host) && admin_constant_time_equal(origin_host, host);
}

AdminRateLimit::AdminRateLimit()
    : failures_{}, failure_count_(0), cooldown_started_(0), cooling_down_(false) {}

bool AdminRateLimit::blocked(uint32_t now) const {
    return cooling_down_ && !elapsed(now, cooldown_started_, kRateWindowMs);
}

void AdminRateLimit::record_failure(bool credential_attempt, uint32_t now) {
    if (!credential_attempt || blocked(now)) return;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < failure_count_; ++i)
        if (!elapsed(now, failures_[i], kRateWindowMs)) failures_[kept++] = failures_[i];
    failure_count_ = kept;
    if (failure_count_ < 5) failures_[failure_count_++] = now;
    if (failure_count_ == 5) {
        cooling_down_ = true;
        cooldown_started_ = now;
    }
}

void AdminRateLimit::record_success() {
    failure_count_ = 0;
    cooling_down_ = false;
}

AdminRequestGate::AdminRequestGate() : csrf_{}, configured_(false), rate_limit_() {}

void AdminRequestGate::configure(const char *csrf) {
    configured_ = csrf && strlen(csrf) == 64 && printable_ascii(csrf, 64);
    if (configured_) memcpy(csrf_, csrf, sizeof(csrf_));
}

void AdminRequestGate::disable() {
    configured_ = false;
    memset(csrf_, 0, sizeof(csrf_));
    rate_limit_.record_success();
}

AdminRequestGate::Decision AdminRequestGate::authorize(
    bool authenticated, bool credential_supplied, bool mutation, const char *csrf,
    const char *origin, const char *host, uint32_t now) {
    if (!configured_) return Decision::Disabled;
    if (rate_limit_.blocked(now)) return Decision::RateLimited;
    if (!authenticated) {
        rate_limit_.record_failure(credential_supplied, now);
        return rate_limit_.blocked(now) ? Decision::RateLimited : Decision::Challenge;
    }
    rate_limit_.record_success();
    if (!mutation) return Decision::Allow;
    return admin_constant_time_equal(csrf, csrf_) &&
                   admin_origin_matches_host(origin, host)
               ? Decision::Allow
               : Decision::Forbidden;
}

AdminUploadSession::AdminUploadSession(AdminUploadIo &io)
    : io_(io), active_(false), suspended_(false), file_ended_(false),
      failed_(false), finalized_(false), bytes_(0), last_activity_(0) {}

bool AdminUploadSession::alive(uint32_t now) {
    if (active_ && elapsed(now, last_activity_, kUploadIdleMs)) fail();
    return active_ && !failed_;
}

void AdminUploadSession::fail() {
    if (failed_) return;
    if (suspended_) {
        io_.abort();
        io_.resume();
        suspended_ = false;
    }
    active_ = false;
    file_ended_ = false;
    failed_ = true;
}

bool AdminUploadSession::start(bool authorized, uint32_t now) {
    if (active_ || file_ended_ || failed_ || finalized_ || !authorized) {
        fail();
        return false;
    }
    if (!io_.suspend()) { fail(); return false; }
    suspended_ = true;
    if (!io_.begin()) {
        io_.resume();
        suspended_ = false;
        failed_ = true;
        return false;
    }
    active_ = true;
    last_activity_ = now;
    return true;
}

bool AdminUploadSession::write(bool authorized, const uint8_t *bytes, size_t length,
                               uint32_t now) {
    if (!authorized || !alive(now) || (!bytes && length) || length > SIZE_MAX - bytes_) {
        fail();
        return false;
    }
    if (!io_.write(bytes, length)) {
        fail();
        return false;
    }
    bytes_ += length;
    last_activity_ = now;
    return true;
}

bool AdminUploadSession::end(bool authorized, uint32_t now) {
    if (!authorized || !alive(now) || bytes_ == 0) {
        fail();
        return false;
    }
    active_ = false;
    file_ended_ = true;
    last_activity_ = now;
    return true;
}

bool AdminUploadSession::finalize(bool authorized, uint32_t now) {
    if (!authorized || failed_ || finalized_ || active_ || !file_ended_ ||
        elapsed(now, last_activity_, kUploadIdleMs)) {
        fail();
        return false;
    }
    if (!io_.finish()) {
        fail();
        return false;
    }
    // The caller now owns the restart. Keep ranging paused, but relinquish the
    // update resource so scope exit never aborts an already-selected image.
    finalized_ = true;
    file_ended_ = false;
    suspended_ = false;
    return true;
}

void AdminUploadSession::abort() { fail(); }

void AdminUploadSession::clear() {
    if (suspended_) fail();
    active_ = suspended_ = file_ended_ = failed_ = finalized_ = false;
    bytes_ = 0;
    last_activity_ = 0;
}
