#include "core/update_controller.h"
#include <cstdio>
#include <cstring>

UpdateQueueResult UpdateController::motion_request(uint16_t tag, uint32_t idle,
                                                   uint32_t moving, uint64_t now) {
    if (!ready_ || disabled_) return UpdateQueueResult::Error;
    if (!tag || tag == UINT16_MAX || (idle && (idle < 60000 || idle > 3600000)) ||
        (moving && (moving < 1000 || moving > 60000))) return UpdateQueueResult::Invalid;
    if (!association(tag)) return UpdateQueueResult::NeedsAction;
    size_t slot = UPDATE_SNAPSHOT_MAX_ASSOCIATIONS;
    for (size_t i = 0; i < UPDATE_SNAPSHOT_MAX_ASSOCIATIONS; ++i) {
        if (motion_[i].tag == tag) { slot = i; break; }
        if (!motion_[i].tag && i != motion_active_ && slot == UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) slot = i;
    }
    if (slot == UPDATE_SNAPSHOT_MAX_ASSOCIATIONS) return UpdateQueueResult::Capacity;
    if (motion_active_ == slot && motion_forgetting_) return UpdateQueueResult::Conflict;
    auto &m = motion_[slot];
    // Read refreshes must not cancel a pending write or schedule duplicate work.
    if (!idle && !moving && (m.queued || motion_active_ == slot)) return UpdateQueueResult::Accepted;
    m.tag = tag;
    if (idle) m.idle_ms = idle;
    if (moving) m.moving_ms = moving;
    m.queued = true;
    if (motion_active_ != slot) m.phase = MotionPhase::Queued;
    m.changed_ms = now;
    m.error[0] = 0;
    return UpdateQueueResult::Accepted;
}

void UpdateController::motion_forget(uint16_t tag, uint64_t now) {
    for (size_t i = 0; i < UPDATE_SNAPSHOT_MAX_ASSOCIATIONS; ++i) if (motion_[i].tag == tag) {
        if (i == motion_active_) {
            motion_fail("Tag no longer adopted", now);
            // Keep its identity until the worker acknowledges disconnect.
            motion_[i].queued = false;
            motion_forgetting_ = true;
        } else motion_[i] = {};
    }
}

void UpdateController::motion_start(size_t slot, uint64_t now) {
    auto &m = motion_[slot];
    if (!association(m.tag) || session_ == UINT32_MAX) {
        m.queued = false; m.phase = MotionPhase::Failed; m.changed_ms = now;
        std::snprintf(m.error, sizeof(m.error), "Association missing or session IDs exhausted");
        return;
    }
    motion_active_ = slot;
    motion_forgetting_ = false;
    motion_idle_ = m.idle_ms; motion_moving_ = m.moving_ms;
    m.idle_ms = m.moving_ms = 0; m.queued = false;
    m.phase = MotionPhase::Reading; m.changed_ms = now; m.error[0] = 0;
    motion_verifying_ = false; motion_apply_until_ = 0; motion_target_ = {};
    ++session_; session_started_ = now;
    if (!submit(UpdateTransportCommandKind::Connect, SmpCommand::TagStatus, now))
        motion_fail("Bluetooth connection could not be queued", now);
}

void UpdateController::motion_fail(const char *error, uint64_t now) {
    auto &m = motion_[motion_active_];
    m.phase = MotionPhase::Failed; m.changed_ms = now;
    // After an attempted write, the previous value is no longer authoritative.
    if (motion_verifying_ || pending_smp_ == SmpCommand::ConfigWrite) m.known = false;
    std::snprintf(m.error, sizeof(m.error), "%s", error);
    // Never replay a timed-out write. A new request starts with a fresh read.
    close(now, 60000);
}

bool UpdateController::motion_send(SmpCommand command, uint64_t now) {
    if (submit(UpdateTransportCommandKind::Exchange, command, now)) return true;
    motion_fail("Bluetooth request could not be queued", now);
    return false;
}

void UpdateController::motion_handle(const UpdateTransportEvent &event, uint64_t now) {
    auto &m = motion_[motion_active_];
    if (event.kind == UpdateTransportEventKind::Failed || event.kind == UpdateTransportEventKind::Disconnected) {
        motion_fail(event.failure == UpdateTransportFailure::Transient
            ? "Tag unreachable or disconnected; read settings or retry"
            : "Bluetooth security/identity/protocol failure", now);
        return;
    }
    if (await_ == Await::Connected && event.kind == UpdateTransportEventKind::Connected) {
        if (!submit(UpdateTransportCommandKind::Security, SmpCommand::TagStatus, now))
            motion_fail("Could not queue authenticated link security", now);
        return;
    }
    if (await_ == Await::Secured && event.kind == UpdateTransportEventKind::Secured) {
        if (!event.bonded_mitm_sc) motion_fail("Authenticated Secure Connections bond required", now);
        else motion_send(SmpCommand::TagStatus, now);
        return;
    }
    if (await_ != Await::Reply || event.kind != UpdateTransportEventKind::Reply) return;
    if (event.reply.outcome != SmpOutcome::Ok) {
        char error[80];
        std::snprintf(error, sizeof(error), "Tag denied settings command (group %u, code %lu)",
            event.reply.error_group, static_cast<unsigned long>(event.reply.error_code));
        motion_fail(error, now); return;
    }
    if (pending_smp_ == SmpCommand::TagStatus) {
        const auto *a = association(m.tag);
        const auto &tag = event.reply.tag;
        if (!a || tag.tag != a->target.tag || std::memcmp(tag.id, a->target.hardware_id, 17)) {
            motion_fail("Commissioned tag identity mismatch", now); return;
        }
        if (!tag.confirmed || !tag.config_status_known) {
            motion_fail("Confirmed tag firmware 0.2.6+ with settings schema 1 required", now); return;
        }
        if (!motion_verifying_) { motion_send(SmpCommand::ConfigRead, now); return; }
        if (tag.config_pending) {
            await_ = Await::Poll; deadline_ = now + 2000;
            return;
        }
        if (tag.sensor_error) { motion_fail("Tag saved settings but motion sensor application failed", now); return; }
        m.config = motion_target_; m.known = true; m.seen_ms = now; m.changed_ms = now;
        m.phase = m.queued ? MotionPhase::Queued : MotionPhase::Applied;
        close(now);
        return;
    }
    if (pending_smp_ == SmpCommand::ConfigRead) {
        if (!smp_motion_valid(event.reply.config)) { motion_fail("Invalid tag motion configuration", now); return; }
        if (motion_verifying_) {
            if (!smp_motion_equal(event.reply.config, motion_target_)) {
                motion_fail("Tag settings readback did not match request", now); return;
            }
            motion_send(SmpCommand::TagStatus, now); return;
        }
        motion_target_ = event.reply.config;
        if (motion_idle_) motion_target_.idle_ms = motion_idle_;
        if (motion_moving_) motion_target_.moving_ms = motion_moving_;
        if (!smp_motion_valid(motion_target_)) { motion_fail("Requested intervals are invalid", now); return; }
        motion_apply_until_ = now + 15000;
        motion_verifying_ = true;
        m.changed_ms = now;
        if (smp_motion_equal(motion_target_, event.reply.config)) {
            m.phase = MotionPhase::Verifying;
            motion_send(SmpCommand::TagStatus, now);
        } else {
            m.phase = MotionPhase::Writing;
            m.known = false;
            motion_send(SmpCommand::ConfigWrite, now);
        }
    } else if (pending_smp_ == SmpCommand::ConfigWrite) {
        m.phase = MotionPhase::Verifying; m.changed_ms = now;
        motion_send(SmpCommand::ConfigRead, now);
    }
}
