#pragma once

#include <cstddef>
#include <cstdint>

#include "core/smp_codec.h"
#include "core/update_snapshot.h"

enum class UpdateQueueResult : uint8_t { Accepted, Conflict, Invalid, Capacity, NeedsAction, Error };
enum class UpdatePairingPhase : uint8_t {
    Idle, Connecting, Securing, Checking, Disconnecting, Successful, Failed
};

// Controller-owned asynchronous boundary. The worker must copy each submitted
// command and return an event with the exact operation/session IDs. submit(false)
// means nothing was queued. Disconnect is idempotent, cancels pending operations,
// and returns Disconnected only after all session access is quiescent.
enum class UpdateTransportCommandKind : uint8_t { Connect, Security, Exchange, Disconnect };
enum class UpdateTransportEventKind : uint8_t { Connected, Secured, Reply, Disconnected, Failed };
enum class UpdateTransportFailure : uint8_t { Transient, Security, Identity, Protocol, Unavailable };

struct UpdateTransportCommand {
    UpdateTransportCommandKind kind;
    uint32_t operation;
    uint32_t session;
    UpdateAssociation association;
    SmpCommand smp;
    uint8_t frame[512];
    size_t frame_size;
    // Explicit commissioning only; ordinary controller commands leave these
    // zero. The sender must erase its PIN copy after submit. Worker erases its
    // copy after security completes or the session closes.
    bool commissioning = false;
    uint32_t passkey = 0;
};

struct UpdateTransportEvent {
    UpdateTransportEventKind kind;
    uint32_t operation;
    uint32_t session;
    bool bonded_mitm_sc;
    SmpReply reply;
    UpdateTransportFailure failure = UpdateTransportFailure::Transient;
};

class UpdateControllerTransport {
  public:
    virtual ~UpdateControllerTransport() = default;
    virtual bool submit(const UpdateTransportCommand &command) = 0;
    virtual bool poll(UpdateTransportEvent &event) = 0;
};

class UpdateControllerStorage {
  public:
    virtual ~UpdateControllerStorage() = default;
    virtual bool begin(UpdateQueue &queue, uint64_t now) = 0;
    virtual bool checkpoint(const UpdateSnapshot &snapshot) = 0;
    virtual const UpdateSnapshot &snapshot() const = 0;
    virtual const UpdateRelease *staged_release() const = 0;
    virtual bool acquire(const UpdateRelease &release, const uint8_t *&bytes) = 0;
    virtual void release() = 0;
    virtual size_t capacity() const = 0;
};

// Keep this bounded but sizeable snapshot in main-loop-owned static storage,
// not on the ESP32 loop task's small stack. Observations are volatile and their
// seen_ms must be used to label stale data; persisted job state is separate.
struct UpdateControllerSnapshot {
    size_t association_count;
    size_t job_count;
    UpdateAssociation associations[UPDATE_SNAPSHOT_MAX_ASSOCIATIONS];
    UpdateJobState jobs[UPDATE_QUEUE_MAX];
    uint32_t active_job_id;
    size_t staging_capacity;
    bool staging_available;
    bool disabled;
    bool disconnecting;
    char error[80];
    struct Pairing {
        UpdatePairingPhase phase;
        UpdateAssociation association;
        char error[80];
        uint64_t changed_ms;
    } pairing;
    uint64_t changed_ms[UPDATE_QUEUE_MAX];
    struct Observation {
        bool identity_valid;
        bool image_valid;
        SmpTag tag;
        SmpImage active;
        uint64_t seen_ms;
    } observed[UPDATE_QUEUE_MAX];
};

class UpdateController {
  public:
    UpdateController(UpdateControllerStorage *storage, UpdateControllerTransport *transport);
    bool begin(uint64_t now);
    UpdateQueueResult queue(uint16_t adopted_tag, uint32_t *job_id, uint64_t now);
    UpdateQueueResult commission(const UpdateAssociation &association, uint32_t pin, uint64_t now);
    bool cancel(uint32_t job_id, uint64_t now);
    bool retry(uint32_t job_id, uint64_t now);
    void loop(uint64_t now);
    bool snapshot(UpdateControllerSnapshot &out) const;
    bool busy() const;

  private:
    UpdateAssociation *association(uint16_t tag);
    const UpdateAssociation *association(uint16_t tag) const;
    bool checkpoint();
    bool transition(UpdateJob &job, UpdatePhase phase, uint64_t now);
    bool submit(UpdateTransportCommandKind kind, SmpCommand smp, uint64_t now);
    void stop(UpdatePhase phase, const char *error, uint64_t now, uint64_t delay = 0);
    void failed(const char *error, uint64_t now, bool retryable);
    void close(uint64_t now, uint64_t delay = 0);
    void finish_close();
    void disable(const char *error);
    void handle(const UpdateTransportEvent &event, uint64_t now);
    void start(UpdateJob &job, uint64_t now);
    void inspect_or_upload(const SmpReply &reply, uint64_t now);
    bool active_image(const SmpReply &reply, SmpImage &out) const;
    bool pending_image(const SmpReply &reply) const;
    size_t index(uint32_t job_id) const;
    bool prepare_check(UpdateJob &job, uint64_t now);
    bool valid_association(const UpdateAssociation &association) const;
    bool same_association(const UpdateAssociation &a, const UpdateAssociation &b) const;
    bool pairing_active() const;
    void pairing_fail(const char *error, uint64_t now);
    void pairing_finish_close(uint64_t now);

    enum class Await : uint8_t { None, Connected, Secured, Reply, Poll, Disconnected };

    UpdateControllerStorage *storage_;
    UpdateControllerTransport *transport_;
    UpdateQueue queue_;
    UpdateAssociation associations_[UPDATE_SNAPSHOT_MAX_ASSOCIATIONS];
    size_t association_count_;
    uint32_t active_job_id_;
    uint32_t operation_;
    uint32_t session_;
    uint64_t deadline_;
    uint64_t session_started_;
    uint64_t cooldown_until_;
    uint8_t sequence_;
    uint8_t stalled_;
    SmpCommand pending_smp_;
    uint32_t sent_offset_;
    uint32_t sent_end_;
    const uint8_t *stage_bytes_;
    bool stage_borrowed_;
    bool disabled_;
    UpdateSnapshot checkpoint_scratch_;
    bool begun_ = false;
    bool ready_ = false;
    Await await_ = Await::None;
    bool disconnect_submitted_ = false;
    uint64_t next_disconnect_try_ = 0;
    uint64_t checking_until_[UPDATE_QUEUE_MAX]{};
    UpdateControllerSnapshot::Observation observed_[UPDATE_QUEUE_MAX]{};
    char error_[80]{};
    UpdatePairingPhase pairing_phase_ = UpdatePairingPhase::Idle;
    UpdateAssociation pairing_association_{};
    uint32_t pairing_pin_ = 0;
    bool pairing_success_ = false;
    char pairing_error_[80]{};
    uint64_t pairing_changed_ms_ = 0;
};
