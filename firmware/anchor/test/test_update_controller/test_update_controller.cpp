#include <unity.h>

#include <cstring>

#include "core/update_controller.h"

// Break caught: accepting a queued update without an association would let an
// ordinary update silently initiate pairing rather than stopping for operator
// action.
void test_queue_requires_a_commissioned_target() {
    UpdateController controller(nullptr, nullptr);
    uint32_t job_id = 0;
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::NeedsAction,
                          (int)controller.queue(0xb100, &job_id, 1));
    TEST_ASSERT_EQUAL_UINT32(0, job_id);
}

namespace {
struct FakeStorage final : UpdateControllerStorage {
    UpdateSnapshot saved{};
    UpdateRelease staged{};
    uint8_t image[1024]{};
    bool has_stage = true;
    bool checkpoint_ok = true;
    bool borrowed = false;
    unsigned checkpoints = 0;
    bool begin(UpdateQueue &queue, uint64_t now) override {
        if (!queue.restore(saved.jobs, saved.job_count, now)) return false;
        ++saved.restart_count;
        return true;
    }
    bool checkpoint(const UpdateSnapshot &next) override {
        ++checkpoints;
        if (!checkpoint_ok) return false;
        saved = next;
        return true;
    }
    const UpdateSnapshot &snapshot() const override { return saved; }
    const UpdateRelease *staged_release() const override { return has_stage ? &staged : nullptr; }
    bool acquire(const UpdateRelease &release, const uint8_t *&bytes) override {
        if (borrowed || std::memcmp(&release, &staged, sizeof(release))) return false;
        borrowed = true; bytes = image; return true;
    }
    void release() override { borrowed = false; }
    size_t capacity() const override { return sizeof(image); }
};

struct FakeTransport final : UpdateControllerTransport {
    UpdateTransportCommand commands[128]{};
    size_t command_count = 0;
    UpdateTransportEvent events[128]{};
    size_t event_count = 0, event_pos = 0;
    bool submit(const UpdateTransportCommand &command) override {
        if (command_count == 128) return false;
        commands[command_count++] = command;
        return true;
    }
    bool poll(UpdateTransportEvent &event) override {
        if (event_pos == event_count) return false;
        event = events[event_pos++]; return true;
    }
    void reply_for_last(UpdateTransportEventKind kind, const SmpReply &reply = {}) {
        const UpdateTransportCommand &command = commands[command_count - 1];
        events[event_count++] = {kind, command.operation, command.session, true, reply};
    }
};

UpdateRelease release() {
    UpdateRelease out{};
    out.size = 64;
    std::memset(out.file_sha, 0x11, sizeof(out.file_sha));
    std::memset(out.image_hash, 0x22, sizeof(out.image_hash));
    std::strcpy(out.version, "0.2.2");
    return out;
}
void commissioned(FakeStorage &storage) {
    storage.staged = release();
    storage.saved.association_count = 1;
    storage.saved.associations[0] = {{0xb100, "f00dbaad12345678"}, {1,2,3,4,5,6}, 0};
}
SmpReply status() {
    SmpReply out{}; out.outcome = SmpOutcome::Ok; out.tag.tag = 0xb100;
    std::strcpy(out.tag.id, "f00dbaad12345678"); return out;
}
SmpReply slots(uint8_t active, bool confirmed, bool pending = false) {
    SmpReply out{}; out.outcome = SmpOutcome::Ok; out.image_count = 2;
    out.images[0].active = true; out.images[0].confirmed = confirmed;
    out.images[0].bootable = true;
    std::memset(out.images[0].hash, active, 32);
    out.images[1].pending = pending;
    out.images[1].slot = 1;
    out.images[1].bootable = true;
    std::memset(out.images[1].hash, 0x22, 32);
    return out;
}

void connect_and_inspect(UpdateController &controller, FakeTransport &transport,
                         uint64_t now, const SmpReply &images) {
    controller.loop(now);
    transport.reply_for_last(UpdateTransportEventKind::Connected);
    controller.loop(now + 1);
    transport.reply_for_last(UpdateTransportEventKind::Secured);
    controller.loop(now + 2);
    transport.reply_for_last(UpdateTransportEventKind::Reply, status());
    controller.loop(now + 3);
    transport.reply_for_last(UpdateTransportEventKind::Reply, images);
    controller.loop(now + 4);
}

void upload_to_trial(UpdateController &controller, FakeTransport &transport) {
    connect_and_inspect(controller, transport, 3, slots(0x10, true));
    SmpReply offset{}; offset.outcome = SmpOutcome::Ok; offset.offset = 64;
    transport.reply_for_last(UpdateTransportEventKind::Reply, offset);
    controller.loop(8);
    transport.reply_for_last(UpdateTransportEventKind::Reply, slots(0x10, true));
    controller.loop(9);
}
}  // namespace

// Break caught: issuing TEST before a durable Rebooting checkpoint (or without
// the verified inactive image hash) makes a power loss look like a safe update.
void test_upload_verifies_inactive_hash_then_checkpoints_before_test() {
    FakeStorage storage; commissioned(storage);
    FakeTransport transport;
    UpdateController controller(&storage, &transport);
    TEST_ASSERT_TRUE(controller.begin(1));
    uint32_t id = 0;
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted, (int)controller.queue(0xb100, &id, 2));
    controller.loop(3);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Connect, (int)transport.commands[0].kind);
    transport.reply_for_last(UpdateTransportEventKind::Connected);
    controller.loop(4);
    transport.reply_for_last(UpdateTransportEventKind::Secured);
    controller.loop(5);
    transport.reply_for_last(UpdateTransportEventKind::Reply, status());
    controller.loop(6);
    transport.reply_for_last(UpdateTransportEventKind::Reply, slots(0x10, true));
    controller.loop(7);
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::Upload, (int)transport.commands[4].smp);
    SmpReply offset{}; offset.outcome = SmpOutcome::Ok; offset.offset = 64;
    transport.reply_for_last(UpdateTransportEventKind::Reply, offset);
    controller.loop(8);
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::ImageList, (int)transport.commands[5].smp);
    transport.reply_for_last(UpdateTransportEventKind::Reply, slots(0x10, true));
    controller.loop(9);
    UpdateControllerSnapshot snap{};
    controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Rebooting, (int)snap.jobs[0].phase);
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::Trial, (int)transport.commands[6].smp);
    TEST_ASSERT_TRUE(storage.checkpoints >= 4);
}

// Break caught: allowing an ordinary update to proceed after insecure or
// unbonded link establishment would silently downgrade the commissioned bond.
void test_insecure_link_becomes_needs_action_without_an_smp_request() {
    FakeStorage storage; commissioned(storage);
    FakeTransport transport;
    UpdateController controller(&storage, &transport);
    TEST_ASSERT_TRUE(controller.begin(1));
    uint32_t id = 0;
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted, (int)controller.queue(0xb100, &id, 2));
    controller.loop(3);
    transport.reply_for_last(UpdateTransportEventKind::Connected);
    controller.loop(4);
    UpdateTransportEvent insecure{};
    const UpdateTransportCommand &security = transport.commands[1];
    insecure.kind = UpdateTransportEventKind::Secured;
    insecure.operation = security.operation;
    insecure.session = security.session;
    insecure.bonded_mitm_sc = false;
    transport.events[transport.event_count++] = insecure;
    controller.loop(5);
    UpdateControllerSnapshot snap{};
    controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsAction, (int)snap.jobs[0].phase);
    TEST_ASSERT_EQUAL_UINT(3, transport.command_count); // connect, security, disconnect
}

// Break caught: a failed durable enqueue checkpoint used to leave a live job
// behind; a later loop could connect and update despite reporting queue error.
void test_checkpoint_failure_latches_controller_before_any_transport_command() {
    FakeStorage storage; commissioned(storage); storage.checkpoint_ok = false;
    FakeTransport transport;
    UpdateController controller(&storage, &transport);
    TEST_ASSERT_TRUE(controller.begin(1));
    uint32_t id = 99;
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Error, (int)controller.queue(0xb100, &id, 2));
    TEST_ASSERT_EQUAL_UINT32(0, id);
    storage.checkpoint_ok = true;  // Storage recovery must not revive unsafe work.
    controller.loop(3);
    TEST_ASSERT_EQUAL_UINT(0, transport.command_count);
}

// A successful transfer is not a successful update. Observe an unconfirmed
// wanted image first, then require local confirmation before publishing success.
void test_complete_update_waits_for_local_confirmation_and_disconnect_ack() {
    FakeStorage storage; commissioned(storage);
    FakeTransport transport; UpdateController controller(&storage, &transport);
    TEST_ASSERT_TRUE(controller.begin(1));
    uint32_t id;
    controller.queue(0xb100, &id, 2);
    upload_to_trial(controller, transport);
    transport.reply_for_last(UpdateTransportEventKind::Reply);
    controller.loop(10);
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::Reset, (int)transport.commands[7].smp);
    transport.reply_for_last(UpdateTransportEventKind::Reply);
    controller.loop(11);
    TEST_ASSERT_TRUE(storage.borrowed); // Still owned until BLE is quiescent.
    transport.reply_for_last(UpdateTransportEventKind::Disconnected);
    controller.loop(12);
    TEST_ASSERT_FALSE(storage.borrowed);
    connect_and_inspect(controller, transport, 3000, slots(0x22, false));
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Checking, (int)snap.jobs[0].phase);
    controller.loop(5100);
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::ImageList,
                          (int)transport.commands[transport.command_count - 1].smp);
    transport.reply_for_last(UpdateTransportEventKind::Reply, slots(0x22, true));
    controller.loop(5101); controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Successful, (int)snap.jobs[0].phase);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Successful, (int)storage.saved.jobs[0].phase);
}

// Packet ACKs must not wear NVS; only phase/command safety barriers persist.
void test_upload_progress_does_not_checkpoint_each_packet() {
    FakeStorage storage; commissioned(storage); storage.staged.size = 640;
    FakeTransport transport; UpdateController controller(&storage, &transport);
    controller.begin(1); uint32_t id; controller.queue(0xb100, &id, 2);
    connect_and_inspect(controller, transport, 3, slots(0x10, true));
    const unsigned before = storage.checkpoints;
    SmpReply offset{}; offset.offset = 256;
    transport.reply_for_last(UpdateTransportEventKind::Reply, offset);
    controller.loop(8);
    TEST_ASSERT_EQUAL_UINT(before, storage.checkpoints);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_UINT32(256, snap.jobs[0].acknowledged);
}

// A failed persistence barrier after TEST must never be followed by RESET.
void test_checkpoint_failure_before_reset_disconnects_and_latches() {
    FakeStorage storage; commissioned(storage);
    FakeTransport transport; UpdateController controller(&storage, &transport);
    controller.begin(1); uint32_t id; controller.queue(0xb100, &id, 2);
    upload_to_trial(controller, transport);
    storage.checkpoint_ok = false;
    transport.reply_for_last(UpdateTransportEventKind::Reply);
    controller.loop(10);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Disconnect,
                          (int)transport.commands[transport.command_count - 1].kind);
    TEST_ASSERT_TRUE(storage.borrowed);
    storage.checkpoint_ok = true;
    transport.reply_for_last(UpdateTransportEventKind::Disconnected);
    controller.loop(11);
    const size_t before = transport.command_count;
    controller.loop(100000);
    TEST_ASSERT_EQUAL_UINT(before, transport.command_count);
    TEST_ASSERT_FALSE(storage.borrowed);
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Error, (int)controller.queue(0xb100, &id, 100001));
}

// A callback with a current token but the wrong event type cannot advance an
// unrelated command (e.g. duplicate Connected during an upload).
void test_unexpected_event_cannot_replace_an_upload_exchange() {
    FakeStorage storage; commissioned(storage);
    FakeTransport transport; UpdateController controller(&storage, &transport);
    controller.begin(1); uint32_t id; controller.queue(0xb100, &id, 2);
    connect_and_inspect(controller, transport, 3, slots(0x10, true));
    const size_t before = transport.command_count;
    transport.reply_for_last(UpdateTransportEventKind::Connected);
    controller.loop(8);
    TEST_ASSERT_EQUAL_UINT(before, transport.command_count);
}

// A late scan result can legitimately need the full subsequent connection
// timeout. The owner must not cancel it at the old scan+connect deadline.
void test_slow_advertiser_gets_full_connection_window_but_not_forever() {
    FakeStorage storage; commissioned(storage); FakeTransport transport;
    UpdateController controller(&storage, &transport);
    TEST_ASSERT_TRUE(controller.begin(1));
    uint32_t id = 0;
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted,
                          (int)controller.queue(0xb100, &id, 2));
    controller.loop(3);
    controller.loop(30003);
    TEST_ASSERT_EQUAL_UINT(1, transport.command_count);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Connect,
                          (int)transport.commands[0].kind);
    controller.loop(35004);
    TEST_ASSERT_EQUAL_UINT(2, transport.command_count);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Disconnect,
                          (int)transport.commands[1].kind);
}

// Lost peers get bounded attempts and cooldown, not an endless reconnect loop.
void test_three_connection_failures_stop_automatic_retry() {
    FakeStorage storage; commissioned(storage);
    FakeTransport transport; UpdateController controller(&storage, &transport);
    controller.begin(1); uint32_t id; controller.queue(0xb100, &id, 2);
    for (unsigned i = 0; i < 3; ++i) {
        const uint64_t now = 3 + i * 61000;
        controller.loop(now);
        TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Connect,
                              (int)transport.commands[transport.command_count - 1].kind);
        transport.reply_for_last(UpdateTransportEventKind::Failed);
        controller.loop(now + 1);
        transport.reply_for_last(UpdateTransportEventKind::Disconnected);
        controller.loop(now + 2);
    }
    const size_t before = transport.command_count;
    controller.loop(300000);
    TEST_ASSERT_EQUAL_UINT(before, transport.command_count);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_UINT8(3, snap.jobs[0].attempts);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsAction, (int)snap.jobs[0].phase);
}

// Recovery must renegotiate off=0, never send the old ACK offset as its first
// packet. Lost staging also cannot silently substitute a different release.
void test_restart_upload_requires_exact_release_and_restarts_negotiation() {
    FakeStorage storage; commissioned(storage); storage.staged.size = 640;
    uint32_t id;
    {
        FakeTransport old;
        UpdateController controller(&storage, &old);
        controller.begin(1); controller.queue(0xb100, &id, 2);
        connect_and_inspect(controller, old, 3, slots(0x10, true));
        SmpReply ack{}; ack.offset = 256;
        old.reply_for_last(UpdateTransportEventKind::Reply, ack); controller.loop(8);
    }
    storage.has_stage = false; storage.borrowed = false; // Actual anchor restart.
    FakeTransport transport; UpdateController restored(&storage, &transport);
    TEST_ASSERT_TRUE(restored.begin(1));
    restored.loop(2);
    TEST_ASSERT_EQUAL_UINT(0, transport.command_count);
    TEST_ASSERT_FALSE(restored.retry(id, 3));
    storage.has_stage = true; storage.staged.image_hash[0] = 0x33;
    TEST_ASSERT_FALSE(restored.retry(id, 4));
    storage.staged.image_hash[0] = 0x22;
    TEST_ASSERT_TRUE(restored.retry(id, 5));
    connect_and_inspect(restored, transport, 6, slots(0x10, true));
    const auto &first = transport.commands[transport.command_count - 1];
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::Upload, (int)first.smp);
    const uint8_t off_zero[] = {0xa4, 0x63, 'o', 'f', 'f', 0x00};
    TEST_ASSERT_EQUAL_MEMORY(off_zero, first.frame + 8, sizeof(off_zero));
    SmpReply ack{}; ack.offset = 512; // Receiver can resume beyond first chunk.
    transport.reply_for_last(UpdateTransportEventKind::Reply, ack); restored.loop(11);
    UpdateControllerSnapshot snap{}; restored.snapshot(snap);
    TEST_ASSERT_EQUAL_UINT32(512, snap.jobs[0].acknowledged);
}

// Restart after a possible TEST never replays TEST/reset, even if an operator
// retries a pending result. Without a file it can still verify local success.
void test_restart_uncertain_trial_only_inspects_and_operator_retry_is_bounded() {
    FakeStorage storage; commissioned(storage); uint32_t id;
    {
        FakeTransport old; UpdateController controller(&storage, &old);
        controller.begin(1); controller.queue(0xb100, &id, 2);
        upload_to_trial(controller, old);
    }
    storage.has_stage = false; storage.borrowed = false;
    FakeTransport transport; UpdateController restored(&storage, &transport);
    TEST_ASSERT_TRUE(restored.begin(1));
    connect_and_inspect(restored, transport, 2, slots(0x10, true, true));
    UpdateControllerSnapshot snap{}; restored.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsAction, (int)snap.jobs[0].phase);
    transport.reply_for_last(UpdateTransportEventKind::Disconnected); restored.loop(7);
    TEST_ASSERT_TRUE(restored.retry(id, 8));
    connect_and_inspect(restored, transport, 61000, slots(0x22, true));
    restored.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Successful, (int)snap.jobs[0].phase);
    for (size_t i = 0; i < transport.command_count; ++i) {
        const auto &c = transport.commands[i];
        if (c.kind != UpdateTransportCommandKind::Exchange) continue;
        TEST_ASSERT_TRUE(c.smp == SmpCommand::ImageList || c.smp == SmpCommand::TagStatus);
    }
}

// Cancellation after TEST means inspect, not 'installation undone'. A stale
// TEST reply and a stale disconnect ACK must not release borrowed image bytes.
void test_cancel_after_trial_ignores_stale_events_and_reports_real_outcome() {
    FakeStorage storage; commissioned(storage); FakeTransport transport;
    UpdateController controller(&storage, &transport); uint32_t id;
    controller.begin(1); controller.queue(0xb100, &id, 2);
    upload_to_trial(controller, transport);
    transport.reply_for_last(UpdateTransportEventKind::Reply);
    const auto trial = transport.commands[transport.command_count - 1];
    TEST_ASSERT_TRUE(controller.cancel(id, 10));
    controller.loop(11); // Queued stale TEST reply is ignored.
    transport.events[transport.event_count++] = {
        UpdateTransportEventKind::Disconnected, trial.operation, trial.session, true, {}};
    controller.loop(12);
    TEST_ASSERT_TRUE(storage.borrowed);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Checking, (int)snap.jobs[0].phase);
    transport.reply_for_last(UpdateTransportEventKind::Disconnected); controller.loop(13);
    TEST_ASSERT_FALSE(storage.borrowed);
    connect_and_inspect(controller, transport, 3000, slots(0x10, true));
    controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Failed, (int)snap.jobs[0].phase);
    for (size_t i = 0; i < transport.command_count; ++i)
        TEST_ASSERT_FALSE(transport.commands[i].kind == UpdateTransportCommandKind::Exchange &&
                           transport.commands[i].smp == SmpCommand::Reset);
}

// A stuck BLE worker must keep the staging lock and prevent a second session,
// even when the job is already cancelled and the ordinary timeout elapsed.
void test_unacknowledged_disconnect_disables_updater_without_releasing_image() {
    FakeStorage storage; commissioned(storage); FakeTransport transport;
    UpdateController controller(&storage, &transport); uint32_t id;
    controller.begin(1); controller.queue(0xb100, &id, 2);
    connect_and_inspect(controller, transport, 3, slots(0x10, true));
    TEST_ASSERT_TRUE(controller.cancel(id, 8));
    uint32_t next_id = 0;
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Conflict,
                          (int)controller.queue(0xb100, &next_id, 9));
    controller.loop(11000);
    TEST_ASSERT_TRUE(storage.borrowed);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_TRUE(snap.disabled);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Cancelled, (int)snap.jobs[0].phase);
    transport.reply_for_last(UpdateTransportEventKind::Disconnected); controller.loop(11001);
    TEST_ASSERT_FALSE(storage.borrowed);
    controller.snapshot(snap);
    TEST_ASSERT_TRUE(snap.disabled); // A late ACK cannot silently revive jobs.
}

// Matching version/secure BLE address is insufficient: a wrong hardware ID
// cannot reach image inspection, upload or reset.
void test_wrong_hardware_identity_stops_before_any_image_command() {
    FakeStorage storage; commissioned(storage); FakeTransport transport;
    UpdateController controller(&storage, &transport); uint32_t id;
    controller.begin(1); controller.queue(0xb100, &id, 2); controller.loop(3);
    transport.reply_for_last(UpdateTransportEventKind::Connected); controller.loop(4);
    transport.reply_for_last(UpdateTransportEventKind::Secured); controller.loop(5);
    SmpReply wrong = status(); wrong.tag.id[0] = '0';
    transport.reply_for_last(UpdateTransportEventKind::Reply, wrong); controller.loop(6);
    TEST_ASSERT_EQUAL_UINT(4, transport.command_count);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Disconnect, (int)transport.commands[3].kind);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsAction, (int)snap.jobs[0].phase);
    TEST_ASSERT_FALSE(snap.observed[0].identity_valid);
}

// Local confirmation polling has one overall deadline; an unconfirmed image
// cannot hold a maintenance connection forever or be reported successful.
void test_unconfirmed_image_has_an_overall_confirmation_deadline() {
    FakeStorage storage; commissioned(storage); FakeTransport transport;
    UpdateController controller(&storage, &transport); uint32_t id;
    controller.begin(1); controller.queue(0xb100, &id, 2);
    connect_and_inspect(controller, transport, 3, slots(0x22, false));
    controller.loop(150007);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsAction, (int)snap.jobs[0].phase);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Disconnect,
                          (int)transport.commands[transport.command_count - 1].kind);
    TEST_ASSERT_FALSE(storage.borrowed);
}

// Break caught: persisting an association before authenticated identity proof
// would let an impostor claim an adopted tag.
void test_commissioning_persists_only_after_secure_matching_identity_and_close() {
    FakeStorage storage; FakeTransport transport;
    UpdateController controller(&storage, &transport);
    TEST_ASSERT_TRUE(controller.begin(1));
    UpdateAssociation association{{0xb100, "f00dbaad12345678"}, {1,2,3,4,5,6}, 0};
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted,
                          (int)controller.commission(association, 123456, 2));
    controller.loop(3);
    TEST_ASSERT_TRUE(transport.commands[0].kind == UpdateTransportCommandKind::Connect);
    transport.reply_for_last(UpdateTransportEventKind::Connected); controller.loop(4);
    TEST_ASSERT_TRUE(transport.commands[1].commissioning);
    TEST_ASSERT_EQUAL_UINT32(123456, transport.commands[1].passkey);
    transport.reply_for_last(UpdateTransportEventKind::Secured); controller.loop(5);
    transport.reply_for_last(UpdateTransportEventKind::Reply, status()); controller.loop(6);
    TEST_ASSERT_EQUAL_UINT(1, storage.saved.association_count); // durable before disconnect
    const UpdateTransportCommand stale = transport.commands[2];
    transport.events[transport.event_count++] = {UpdateTransportEventKind::Reply, stale.operation,
                                                  stale.session, true, status()};
    controller.loop(6);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePairingPhase::Disconnecting, (int)snap.pairing.phase);
    transport.reply_for_last(UpdateTransportEventKind::Disconnected); controller.loop(7);
    TEST_ASSERT_EQUAL_UINT(1, storage.saved.association_count);
    controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePairingPhase::Successful, (int)snap.pairing.phase);
}

// Break caught: a wrong-identity callback must not persist the proposed
// association or turn a stale callback into success.
void test_commissioning_rejects_wrong_identity_callback() {
    FakeStorage storage; FakeTransport transport;
    UpdateController controller(&storage, &transport); controller.begin(1);
    UpdateAssociation association{{0xb100, "f00dbaad12345678"}, {1,2,3,4,5,6}, 0};
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted,
                          (int)controller.commission(association, 1, 2));
    controller.loop(3); transport.reply_for_last(UpdateTransportEventKind::Connected); controller.loop(4);
    transport.reply_for_last(UpdateTransportEventKind::Secured); controller.loop(5);
    SmpReply wrong = status(); wrong.tag.id[0] = '0';
    transport.reply_for_last(UpdateTransportEventKind::Reply, wrong); controller.loop(6);
    TEST_ASSERT_EQUAL_UINT(0, storage.saved.association_count);
    transport.reply_for_last(UpdateTransportEventKind::Disconnected); controller.loop(7);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePairingPhase::Failed, (int)snap.pairing.phase);
    TEST_ASSERT_EQUAL_UINT(0, storage.saved.association_count);
}

// Break caught: treating an encryption event without authenticated Secure
// Connections as proof would persist an association without a usable bond.
void test_commissioning_rejects_unauthenticated_security() {
    FakeStorage storage; FakeTransport transport;
    UpdateController controller(&storage, &transport); controller.begin(1);
    UpdateAssociation association{{0xb100, "f00dbaad12345678"}, {1,2,3,4,5,6}, 0};
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted,
                          (int)controller.commission(association, 1, 2));
    controller.loop(3); transport.reply_for_last(UpdateTransportEventKind::Connected); controller.loop(4);
    const UpdateTransportCommand security = transport.commands[1];
    transport.events[transport.event_count++] = {UpdateTransportEventKind::Secured, security.operation,
                                                  security.session, false, {}};
    controller.loop(5);
    TEST_ASSERT_EQUAL_UINT(0, storage.saved.association_count);
    transport.reply_for_last(UpdateTransportEventKind::Disconnected); controller.loop(6);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)UpdatePairingPhase::Failed, (int)snap.pairing.phase);
}

// Break caught: allowing commissioning beside a queued or closing updater
// session would multiplex a single BLE transport and blur callback ownership.
void test_commissioning_conflicts_with_active_work_and_lost_close_ack_disables() {
    FakeStorage storage; commissioned(storage); FakeTransport transport;
    UpdateController controller(&storage, &transport); controller.begin(1);
    uint32_t id = 0; TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted,
                                           (int)controller.queue(0xb100, &id, 2));
    UpdateAssociation fresh{{0xb101, "97c97886a9e4421b"}, {6,5,4,3,2,1}, 0};
    controller.loop(3);
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Conflict,
                          (int)controller.commission(fresh, 1, 3));
    controller.loop(4); transport.reply_for_last(UpdateTransportEventKind::Connected); controller.loop(5);
    transport.reply_for_last(UpdateTransportEventKind::Secured); controller.loop(6);
    TEST_ASSERT_TRUE(controller.cancel(id, 7));
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Conflict,
                          (int)controller.commission(fresh, 1, 8));
    controller.loop(18000);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_TRUE(snap.disabled);
}

// Break caught: a storage failure after verified commissioning must fail closed
// and must not expose a durable association.
void test_commissioning_checkpoint_failure_disables_without_association() {
    FakeStorage storage; FakeTransport transport;
    UpdateController controller(&storage, &transport); controller.begin(1);
    UpdateAssociation association{{0xb100, "f00dbaad12345678"}, {1,2,3,4,5,6}, 0};
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted,
                          (int)controller.commission(association, 0, 2));
    controller.loop(3); transport.reply_for_last(UpdateTransportEventKind::Connected); controller.loop(4);
    transport.reply_for_last(UpdateTransportEventKind::Secured); controller.loop(5);
    storage.checkpoint_ok = false;
    transport.reply_for_last(UpdateTransportEventKind::Reply, status()); controller.loop(6);
    TEST_ASSERT_EQUAL_UINT(0, storage.saved.association_count);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_TRUE(snap.disabled);
}

// Break caught: blocking a second queued target while the single transport is
// busy loses legitimate serialized work that the controller already owns.
void test_queue_allows_a_second_tag_while_an_ordinary_session_is_active() {
    FakeStorage storage; commissioned(storage);
    storage.saved.association_count = 2;
    storage.saved.associations[1] = {{0xb101, "97c97886a9e4421b"}, {6,5,4,3,2,1}, 0};
    FakeTransport transport; UpdateController controller(&storage, &transport);
    TEST_ASSERT_TRUE(controller.begin(1));
    uint32_t first = 0, second = 0;
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted, (int)controller.queue(0xb100, &first, 2));
    controller.loop(3);
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted, (int)controller.queue(0xb101, &second, 4));
    TEST_ASSERT_TRUE(second != first);
}

// Break caught: accepting unspecified broadcast-like addresses could persist a
// target the worker can never safely connect to.
void test_commissioning_rejects_zero_or_broadcast_address_without_transport() {
    FakeStorage storage; FakeTransport transport;
    UpdateController controller(&storage, &transport); controller.begin(1);
    UpdateAssociation association{{0xb100, "f00dbaad12345678"}, {}, 0};
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Invalid,
                          (int)controller.commission(association, 1, 2));
    std::memset(association.address, 0xff, sizeof(association.address));
    association.address_type = 1;
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Invalid,
                          (int)controller.commission(association, 1, 3));
    TEST_ASSERT_EQUAL_UINT(0, transport.command_count);
}

// Break caught: starting a session when the association table is full defers a
// deterministic capacity failure until after pairing has touched the tag.
void test_commissioning_full_association_table_does_not_start_transport() {
    FakeStorage storage; FakeTransport transport;
    storage.saved.association_count = UPDATE_SNAPSHOT_MAX_ASSOCIATIONS;
    UpdateController controller(&storage, &transport); controller.begin(1);
    UpdateAssociation association{{0xb100, "f00dbaad12345678"}, {1,2,3,4,5,6}, 0};
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Capacity,
                          (int)controller.commission(association, 1, 2));
    TEST_ASSERT_EQUAL_UINT(0, transport.command_count);
}

// Break caught: rolling back a failed re-checkpoint must not remove an already
// stored association merely because it happens to occupy the final slot.
void test_commissioning_checkpoint_failure_preserves_existing_last_association() {
    FakeStorage storage; commissioned(storage); FakeTransport transport;
    UpdateController controller(&storage, &transport); controller.begin(1);
    const UpdateAssociation association = storage.saved.associations[0];
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted,
                          (int)controller.commission(association, 1, 2));
    controller.loop(3); transport.reply_for_last(UpdateTransportEventKind::Connected); controller.loop(4);
    transport.reply_for_last(UpdateTransportEventKind::Secured); controller.loop(5);
    storage.checkpoint_ok = false;
    transport.reply_for_last(UpdateTransportEventKind::Reply, status()); controller.loop(6);
    UpdateControllerSnapshot snap{}; controller.snapshot(snap);
    TEST_ASSERT_EQUAL_UINT(1, snap.association_count);
    TEST_ASSERT_EQUAL_MEMORY(&association, &snap.associations[0], sizeof(association));
}

void setUp() {}
void tearDown() {}

#include "motion_tests.h"

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_motion_queues_second_change_without_losing_first);
    RUN_TEST(test_motion_lost_write_reply_never_claims_success_or_replays);
    RUN_TEST(test_motion_insecure_unsupported_denied_and_sensor_fault_do_not_succeed);
    RUN_TEST(test_motion_waits_for_update_session_and_forget_cancels_queued_work);
    RUN_TEST(test_motion_forget_during_session_removes_state_after_disconnect);
    RUN_TEST(test_motion_preserves_tuning_and_confirms_application);
    RUN_TEST(test_motion_rejects_invalid_unpaired_and_wrong_identity);
    RUN_TEST(test_queue_requires_a_commissioned_target);
    RUN_TEST(test_upload_verifies_inactive_hash_then_checkpoints_before_test);
    RUN_TEST(test_insecure_link_becomes_needs_action_without_an_smp_request);
    RUN_TEST(test_checkpoint_failure_latches_controller_before_any_transport_command);
    RUN_TEST(test_complete_update_waits_for_local_confirmation_and_disconnect_ack);
    RUN_TEST(test_upload_progress_does_not_checkpoint_each_packet);
    RUN_TEST(test_checkpoint_failure_before_reset_disconnects_and_latches);
    RUN_TEST(test_unexpected_event_cannot_replace_an_upload_exchange);
    RUN_TEST(test_three_connection_failures_stop_automatic_retry);
    RUN_TEST(test_slow_advertiser_gets_full_connection_window_but_not_forever);
    RUN_TEST(test_restart_upload_requires_exact_release_and_restarts_negotiation);
    RUN_TEST(test_restart_uncertain_trial_only_inspects_and_operator_retry_is_bounded);
    RUN_TEST(test_cancel_after_trial_ignores_stale_events_and_reports_real_outcome);
    RUN_TEST(test_unacknowledged_disconnect_disables_updater_without_releasing_image);
    RUN_TEST(test_wrong_hardware_identity_stops_before_any_image_command);
    RUN_TEST(test_unconfirmed_image_has_an_overall_confirmation_deadline);
    RUN_TEST(test_commissioning_persists_only_after_secure_matching_identity_and_close);
    RUN_TEST(test_commissioning_rejects_wrong_identity_callback);
    RUN_TEST(test_commissioning_rejects_unauthenticated_security);
    RUN_TEST(test_commissioning_conflicts_with_active_work_and_lost_close_ack_disables);
    RUN_TEST(test_commissioning_checkpoint_failure_disables_without_association);
    RUN_TEST(test_queue_allows_a_second_tag_while_an_ordinary_session_is_active);
    RUN_TEST(test_commissioning_rejects_zero_or_broadcast_address_without_transport);
    RUN_TEST(test_commissioning_full_association_table_does_not_start_transport);
    RUN_TEST(test_commissioning_checkpoint_failure_preserves_existing_last_association);
    return UNITY_END();
}
