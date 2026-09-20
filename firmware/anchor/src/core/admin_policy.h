#pragma once

#include <cstddef>
#include <cstdint>

// These checks are deliberately Arduino-free: adapters must use the same
// policy for HTTP and every multipart callback.
bool admin_password_valid(const char *password, size_t length);
bool admin_digest_syntax_valid(const char *header, const char *actual_uri);
bool admin_origin_matches_host(const char *origin, const char *host);
bool admin_constant_time_equal(const char *left, const char *right);

class AdminRateLimit {
  public:
    AdminRateLimit();
    void record_failure(bool credential_attempt, uint32_t now);
    void record_success();
    bool blocked(uint32_t now) const;

  private:
    uint32_t failures_[5];
    uint8_t failure_count_;
    uint32_t cooldown_started_;
    bool cooling_down_;
};

class AdminRequestGate {
  public:
    enum class Decision : uint8_t { Allow, Disabled, Challenge, RateLimited, Forbidden };
    AdminRequestGate();
    void configure(const char *csrf);
    Decision authorize(bool authenticated, bool credential_supplied, bool mutation,
                       const char *csrf, const char *origin, const char *host,
                       uint32_t now);
    void disable();
    bool configured() const { return configured_; }

  private:
    char csrf_[65];
    bool configured_;
    AdminRateLimit rate_limit_;
};

class AdminUploadIo {
  public:
    virtual ~AdminUploadIo() = default;
    virtual bool suspend() = 0;
    virtual void resume() = 0;
    virtual bool begin() = 0;
    virtual bool write(const uint8_t *bytes, size_t length) = 0;
    virtual bool finish() = 0;
    virtual void abort() = 0;
};

class AdminUploadSession {
  public:
    explicit AdminUploadSession(AdminUploadIo &io);
    bool start(bool authorized, uint32_t now);
    bool write(bool authorized, const uint8_t *bytes, size_t length, uint32_t now);
    bool end(bool authorized, uint32_t now);
    bool finalize(bool authorized, uint32_t now);
    void abort();
    // Request boundary only: abort/resume unfinished work before resetting.
    void clear();
    bool failed() const { return failed_; }

  private:
    AdminUploadIo &io_;
    bool active_;
    bool suspended_;
    bool file_ended_;
    bool failed_;
    bool finalized_;
    size_t bytes_;
    uint32_t last_activity_;
    bool alive(uint32_t now);
    void fail();
};

// One synchronous WebServer::handleClient() attempt. No state may cross calls,
// including parser exits that omit both the final handler and ABORT callback.
class AdminUploadScope {
  public:
    explicit AdminUploadScope(AdminUploadSession &session) : session_(session) {
        session_.clear();
    }
    ~AdminUploadScope() { session_.clear(); }
    AdminUploadScope(const AdminUploadScope &) = delete;
    AdminUploadScope &operator=(const AdminUploadScope &) = delete;

  private:
    AdminUploadSession &session_;
};
