#pragma once

#include <cstddef>
#include <cstdint>

// Update policy stays independent of Arduino, BLE, storage, and transport so
// the safety rules can be exercised by native tests.
enum class UpdatePhase : uint8_t {
    Queued, Waiting, Connecting, Uploading, Rebooting, Checking,
    Successful, Failed, Cancelled, NeedsRelease, NeedsAction
};

struct UpdateTarget {
    uint16_t tag;
    char hardware_id[17];
};

struct UpdateRelease {
    uint32_t size;
    uint8_t file_sha[32];
    uint8_t image_hash[32];
    char version[24];
};

class UpdateJob;

// Detached persistence DTO, never an alias for queue-owned storage. Uptime is
// deliberately absent: restoration starts a new monotonic epoch. Wall time is
// snapshot metadata, not policy state. Imports must go through restore().
struct UpdateJobState {
    uint32_t id;
    UpdateTarget target;
    UpdateRelease release;
    UpdatePhase phase;
    uint32_t acknowledged;
    uint8_t attempts;
    char error[80];
    bool trial_may_be_armed;
};

UpdateJobState update_export_state(const UpdateJob &job);

// Only these states have no permitted future policy transition.
bool update_terminal(UpdatePhase phase);

// Recovery never authorizes resuming a saved transfer offset. The caller must
// obtain a release again before a new upload; Rebooting/Checking require a
// device inspection instead.
UpdatePhase update_after_restart(UpdatePhase saved);

// A release is complete only after an explicit device confirmation and an
// exact active-image hash match. Version text and byte counts are insufficient.
bool update_confirmed_match(const UpdateRelease &wanted,
                            const uint8_t active_hash[32], bool confirmed);

// Approved ordinary transition table:
// Queued -> Waiting, Cancelled, Failed
// Waiting -> Connecting, Cancelled, Failed, NeedsRelease
// Connecting -> Waiting, Uploading, Checking, Cancelled, Failed, NeedsAction
// Uploading -> Waiting, Rebooting, Cancelled, Failed, NeedsRelease, NeedsAction
// Rebooting -> Checking, NeedsAction
// Checking -> Failed, NeedsAction (Successful is guarded by update_complete)
// NeedsRelease -> Waiting, Cancelled
// NeedsAction -> Checking, Cancelled (only without trial uncertainty)
// Terminal states are immutable. Timestamps must not regress. Entering
// Rebooting arms trial_may_be_armed before any external hardware action.
bool update_transition(UpdateJob &job, UpdatePhase next, uint64_t now);

// Records receiver-confirmed transfer progress only while Uploading. It
// accepts a receiver-directed rewind but rejects offsets beyond release.size
// and timestamps earlier than the existing state.
bool update_progress(UpdateJob &job, uint32_t acknowledged, uint64_t now);

// Count connections before checkpointing. Only an explicit operator retry
// resets the three-attempt budget; uncertain trials remain inspection-only.
bool update_attempt(UpdateJob &job, uint64_t now);
bool update_retry(UpdateJob &job, uint64_t now);
bool update_error(UpdateJob &job, const char *error, uint64_t now);

// Cancels safely. If a trial could be armed, the job returns to Checking
// rather than claiming an installation was undone; otherwise it is Cancelled.
bool update_cancel(UpdateJob &job, uint64_t now);

// The sole path to Successful. It is valid only while Checking and requires
// update_confirmed_match; failure leaves the job available for inspection.
bool update_complete(UpdateJob &job, const uint8_t active_hash[32],
                     bool confirmed, uint64_t now);

// A queue-owned policy record. Identity, release, ID, and state are private:
// callers can inspect them through read-only accessors, while UpdateQueue and
// the policy helpers above are the only mutation paths. Detached persistence
// state can only enter through the queue's validated restoration boundary.
class UpdateJob {
  public:
    uint32_t id() const { return id_; }
    const UpdateTarget &target() const { return target_; }
    const UpdateRelease &release() const { return release_; }
    UpdatePhase phase() const { return phase_; }
    uint32_t acknowledged() const { return acknowledged_; }
    uint8_t attempts() const { return attempts_; }
    uint64_t changed_ms() const { return changed_ms_; }
    const char *error() const { return error_; }
    bool trial_may_be_armed() const { return trial_may_be_armed_; }

  private:
    UpdateJob()
        : id_(0), target_{}, release_{}, phase_(UpdatePhase::Queued),
          acknowledged_(0), attempts_(0), changed_ms_(0), error_{},
          trial_may_be_armed_(false) {}
    // Queue slot replacement is controlled internally; callers cannot replace
    // an existing job and thereby bypass identity or transition policy.
    UpdateJob(const UpdateJob &) = default;
    UpdateJob(UpdateJob &&) = default;
    UpdateJob &operator=(const UpdateJob &) = default;
    UpdateJob &operator=(UpdateJob &&) = default;

    uint32_t id_;
    UpdateTarget target_;
    UpdateRelease release_;
    UpdatePhase phase_;
    uint32_t acknowledged_;
    uint8_t attempts_;
    uint64_t changed_ms_;
    char error_[80];
    bool trial_may_be_armed_;

    friend class UpdateQueue;
    friend bool update_transition(UpdateJob &job, UpdatePhase next, uint64_t now);
    friend bool update_progress(UpdateJob &job, uint32_t acknowledged, uint64_t now);
    friend bool update_attempt(UpdateJob &job, uint64_t now);
    friend bool update_retry(UpdateJob &job, uint64_t now);
    friend bool update_error(UpdateJob &job, const char *error, uint64_t now);
    friend bool update_cancel(UpdateJob &job, uint64_t now);
    friend bool update_complete(UpdateJob &job, const uint8_t active_hash[32],
                                bool confirmed, uint64_t now);
};

#define UPDATE_QUEUE_MAX 16

// Fixed storage for update policy records. A full queue never evicts live
// work; it replaces only the oldest terminal record and never wraps job IDs.
class UpdateQueue {
  public:
    // first_id exists to make ID exhaustion testable. Zero means exhausted.
    explicit UpdateQueue(uint32_t first_id = 1);

    // Returns nullptr for malformed input, duplicate live targets, an all-live
    // full queue, or exhausted IDs. A target tag is 1..0xfffe and its hardware
    // ID is exactly 16 lowercase hex characters. Valid releases are 32..212992
    // bytes.
    UpdateJob *enqueue(const UpdateTarget &target, const UpdateRelease &release,
                       uint64_t now);
    UpdateJob *find(uint32_t id);
    const UpdateJob *find(uint32_t id) const;
    UpdateJob *at(size_t index) { return index < size_ ? &jobs_[index] : nullptr; }
    const UpdateJob *at(size_t index) const {
        return index < size_ ? &jobs_[index] : nullptr;
    }
    size_t size() const { return size_; }

    // All records are validated before replacement. Null is allowed only for
    // count=0. Live tags must be unique, including across hardware replacements.
    // Trial uncertainty is valid only in Rebooting/Checking/NeedsAction or
    // Successful/Failed history. Checking/Successful do not require a trial.
    static bool valid_restore(const UpdateJobState *states, size_t count);
    // Replace the queue, preserving IDs and applying update_after_restart.
    // Live offsets become zero; every changed_ms becomes new_boot_now. The next
    // ID exceeds the maximum imported ID (UINT32_MAX exhausts); empty starts at
    // 1. Failure preserves all prior jobs and ID allocation state.
    bool restore(const UpdateJobState *states, size_t count, uint64_t new_boot_now);

  private:
    static bool valid_target(const UpdateTarget &target);
    static bool valid_release(const UpdateRelease &release);
    static bool same_target(const UpdateTarget &a, const UpdateTarget &b);

    UpdateJob jobs_[UPDATE_QUEUE_MAX];
    size_t size_;
    uint32_t next_id_;
    bool id_exhausted_;
};
