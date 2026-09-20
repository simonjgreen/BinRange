#include "core/update_controller.h"

#include <cstdio>
#include <cstring>

namespace {
// Worker permits 15 s scanning then 15 s connecting; allow callback/queue time.
constexpr uint64_t kConnectDeadline = 35000;
constexpr uint64_t kRequestDeadline = 10000;
constexpr uint64_t kSessionDeadline = 540000;
constexpr uint64_t kCooldown = 60000;
constexpr uint64_t kConfirmationDeadline = 150000;
constexpr uint64_t kPollInterval = 2000;

void secure_erase(void *bytes, size_t size) {
    volatile uint8_t *out = static_cast<volatile uint8_t *>(bytes);
    while (size--) *out++ = 0;
}

struct ScopedPin final {
    explicit ScopedPin(uint32_t &source) : value(source) {}
    ~ScopedPin() { secure_erase(&value, sizeof(value)); }
    uint32_t &value;
};

bool same_target(const UpdateTarget &a, const UpdateTarget &b) {
    return a.tag == b.tag && std::memcmp(a.hardware_id, b.hardware_id, 17) == 0;
}
bool same_release(const UpdateRelease &a, const UpdateRelease &b) {
    return a.size == b.size && std::memcmp(a.file_sha, b.file_sha, 32) == 0 &&
           std::memcmp(a.image_hash, b.image_hash, 32) == 0 &&
           std::memcmp(a.version, b.version, sizeof(a.version)) == 0;
}
bool hardware_id_valid(const char id[17]) {
    for (size_t i = 0; i < 16; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
            return false;
    return id[16] == '\0';
}
}  // namespace

UpdateController::UpdateController(UpdateControllerStorage *storage,
                                   UpdateControllerTransport *transport)
    : storage_(storage), transport_(transport), queue_(), associations_{},
      association_count_(0), active_job_id_(0), operation_(0), session_(0), deadline_(0),
      session_started_(0), cooldown_until_(0), sequence_(0), stalled_(0),
      pending_smp_(SmpCommand::TagStatus), sent_offset_(0), sent_end_(0),
      stage_bytes_(nullptr), stage_borrowed_(false), disabled_(false),
      checkpoint_scratch_{} {}

void UpdateController::disable(const char *error) {
    disabled_ = true;
    std::snprintf(error_, sizeof(error_), "%s", error);
}

bool UpdateController::begin(uint64_t now) {
    if (begun_) return false; // A failed initialization cannot reset the latch.
    begun_ = true;
    if (!storage_ || !transport_ || !storage_->begin(queue_, now)) {
        disable("Update storage/transport unavailable");
        return false;
    }
    const UpdateSnapshot &saved = storage_->snapshot();
    if (saved.association_count > UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) {
        disable("Invalid saved associations");
        return false;
    }
    association_count_ = saved.association_count;
    std::memcpy(associations_, saved.associations, sizeof(associations_));
    for (size_t i = 0; i < queue_.size(); ++i)
        if (queue_.at(i)->phase() == UpdatePhase::Checking)
            checking_until_[i] = now + kConfirmationDeadline;
    ready_ = true;
    return true;
}

bool UpdateController::same_association(const UpdateAssociation &a,
                                        const UpdateAssociation &b) const {
    return same_target(a.target, b.target) && a.address_type == b.address_type &&
           std::memcmp(a.address, b.address, sizeof(a.address)) == 0;
}

bool UpdateController::valid_association(const UpdateAssociation &a) const {
    if (!a.target.tag || a.target.tag == UINT16_MAX || !hardware_id_valid(a.target.hardware_id)) return false;
    if (a.address_type > 1) return false;
    bool all_zero = true, all_ff = true;
    for (uint8_t byte : a.address) {
        all_zero = all_zero && byte == 0;
        all_ff = all_ff && byte == 0xff;
    }
    if (all_zero || all_ff) return false;
    // Random addresses used for identity must be static; public ones have no
    // address-bit constraint.
    return a.address_type == 0 || (a.address[0] & 0xc0) == 0xc0;
}

bool UpdateController::pairing_active() const {
    return pairing_phase_ == UpdatePairingPhase::Connecting ||
           pairing_phase_ == UpdatePairingPhase::Securing ||
           pairing_phase_ == UpdatePairingPhase::Checking ||
           pairing_phase_ == UpdatePairingPhase::Disconnecting;
}

bool UpdateController::busy() const { return active_job_id_ || motion_active_ != UPDATE_SNAPSHOT_MAX_ASSOCIATIONS || pairing_active() || await_ == Await::Disconnected; }

UpdateAssociation *UpdateController::association(uint16_t tag) {
    for (size_t i = 0; i < association_count_; ++i)
        if (associations_[i].target.tag == tag) return &associations_[i];
    return nullptr;
}
const UpdateAssociation *UpdateController::association(uint16_t tag) const {
    return const_cast<UpdateController *>(this)->association(tag);
}

size_t UpdateController::index(uint32_t id) const {
    for (size_t i = 0; i < queue_.size(); ++i)
        if (queue_.at(i)->id() == id) return i;
    return UPDATE_QUEUE_MAX;
}

bool UpdateController::checkpoint() {
    if (!storage_ || disabled_) return false;
    checkpoint_scratch_ = storage_->snapshot();
    checkpoint_scratch_.association_count = association_count_;
    std::memcpy(checkpoint_scratch_.associations, associations_, sizeof(associations_));
    checkpoint_scratch_.job_count = queue_.size();
    for (size_t i = 0; i < checkpoint_scratch_.job_count; ++i) {
        if (checkpoint_scratch_.jobs[i].id != queue_.at(i)->id())
            checkpoint_scratch_.last_wall_seconds[i] = 0;
        checkpoint_scratch_.jobs[i] = update_export_state(*queue_.at(i));
    }
    if (storage_->checkpoint(checkpoint_scratch_)) return true;
    disable("Checkpoint failed; updater disabled until restart");
    return false;
}

UpdateQueueResult UpdateController::queue(uint16_t adopted_tag, uint32_t *job_id,
                                          uint64_t now) {
    if (job_id) *job_id = 0;
    if (disabled_) return UpdateQueueResult::Error;
    // Do not reuse a terminal queue slot while it still owns a closing BLE
    // session. The disconnect acknowledgment releases that ownership first.
    if (await_ == Await::Disconnected || pairing_active()) return UpdateQueueResult::Conflict;
    if (!adopted_tag || adopted_tag == UINT16_MAX) return UpdateQueueResult::Invalid;
    const UpdateAssociation *a = association(adopted_tag);
    if (!ready_ || !a) return UpdateQueueResult::NeedsAction;
    const UpdateRelease *release = storage_->staged_release();
    if (!release) return UpdateQueueResult::Invalid;
    for (size_t i = 0; i < queue_.size(); ++i)
        if (!update_terminal(queue_.at(i)->phase()) && queue_.at(i)->target().tag == adopted_tag)
            return UpdateQueueResult::Conflict;
    UpdateJob *job = queue_.enqueue(a->target, *release, now);
    if (!job) return UpdateQueueResult::Capacity;
    const size_t slot = index(job->id());
    observed_[slot] = {};
    checking_until_[slot] = 0;
    if (!checkpoint()) return UpdateQueueResult::Error;
    if (job_id) *job_id = job->id();
    return UpdateQueueResult::Accepted;
}

UpdateQueueResult UpdateController::commission(const UpdateAssociation &a, uint32_t pin,
                                               uint64_t now) {
    ScopedPin input_pin(pin);
    if (!ready_) return UpdateQueueResult::Error;
    if (disabled_) return UpdateQueueResult::Error;
    if (busy()) return UpdateQueueResult::Conflict;
    if (!valid_association(a) || pin > 999999) return UpdateQueueResult::Invalid;
    bool association_exists = false;
    for (size_t i = 0; i < association_count_; ++i) {
        if (associations_[i].target.tag == a.target.tag ||
            std::memcmp(associations_[i].target.hardware_id, a.target.hardware_id, 17) == 0 ||
            (associations_[i].address_type == a.address_type &&
             std::memcmp(associations_[i].address, a.address, 6) == 0)) {
            if (!same_association(associations_[i], a)) return UpdateQueueResult::Conflict;
            association_exists = true;
        }
    }
    if (!association_exists && association_count_ == UPDATE_SNAPSHOT_MAX_ASSOCIATIONS)
        return UpdateQueueResult::Capacity;
    for (size_t i = 0; i < queue_.size(); ++i)
        if (!update_terminal(queue_.at(i)->phase()) && queue_.at(i)->target().tag == a.target.tag)
            return UpdateQueueResult::Conflict;
    if (session_ == UINT32_MAX || operation_ >= UINT32_MAX - 1) {
        disable("Transport IDs exhausted; restart required");
        return UpdateQueueResult::Error;
    }
    pairing_association_ = a;
    pairing_pin_ = input_pin.value;
    pairing_success_ = false;
    pairing_error_[0] = '\0';
    pairing_phase_ = UpdatePairingPhase::Connecting;
    pairing_changed_ms_ = now;
    ++session_;
    session_started_ = now;
    stalled_ = 0;
    if (!submit(UpdateTransportCommandKind::Connect, SmpCommand::TagStatus, now)) {
        pairing_fail("BLE connection could not be queued", now);
        return UpdateQueueResult::Error;
    }
    return UpdateQueueResult::Accepted;
}

bool UpdateController::transition(UpdateJob &job, UpdatePhase phase, uint64_t now) {
    return update_transition(job, phase, now) && checkpoint();
}

bool UpdateController::cancel(uint32_t job_id, uint64_t now) {
    if (!ready_ || disabled_) return false;
    UpdateJob *job = queue_.find(job_id);
    if (!job || !update_cancel(*job, now)) return false;
    if (job->trial_may_be_armed()) {
        update_error(*job, "Cancellation requested after trial; inspecting outcome", now);
        prepare_check(*job, now);
    }
    const bool saved = checkpoint();
    if (job_id == active_job_id_) close(now, job->trial_may_be_armed() ? kPollInterval : 0);
    return saved;
}

bool UpdateController::retry(uint32_t job_id, uint64_t now) {
    if (!ready_ || disabled_ || active_job_id_ == job_id) return false;
    UpdateJob *job = queue_.find(job_id);
    if (!job) return false;
    if (!job->trial_may_be_armed()) {
        const UpdateRelease *staged = storage_->staged_release();
        if (!staged || !same_release(job->release(), *staged)) return false;
    }
    if (!update_retry(*job, now)) return false;
    checking_until_[index(job_id)] = job->phase() == UpdatePhase::Checking
        ? now + kConfirmationDeadline : 0;
    return checkpoint();
}

bool UpdateController::submit(UpdateTransportCommandKind kind, SmpCommand smp,
                              uint64_t now) {
    const bool disconnect = kind == UpdateTransportCommandKind::Disconnect;
    if ((!disconnect && disabled_) || operation_ == UINT32_MAX ||
        (!disconnect && operation_ == UINT32_MAX - 1)) {
        disable("Transport operation IDs exhausted; restart required");
        return false;
    }
    UpdateJob *job = queue_.find(active_job_id_);
    const UpdateAssociation *a = job ? association(job->target().tag) :
        (pairing_active() ? &pairing_association_ :
         (motion_active_ != UPDATE_SNAPSHOT_MAX_ASSOCIATIONS ? association(motion_[motion_active_].tag) : nullptr));
    if (!a || (job && !same_target(job->target(), a->target))) return false;
    UpdateTransportCommand command = {};
    command.kind = kind;
    command.operation = ++operation_;
    command.session = session_;
    command.association = *a;
    command.smp = smp;
    if (kind == UpdateTransportCommandKind::Security && pairing_active()) {
        command.commissioning = true;
        command.passkey = pairing_pin_;
    }
    if (kind == UpdateTransportCommandKind::Exchange) {
        SmpUpload upload = {};
        const SmpUpload *payload = nullptr;
        if (smp == SmpCommand::Upload) {
            if (!stage_borrowed_) {
                if (!storage_->acquire(job->release(), stage_bytes_)) return false;
                stage_borrowed_ = true;
            }
            if (!stage_bytes_ || job->acknowledged() >= job->release().size) return false;
            sent_offset_ = job->acknowledged();
            const uint32_t left = job->release().size - sent_offset_;
            const uint32_t chunk = left > 256 ? 256 : left;
            sent_end_ = sent_offset_ + chunk;
            upload = {sent_offset_, job->release().size,
                      sent_offset_ == 0 ? job->release().file_sha : nullptr,
                      stage_bytes_ + sent_offset_, chunk};
            payload = &upload;
        }
        const uint8_t sequence = sequence_++;
        const bool encoded = smp == SmpCommand::ConfigWrite
            ? smp_encode_config(sequence, motion_target_, command.frame, sizeof(command.frame), command.frame_size)
            : smp_encode_request(smp, sequence, payload, command.frame, sizeof(command.frame), command.frame_size);
        if (!encoded) return false;
    }
    const bool submitted = transport_->submit(command);
    if (kind == UpdateTransportCommandKind::Security && pairing_active()) {
        secure_erase(&pairing_pin_, sizeof(pairing_pin_));
        secure_erase(&command.passkey, sizeof(command.passkey));
    }
    if (!submitted) return false;
    pending_smp_ = smp;
    switch (kind) {
        case UpdateTransportCommandKind::Connect: await_ = Await::Connected; break;
        case UpdateTransportCommandKind::Security: await_ = Await::Secured; break;
        case UpdateTransportCommandKind::Exchange: await_ = Await::Reply; break;
        case UpdateTransportCommandKind::Disconnect: await_ = Await::Disconnected; break;
    }
    if (!disconnect)
        deadline_ = now + (kind == UpdateTransportCommandKind::Connect ? kConnectDeadline : kRequestDeadline);
    return true;
}

void UpdateController::close(uint64_t now, uint64_t delay) {
    if ((!active_job_id_ && !pairing_active() && motion_active_ == UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) || await_ == Await::Disconnected) return;
    await_ = Await::Disconnected;
    deadline_ = now + kRequestDeadline;
    if (cooldown_until_ < now + delay) cooldown_until_ = now + delay;
    disconnect_submitted_ = submit(UpdateTransportCommandKind::Disconnect, SmpCommand::TagStatus, now);
    next_disconnect_try_ = now + 1000;
}

void UpdateController::finish_close() {
    if (stage_borrowed_) storage_->release();
    stage_borrowed_ = false;
    stage_bytes_ = nullptr;
    active_job_id_ = 0;
    if (motion_active_ != UPDATE_SNAPSHOT_MAX_ASSOCIATIONS && motion_forgetting_)
        motion_[motion_active_] = {};
    motion_forgetting_ = false;
    motion_active_ = UPDATE_SNAPSHOT_MAX_ASSOCIATIONS;
    await_ = Await::None;
    disconnect_submitted_ = false;
}

void UpdateController::pairing_finish_close(uint64_t now) {
    secure_erase(&pairing_pin_, sizeof(pairing_pin_));
    pairing_phase_ = pairing_success_ ? UpdatePairingPhase::Successful : UpdatePairingPhase::Failed;
    pairing_changed_ms_ = now;
    pairing_success_ = false;
    await_ = Await::None;
    disconnect_submitted_ = false;
}

void UpdateController::pairing_fail(const char *error, uint64_t now) {
    secure_erase(&pairing_pin_, sizeof(pairing_pin_));
    pairing_success_ = false;
    std::snprintf(pairing_error_, sizeof(pairing_error_), "%s", error);
    pairing_phase_ = UpdatePairingPhase::Disconnecting;
    pairing_changed_ms_ = now;
    close(now);
}

void UpdateController::stop(UpdatePhase phase, const char *error, uint64_t now, uint64_t delay) {
    UpdateJob *job = queue_.find(active_job_id_);
    if (job && !update_terminal(job->phase()) && !disabled_) {
        update_error(*job, error, now);
        if (job->phase() != phase && !update_transition(*job, phase, now))
            disable("Invalid update transition; restart required");
        checkpoint();
    }
    close(now, delay);
}

void UpdateController::failed(const char *error, uint64_t now, bool retryable) {
    UpdateJob *job = queue_.find(active_job_id_);
    if (!job) return;
    UpdatePhase next = UpdatePhase::NeedsAction;
    if (retryable && job->attempts() < 3) {
        next = (job->trial_may_be_armed() || job->phase() == UpdatePhase::Checking)
            ? UpdatePhase::Checking : UpdatePhase::Waiting;
    }
    if (next == UpdatePhase::Checking) prepare_check(*job, now);
    stop(next, error, now, kCooldown);
}

void UpdateController::start(UpdateJob &job, uint64_t now) {
    const UpdateAssociation *a = association(job.target().tag);
    if (!a || !same_target(job.target(), a->target) || session_ == UINT32_MAX) {
        disable("Association missing or session IDs exhausted");
        return;
    }
    if (job.phase() != UpdatePhase::Checking &&
        !update_transition(job, UpdatePhase::Connecting, now)) return;
    if (!update_attempt(job, now)) {
        update_error(job, "Connection attempts exhausted; operator retry required", now);
        transition(job, UpdatePhase::NeedsAction, now);
        return;
    }
    if (!checkpoint()) return; // Durable attempt/phase before any BLE operation.
    active_job_id_ = job.id();
    ++session_;
    session_started_ = now;
    stalled_ = 0;
    if (!submit(UpdateTransportCommandKind::Connect, SmpCommand::TagStatus, now))
        failed("BLE connection could not be queued", now, true);
}

bool UpdateController::active_image(const SmpReply &reply, SmpImage &out) const {
    if (!reply.image_count || reply.image_count > 2) return false;
    unsigned found = 0;
    for (size_t i = 0; i < reply.image_count; ++i)
        if (reply.images[i].active) { out = reply.images[i]; ++found; }
    return found == 1 && out.bootable;
}
bool UpdateController::pending_image(const SmpReply &reply) const {
    for (size_t i = 0; i < reply.image_count && i < 2; ++i)
        if (reply.images[i].pending) return true;
    return false;
}

bool UpdateController::prepare_check(UpdateJob &job, uint64_t now) {
    const size_t slot = index(job.id());
    if (slot == UPDATE_QUEUE_MAX) return false;
    if (!checking_until_[slot]) checking_until_[slot] = now + kConfirmationDeadline;
    return job.phase() == UpdatePhase::Checking || transition(job, UpdatePhase::Checking, now);
}

void UpdateController::inspect_or_upload(const SmpReply &reply, uint64_t now) {
    UpdateJob *job = queue_.find(active_job_id_);
    if (!job) return;
    SmpImage active = {};
    if (!active_image(reply, active)) { failed("Invalid active image status", now, false); return; }
    auto &observed = observed_[index(job->id())];
    observed.image_valid = true;
    observed.active = active;
    observed.seen_ms = now;
    if (job->phase() == UpdatePhase::Uploading) {
        bool inactive_match = false;
        for (size_t i = 0; i < reply.image_count; ++i)
            inactive_match = inactive_match || (!reply.images[i].active && reply.images[i].bootable &&
                std::memcmp(reply.images[i].hash, job->release().image_hash, 32) == 0);
        if (!inactive_match || pending_image(reply)) {
            failed("Inactive image hash/state does not match release", now, false); return;
        }
        if (!transition(*job, UpdatePhase::Rebooting, now) ||
            !submit(UpdateTransportCommandKind::Exchange, SmpCommand::Trial, now))
            failed("Could not request trial; inspect outcome", now, false);
        return;
    }
    if (std::memcmp(active.hash, job->release().image_hash, 32) == 0) {
        if (!prepare_check(*job, now)) { close(now); return; }
        if (active.confirmed) {
            if (update_complete(*job, active.hash, true, now)) checkpoint();
            close(now);
        } else {
            // Pending=false on the wanted running image is not rollback.
            await_ = Await::Poll;
            deadline_ = now + kPollInterval;
        }
        return;
    }
    if (pending_image(reply)) {
        failed("Old image has a pending trial; inspect locally before retry", now, false); return;
    }
    if (job->phase() == UpdatePhase::Checking || job->trial_may_be_armed()) {
        stop(job->trial_may_be_armed() ? UpdatePhase::Failed : UpdatePhase::NeedsAction,
             "Requested image is not active; trial did not remain installed", now);
        return;
    }
    if (!active.confirmed) {
        failed("Another unconfirmed image is running", now, false); return;
    }
    if (!transition(*job, UpdatePhase::Uploading, now) ||
        !submit(UpdateTransportCommandKind::Exchange, SmpCommand::Upload, now))
        failed("Release could not be acquired or upload queued", now, false);
}

void UpdateController::handle(const UpdateTransportEvent &event, uint64_t now) {
    if ((!active_job_id_ && !pairing_active() && motion_active_ == UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) || event.operation != operation_ || event.session != session_) return;
    if (await_ == Await::Disconnected) {
        if (disconnect_submitted_ && event.kind == UpdateTransportEventKind::Disconnected) {
            if (pairing_active()) pairing_finish_close(now); else finish_close();
        }
        return;
    }
    if (disabled_) return;
    if (motion_active_ != UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) { motion_handle(event, now); return; }
    if (pairing_active()) {
        if (event.kind == UpdateTransportEventKind::Failed || event.kind == UpdateTransportEventKind::Disconnected) {
            pairing_fail(event.failure == UpdateTransportFailure::Transient ?
                "BLE disconnected or operation failed" : "BLE security/identity/protocol failure", now);
        } else if (await_ == Await::Connected && event.kind == UpdateTransportEventKind::Connected) {
            pairing_phase_ = UpdatePairingPhase::Securing; pairing_changed_ms_ = now;
            if (!submit(UpdateTransportCommandKind::Security, SmpCommand::TagStatus, now))
                pairing_fail("Could not queue link security", now);
        } else if (await_ == Await::Secured && event.kind == UpdateTransportEventKind::Secured) {
            if (!event.bonded_mitm_sc) pairing_fail("Authenticated Secure Connections bond required", now);
            else {
                pairing_phase_ = UpdatePairingPhase::Checking; pairing_changed_ms_ = now;
                if (!submit(UpdateTransportCommandKind::Exchange, SmpCommand::TagStatus, now))
                    pairing_fail("Could not queue tag identity request", now);
            }
        } else if (await_ == Await::Reply && event.kind == UpdateTransportEventKind::Reply) {
            if (event.reply.outcome != SmpOutcome::Ok || event.reply.tag.tag != pairing_association_.target.tag ||
                std::memcmp(event.reply.tag.id, pairing_association_.target.hardware_id, 17) != 0) {
                pairing_fail("Commissioned tag identity mismatch", now);
            } else {
                size_t existing = association_count_;
                for (size_t i = 0; i < association_count_; ++i)
                    if (same_association(associations_[i], pairing_association_)) { existing = i; break; }
                bool added = false;
                if (existing == association_count_) {
                    if (association_count_ == UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) {
                        pairing_fail("Association capacity exhausted", now); return;
                    }
                    associations_[association_count_++] = pairing_association_;
                    added = true;
                }
                if (!checkpoint()) {
                    if (added) --association_count_;
                    pairing_fail("Association checkpoint failed", now);
                } else {
                    pairing_success_ = true;
                    pairing_phase_ = UpdatePairingPhase::Disconnecting;
                    pairing_changed_ms_ = now;
                    close(now);
                }
            }
        }
        return;
    }
    UpdateJob *job = queue_.find(active_job_id_);
    if (!job) return;
    if (event.kind == UpdateTransportEventKind::Failed || event.kind == UpdateTransportEventKind::Disconnected) {
        const bool transient = event.kind == UpdateTransportEventKind::Disconnected ||
                               event.failure == UpdateTransportFailure::Transient;
        failed(transient ? "BLE disconnected or operation failed" : "BLE security/identity/protocol failure",
               now, transient);
        return;
    }
    if (await_ == Await::Connected && event.kind == UpdateTransportEventKind::Connected) {
        if (!submit(UpdateTransportCommandKind::Security, SmpCommand::TagStatus, now))
            failed("Could not queue link security", now, false);
        return;
    }
    if (await_ == Await::Secured && event.kind == UpdateTransportEventKind::Secured) {
        if (!event.bonded_mitm_sc) failed("Authenticated Secure Connections bond required", now, false);
        else if (!submit(UpdateTransportCommandKind::Exchange, SmpCommand::TagStatus, now))
            failed("Could not queue tag identity request", now, true);
        return;
    }
    if (await_ != Await::Reply || event.kind != UpdateTransportEventKind::Reply) return;
    if (event.reply.outcome != SmpOutcome::Ok) {
        char error[80];
        std::snprintf(error, sizeof(error), "Tag denied command (group %u, code %lu)",
                      event.reply.error_group, static_cast<unsigned long>(event.reply.error_code));
        failed(error, now, false); return;
    }
    if (pending_smp_ == SmpCommand::TagStatus) {
        if (std::memcmp(event.reply.tag.id, job->target().hardware_id, 17) ||
            event.reply.tag.tag != job->target().tag) { failed("Commissioned tag identity mismatch", now, false); return; }
        auto &observed = observed_[index(job->id())];
        observed.identity_valid = true;
        observed.tag = event.reply.tag;
        observed.seen_ms = now;
        if (!submit(UpdateTransportCommandKind::Exchange, SmpCommand::ImageList, now))
            failed("Could not queue image inspection", now, true);
    } else if (pending_smp_ == SmpCommand::ImageList) {
        inspect_or_upload(event.reply, now);
    } else if (pending_smp_ == SmpCommand::Upload) {
        if (!smp_accept_upload_offset(event.reply.offset, sent_offset_, sent_end_, job->release().size,
                                      sent_offset_ == 0, stalled_) || !update_progress(*job, event.reply.offset, now)) {
            failed("Invalid or stalled receiver upload offset", now, false); return;
        }
        const SmpCommand next = event.reply.offset < job->release().size ? SmpCommand::Upload : SmpCommand::ImageList;
        if (!submit(UpdateTransportCommandKind::Exchange, next, now)) failed("Could not continue upload", now, true);
    } else if (pending_smp_ == SmpCommand::Trial) {
        // Persist uncertainty BEFORE reset. Restart only inspects; it never
        // blindly replays TEST or RESET against an unknown installation state.
        if (!prepare_check(*job, now) ||
            !submit(UpdateTransportCommandKind::Exchange, SmpCommand::Reset, now))
            failed("Could not request reset; inspect outcome", now, false);
    } else if (pending_smp_ == SmpCommand::Reset) {
        close(now, kPollInterval);
    }
}

void UpdateController::loop(uint64_t now) {
    if (!ready_) return;
    // Overdue callbacks cannot authorize another write. Even when disabled,
    // process quiescence ACKs, but keep borrowed memory until one actually arrives.
    if ((active_job_id_ || pairing_active() || motion_active_ != UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) && await_ != Await::Disconnected) {
        const size_t slot = index(active_job_id_);
        UpdateJob *job = queue_.find(active_job_id_);
        if (disabled_) close(now);
        else if (motion_active_ != UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) {
            if (now - session_started_ >= kSessionDeadline || (motion_apply_until_ && now >= motion_apply_until_))
                motion_fail("Tag settings application timed out; read settings or retry", now);
            else if (await_ == Await::Poll && now >= deadline_) motion_send(SmpCommand::TagStatus, now);
            else if (await_ != Await::Poll && now >= deadline_)
                motion_fail("Bluetooth operation timed out; read settings or retry", now);
        }
        else if (pairing_active() && now - session_started_ >= kSessionDeadline)
            pairing_fail("BLE session deadline expired", now);
        else if (pairing_active() && now >= deadline_)
            pairing_fail("BLE operation deadline expired", now);
        else if (job && job->phase() == UpdatePhase::Checking && checking_until_[slot] && now >= checking_until_[slot])
            failed("Local confirmation was not observed before deadline", now, false);
        else if (now - session_started_ >= kSessionDeadline)
            failed("BLE session deadline expired", now, true);
        else if (await_ == Await::Poll && now >= deadline_) {
            if (!submit(UpdateTransportCommandKind::Exchange, SmpCommand::ImageList, now))
                failed("Could not poll confirmation", now, true);
        } else if (await_ != Await::Poll && now >= deadline_)
            failed("BLE operation deadline expired", now, true);
    }
    UpdateTransportEvent event = {};
    for (unsigned i = 0; i < 8 && transport_->poll(event); ++i) handle(event, now);
    if ((active_job_id_ || pairing_active() || motion_active_ != UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) && await_ == Await::Disconnected) {
        if (now >= deadline_) {
            disable("BLE did not acknowledge disconnect; restart required");
        } else if (!disconnect_submitted_ && now >= next_disconnect_try_) {
            disconnect_submitted_ = submit(UpdateTransportCommandKind::Disconnect, SmpCommand::TagStatus, now);
            next_disconnect_try_ = now + 1000;
        }
    }
    if (disabled_ || busy()) return;
    for (size_t i = 0; i < queue_.size(); ++i) {
        UpdateJob *job = queue_.at(i);
        if (job->phase() == UpdatePhase::Checking && checking_until_[i] && now >= checking_until_[i]) {
            update_error(*job, "Local confirmation was not observed before deadline", now);
            if (!transition(*job, UpdatePhase::NeedsAction, now)) return;
        }
        if (now < cooldown_until_) continue;
        if (job->phase() == UpdatePhase::Queued && !transition(*job, UpdatePhase::Waiting, now)) return;
        if (job->phase() == UpdatePhase::Waiting) {
            const UpdateRelease *staged = storage_->staged_release();
            if (!staged || !same_release(job->release(), *staged)) {
                update_error(*job, "Upload the exact release again before retrying", now);
                if (!transition(*job, UpdatePhase::NeedsRelease, now)) return;
                continue;
            }
        }
        if (job->phase() == UpdatePhase::Waiting || job->phase() == UpdatePhase::Checking) { start(*job, now); return; }
    }
    if (now >= cooldown_until_) for (size_t i = 0; i < UPDATE_SNAPSHOT_MAX_ASSOCIATIONS; ++i)
        if (motion_[i].queued) { motion_start(i, now); return; }
}

bool UpdateController::snapshot(UpdateControllerSnapshot &out) const {
    out = {};
    out.association_count = association_count_;
    out.job_count = queue_.size();
    std::memcpy(out.associations, associations_, sizeof(associations_));
    for (size_t i = 0; i < out.job_count; ++i) {
        out.jobs[i] = update_export_state(*queue_.at(i));
        out.changed_ms[i] = queue_.at(i)->changed_ms();
        out.observed[i] = observed_[i];
    }
    out.active_job_id = active_job_id_;
    out.staging_capacity = storage_ ? storage_->capacity() : 0;
    out.staging_available = storage_ && storage_->staged_release();
    out.disabled = disabled_;
    out.disconnecting = await_ == Await::Disconnected;
    std::memcpy(out.motion, motion_, sizeof(motion_));
    std::memcpy(out.error, error_, sizeof(error_));
    out.pairing.phase = pairing_phase_;
    out.pairing.association = pairing_association_;
    std::memcpy(out.pairing.error, pairing_error_, sizeof(pairing_error_));
    out.pairing.changed_ms = pairing_changed_ms_;
    return ready_;
}
