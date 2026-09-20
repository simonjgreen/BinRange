#include "core/update_job.h"

#include <cstring>

namespace {

bool timestamp_ok(const UpdateJob &job, uint64_t now) {
    return now >= job.changed_ms();
}

bool allowed_edge(UpdatePhase from, UpdatePhase to) {
    switch (from) {
        case UpdatePhase::Queued:
            return to == UpdatePhase::Waiting || to == UpdatePhase::Cancelled ||
                   to == UpdatePhase::Failed;
        case UpdatePhase::Waiting:
            return to == UpdatePhase::Connecting || to == UpdatePhase::Cancelled ||
                   to == UpdatePhase::Failed || to == UpdatePhase::NeedsRelease;
        case UpdatePhase::Connecting:
            return to == UpdatePhase::Waiting || to == UpdatePhase::Uploading ||
                   to == UpdatePhase::Checking || to == UpdatePhase::Cancelled ||
                   to == UpdatePhase::Failed || to == UpdatePhase::NeedsAction;
        case UpdatePhase::Uploading:
            return to == UpdatePhase::Waiting || to == UpdatePhase::Rebooting ||
                   to == UpdatePhase::Cancelled || to == UpdatePhase::Failed ||
                   to == UpdatePhase::NeedsRelease || to == UpdatePhase::NeedsAction;
        case UpdatePhase::Rebooting:
            return to == UpdatePhase::Checking || to == UpdatePhase::NeedsAction;
        case UpdatePhase::Checking:
            return to == UpdatePhase::Failed || to == UpdatePhase::NeedsAction;
        case UpdatePhase::NeedsRelease:
            return to == UpdatePhase::Waiting || to == UpdatePhase::Cancelled;
        case UpdatePhase::NeedsAction:
            return to == UpdatePhase::Checking || to == UpdatePhase::Cancelled;
        case UpdatePhase::Successful:
        case UpdatePhase::Failed:
        case UpdatePhase::Cancelled:
            return false;
    }
    return false;
}

bool hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

}  // namespace

bool update_terminal(UpdatePhase phase) {
    return phase == UpdatePhase::Successful || phase == UpdatePhase::Failed ||
           phase == UpdatePhase::Cancelled;
}

UpdatePhase update_after_restart(UpdatePhase saved) {
    switch (saved) {
        case UpdatePhase::Queued:
        case UpdatePhase::Waiting:
        case UpdatePhase::Connecting:
        case UpdatePhase::Uploading:
            return UpdatePhase::NeedsRelease;
        case UpdatePhase::Rebooting:
        case UpdatePhase::Checking:
            return UpdatePhase::Checking;
        case UpdatePhase::Successful:
        case UpdatePhase::Failed:
        case UpdatePhase::Cancelled:
        case UpdatePhase::NeedsRelease:
        case UpdatePhase::NeedsAction:
            return saved;
    }
    return UpdatePhase::NeedsAction;
}

bool update_confirmed_match(const UpdateRelease &wanted,
                            const uint8_t active_hash[32], bool confirmed) {
    return confirmed && active_hash &&
           memcmp(wanted.image_hash, active_hash, sizeof(wanted.image_hash)) == 0;
}

bool update_transition(UpdateJob &job, UpdatePhase next, uint64_t now) {
    if (!timestamp_ok(job, now) || update_terminal(job.phase_) ||
        !allowed_edge(job.phase_, next))
        return false;
    if (next == UpdatePhase::Cancelled && job.trial_may_be_armed_) return false;

    // A transfer becomes invalid as soon as it is deferred or a fresh upload
    // starts. No stored offset can authorize a resumed write.
    if (next == UpdatePhase::NeedsRelease || next == UpdatePhase::Uploading)
        job.acknowledged_ = 0;
    if (next == UpdatePhase::Rebooting) job.trial_may_be_armed_ = true;

    job.phase_ = next;
    job.changed_ms_ = now;
    return true;
}

bool update_progress(UpdateJob &job, uint32_t acknowledged, uint64_t now) {
    if (!timestamp_ok(job, now) || job.phase_ != UpdatePhase::Uploading ||
        acknowledged > job.release_.size)
        return false;
    job.acknowledged_ = acknowledged;
    job.changed_ms_ = now;
    return true;
}

bool update_attempt(UpdateJob &job, uint64_t now) {
    if (!timestamp_ok(job, now) || job.attempts_ >= 3 ||
        (job.phase_ != UpdatePhase::Connecting && job.phase_ != UpdatePhase::Checking))
        return false;
    ++job.attempts_;
    job.changed_ms_ = now;
    return true;
}

bool update_retry(UpdateJob &job, uint64_t now) {
    if (!timestamp_ok(job, now) ||
        (job.phase_ != UpdatePhase::NeedsRelease && job.phase_ != UpdatePhase::NeedsAction))
        return false;
    job.phase_ = job.trial_may_be_armed_ ? UpdatePhase::Checking : UpdatePhase::Waiting;
    job.attempts_ = 0;
    // Inspection-only retries cannot authorize writes; retain live progress for
    // feedback. A new transfer must still ask the receiver for its offset.
    if (!job.trial_may_be_armed_) job.acknowledged_ = 0;
    job.error_[0] = '\0';
    job.changed_ms_ = now;
    return true;
}

bool update_error(UpdateJob &job, const char *error, uint64_t now) {
    if (!timestamp_ok(job, now) || update_terminal(job.phase_) || !error) return false;
    size_t length = 0;
    while (length < sizeof(job.error_) && error[length]) ++length;
    if (length == sizeof(job.error_)) return false;
    std::memset(job.error_, 0, sizeof(job.error_));
    std::memcpy(job.error_, error, length);
    job.changed_ms_ = now;
    return true;
}

bool update_cancel(UpdateJob &job, uint64_t now) {
    if (!timestamp_ok(job, now) || update_terminal(job.phase_)) return false;
    if (!job.trial_may_be_armed_)
        return update_transition(job, UpdatePhase::Cancelled, now);
    if (job.phase_ == UpdatePhase::Checking) return true;
    return update_transition(job, UpdatePhase::Checking, now);
}

bool update_complete(UpdateJob &job, const uint8_t active_hash[32],
                     bool confirmed, uint64_t now) {
    if (!timestamp_ok(job, now) || job.phase_ != UpdatePhase::Checking ||
        !update_confirmed_match(job.release_, active_hash, confirmed))
        return false;
    job.phase_ = UpdatePhase::Successful;
    job.error_[0] = '\0';
    job.changed_ms_ = now;
    return true;
}

UpdateQueue::UpdateQueue(uint32_t first_id)
    : jobs_{}, size_(0), next_id_(first_id), id_exhausted_(first_id == 0) {}

bool UpdateQueue::valid_target(const UpdateTarget &target) {
    if (target.tag == 0 || target.tag == UINT16_MAX ||
        target.hardware_id[16] != '\0')
        return false;
    for (size_t i = 0; i < 16; ++i)
        if (!hex_digit(target.hardware_id[i])) return false;
    return true;
}

bool UpdateQueue::valid_release(const UpdateRelease &release) {
    return release.size >= 32 && release.size <= 212992 && release.version[0] &&
           release.version[23] == '\0';
}

bool UpdateQueue::same_target(const UpdateTarget &a, const UpdateTarget &b) {
    return a.tag == b.tag && memcmp(a.hardware_id, b.hardware_id,
                                    sizeof(a.hardware_id)) == 0;
}

UpdateJob *UpdateQueue::enqueue(const UpdateTarget &target,
                                 const UpdateRelease &release, uint64_t now) {
    if (!valid_target(target) || !valid_release(release) || id_exhausted_)
        return nullptr;

    for (size_t i = 0; i < size_; ++i)
        if (!update_terminal(jobs_[i].phase_) &&
            same_target(jobs_[i].target_, target))
            return nullptr;

    size_t slot = size_;
    if (size_ == UPDATE_QUEUE_MAX) {
        bool found = false;
        uint64_t oldest = 0;
        for (size_t i = 0; i < size_; ++i) {
            if (!update_terminal(jobs_[i].phase_)) continue;
            if (!found || jobs_[i].changed_ms_ < oldest) {
                found = true;
                oldest = jobs_[i].changed_ms_;
                slot = i;
            }
        }
        if (!found) return nullptr;
    } else {
        ++size_;
    }

    UpdateJob job;
    job.id_ = next_id_;
    job.target_ = target;
    job.release_ = release;
    job.phase_ = UpdatePhase::Queued;
    job.changed_ms_ = now;
    jobs_[slot] = job;

    if (next_id_ == UINT32_MAX)
        id_exhausted_ = true;
    else
        ++next_id_;
    return &jobs_[slot];
}

UpdateJob *UpdateQueue::find(uint32_t id) {
    for (size_t i = 0; i < size_; ++i)
        if (jobs_[i].id_ == id) return &jobs_[i];
    return nullptr;
}

const UpdateJob *UpdateQueue::find(uint32_t id) const {
    for (size_t i = 0; i < size_; ++i)
        if (jobs_[i].id_ == id) return &jobs_[i];
    return nullptr;
}

UpdateJobState update_export_state(const UpdateJob &job) {
    UpdateJobState state = {};
    state.id = job.id();
    state.target = job.target();
    state.release = job.release();
    state.phase = job.phase();
    state.acknowledged = job.acknowledged();
    state.attempts = job.attempts();
    memcpy(state.error, job.error(), sizeof(state.error));
    state.trial_may_be_armed = job.trial_may_be_armed();
    return state;
}

bool UpdateQueue::valid_restore(const UpdateJobState *states, size_t count) {
    if (count > UPDATE_QUEUE_MAX || (count && !states)) return false;
    for (size_t i = 0; i < count; ++i) {
        const UpdateJobState &s = states[i];
        if (!s.id || !valid_target(s.target) || !valid_release(s.release) ||
            static_cast<uint8_t>(s.phase) > static_cast<uint8_t>(UpdatePhase::NeedsAction) ||
            s.acknowledged > s.release.size || s.attempts > 3 ||
            !memchr(s.error, '\0', sizeof(s.error)))
            return false;
        if (s.phase == UpdatePhase::Rebooting && !s.trial_may_be_armed) return false;
        if (s.trial_may_be_armed && s.phase != UpdatePhase::Rebooting &&
            s.phase != UpdatePhase::Checking && s.phase != UpdatePhase::NeedsAction &&
            s.phase != UpdatePhase::Successful && s.phase != UpdatePhase::Failed)
            return false;
        for (size_t j = 0; j < i; ++j) {
            if (s.id == states[j].id) return false;
            if (!update_terminal(s.phase) && !update_terminal(states[j].phase) &&
                s.target.tag == states[j].target.tag)
                return false;
        }
    }
    return true;
}

bool UpdateQueue::restore(const UpdateJobState *states, size_t count,
                          uint64_t new_boot_now) {
    if (!valid_restore(states, count)) return false;
    uint32_t largest = 0;
    for (size_t i = 0; i < count; ++i) {
        const UpdateJobState &s = states[i];
        UpdateJob job;
        job.id_ = s.id;
        job.target_ = s.target;
        job.release_ = s.release;
        job.phase_ = update_after_restart(s.phase);
        job.acknowledged_ = update_terminal(s.phase) ? s.acknowledged : 0;
        job.attempts_ = s.attempts;
        job.changed_ms_ = new_boot_now;
        memcpy(job.error_, s.error, sizeof(job.error_));
        job.trial_may_be_armed_ = s.trial_may_be_armed;
        jobs_[i] = job;
        if (s.id > largest) largest = s.id;
    }
    size_ = count;
    id_exhausted_ = largest == UINT32_MAX;
    next_id_ = id_exhausted_ ? 0 : largest + 1;
    return true;
}
