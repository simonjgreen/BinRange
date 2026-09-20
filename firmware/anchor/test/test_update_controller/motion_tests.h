// These tests use the same real controller and fake external BLE/storage boundary.
static SmpReply motion_status() {
    auto tag = status(); tag.tag.confirmed = true; tag.tag.config_status_known = true;
    return tag;
}
static void motion_connect(UpdateController &c, FakeTransport &t) {
    c.loop(3); t.reply_for_last(UpdateTransportEventKind::Connected); c.loop(4);
    t.reply_for_last(UpdateTransportEventKind::Secured); c.loop(5);
    t.reply_for_last(UpdateTransportEventKind::Reply, motion_status()); c.loop(6);
}
void test_motion_preserves_tuning_and_confirms_application() {
    FakeStorage storage; commissioned(storage); FakeTransport t;
    UpdateController c(&storage, &t); TEST_ASSERT_TRUE(c.begin(1));
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Accepted, (int)c.motion_request(0xb100, 300000, 0, 2));
    motion_connect(c, t);
    TEST_ASSERT_FALSE(t.commands[1].commissioning);
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::ConfigRead, (int)t.commands[3].smp);
    SmpReply config{}; config.config = {7000, 600000, 45000, 320, 3};
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(7);
    const auto &write = t.commands[4];
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::ConfigWrite, (int)write.smp);
    auto wire = write; wire.frame[0] = 3;
    SmpAssembler decoder; decoder.begin(SmpCommand::ConfigWrite, wire.frame[6]);
    TEST_ASSERT_EQUAL_INT((int)SmpFeed::Complete, (int)decoder.feed(wire.frame, wire.frame_size));
    TEST_ASSERT_EQUAL_UINT32(300000, decoder.reply()->config.idle_ms);
    TEST_ASSERT_EQUAL_UINT32(7000, decoder.reply()->config.moving_ms);
    TEST_ASSERT_EQUAL_UINT32(45000, decoder.reply()->config.quiet_ms);
    TEST_ASSERT_EQUAL_UINT32(320, decoder.reply()->config.threshold_mg);
    TEST_ASSERT_EQUAL_UINT32(3, decoder.reply()->config.duration_samples);
    config.config.idle_ms = 300000;
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(8);
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::ConfigRead, (int)t.commands[5].smp);
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(9);
    auto tag = motion_status(); tag.tag.config_pending = true;
    t.reply_for_last(UpdateTransportEventKind::Reply, tag); c.loop(10);
    UpdateControllerSnapshot snap; c.snapshot(snap);
    TEST_ASSERT_NOT_EQUAL((int)MotionPhase::Applied, (int)snap.motion[0].phase);
    c.loop(2010); tag.tag.config_pending = false;
    t.reply_for_last(UpdateTransportEventKind::Reply, tag); c.loop(2011);
    c.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)MotionPhase::Applied, (int)snap.motion[0].phase);
    TEST_ASSERT_TRUE(snap.motion[0].known);
    TEST_ASSERT_EQUAL_UINT32(300000, snap.motion[0].config.idle_ms);
    TEST_ASSERT_TRUE(c.busy());
    t.reply_for_last(UpdateTransportEventKind::Disconnected); c.loop(2012);
    TEST_ASSERT_FALSE(c.busy());
}
void test_motion_rejects_invalid_unpaired_and_wrong_identity() {
    FakeStorage storage; commissioned(storage); FakeTransport t;
    UpdateController c(&storage, &t); c.begin(1);
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Invalid, (int)c.motion_request(0xb100, 59999, 0, 2));
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::Invalid, (int)c.motion_request(0xb100, 0, 60001, 2));
    TEST_ASSERT_EQUAL_INT((int)UpdateQueueResult::NeedsAction, (int)c.motion_request(0xb101, 300000, 5000, 2));
    c.motion_request(0xb100, 300000, 5000, 2); c.loop(3);
    t.reply_for_last(UpdateTransportEventKind::Connected); c.loop(4);
    t.reply_for_last(UpdateTransportEventKind::Secured); c.loop(5);
    auto tag = motion_status(); tag.tag.tag = 0xb101;
    t.reply_for_last(UpdateTransportEventKind::Reply, tag); c.loop(6);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Disconnect, (int)t.commands[3].kind);
    UpdateControllerSnapshot snap; c.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)MotionPhase::Failed, (int)snap.motion[0].phase);
    TEST_ASSERT_FALSE(snap.motion[0].known);
}

void test_motion_queues_second_change_without_losing_first() {
    FakeStorage storage; commissioned(storage); FakeTransport t;
    UpdateController c(&storage, &t); c.begin(1);
    c.motion_request(0xb100, 300000, 0, 2); motion_connect(c, t);
    // The second control is changed while the first BLE operation is active.
    c.motion_request(0xb100, 0, 12000, 6);
    SmpReply config{}; config.config = {5000, 600000, 30000, 250, 2};
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(7);
    config.config.idle_ms = 300000;
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(8);
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(9);
    t.reply_for_last(UpdateTransportEventKind::Reply, motion_status()); c.loop(10);
    t.reply_for_last(UpdateTransportEventKind::Disconnected); c.loop(11);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Connect, (int)t.commands[t.command_count-1].kind);
    t.reply_for_last(UpdateTransportEventKind::Connected); c.loop(12);
    t.reply_for_last(UpdateTransportEventKind::Secured); c.loop(13);
    t.reply_for_last(UpdateTransportEventKind::Reply, motion_status()); c.loop(14);
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(15);
    auto wire = t.commands[t.command_count-1]; wire.frame[0] = 3;
    SmpAssembler decoder; decoder.begin(SmpCommand::ConfigWrite, wire.frame[6]);
    TEST_ASSERT_EQUAL_INT((int)SmpFeed::Complete, (int)decoder.feed(wire.frame, wire.frame_size));
    TEST_ASSERT_EQUAL_UINT32(300000, decoder.reply()->config.idle_ms);
    TEST_ASSERT_EQUAL_UINT32(12000, decoder.reply()->config.moving_ms);
}
void test_motion_lost_write_reply_never_claims_success_or_replays() {
    FakeStorage storage; commissioned(storage); FakeTransport t;
    UpdateController c(&storage, &t); c.begin(1);
    c.motion_request(0xb100, 300000, 0, 2); motion_connect(c, t);
    SmpReply config{}; config.config = {5000, 600000, 30000, 250, 2};
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(7);
    // Timeout wins even if a late write reply is already waiting to be drained.
    config.config.idle_ms = 300000; t.reply_for_last(UpdateTransportEventKind::Reply, config);
    c.loop(10007);
    UpdateControllerSnapshot snap; c.snapshot(snap);
    TEST_ASSERT_EQUAL_INT((int)MotionPhase::Failed, (int)snap.motion[0].phase);
    TEST_ASSERT_FALSE(snap.motion[0].known);
    TEST_ASSERT_EQUAL_INT((int)UpdateTransportCommandKind::Disconnect, (int)t.commands[t.command_count-1].kind);
    t.reply_for_last(UpdateTransportEventKind::Disconnected); c.loop(10008);
    size_t count = t.command_count; c.loop(1000000);
    TEST_ASSERT_EQUAL_UINT(count, t.command_count);
    c.motion_request(0xb100, 0, 0, 1000001); c.loop(1000002);
    t.reply_for_last(UpdateTransportEventKind::Connected); c.loop(1000003);
    t.reply_for_last(UpdateTransportEventKind::Secured); c.loop(1000004);
    t.reply_for_last(UpdateTransportEventKind::Reply, motion_status()); c.loop(1000005);
    t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(1000006);
    TEST_ASSERT_EQUAL_INT((int)SmpCommand::TagStatus, (int)t.commands[t.command_count-1].smp);
    t.reply_for_last(UpdateTransportEventKind::Reply, motion_status()); c.loop(1000007);
    c.snapshot(snap); TEST_ASSERT_TRUE(snap.motion[0].known);
    TEST_ASSERT_EQUAL_UINT32(300000, snap.motion[0].config.idle_ms);
}
void test_motion_insecure_unsupported_denied_and_sensor_fault_do_not_succeed() {
    for (unsigned failure = 0; failure < 4; ++failure) {
        FakeStorage storage; commissioned(storage); FakeTransport t;
        UpdateController c(&storage, &t); c.begin(1); c.motion_request(0xb100, 300000, 0, 2);
        c.loop(3); t.reply_for_last(UpdateTransportEventKind::Connected); c.loop(4);
        t.reply_for_last(UpdateTransportEventKind::Secured);
        if (failure == 0) t.events[t.event_count-1].bonded_mitm_sc = false;
        c.loop(5);
        if (failure) {
            auto tag = motion_status();
            if (failure == 1) tag.tag.config_status_known = false;
            t.reply_for_last(UpdateTransportEventKind::Reply, tag); c.loop(6);
            if (failure >= 2) {
                SmpReply config{}; config.config = {5000, 300000, 30000, 250, 2};
                if (failure == 2) config.outcome = SmpOutcome::Denied;
                t.reply_for_last(UpdateTransportEventKind::Reply, config); c.loop(7);
                if (failure == 3) {
                    tag.tag.sensor_error = -5;
                    t.reply_for_last(UpdateTransportEventKind::Reply, tag); c.loop(8);
                }
            }
        }
        UpdateControllerSnapshot snap; c.snapshot(snap);
        TEST_ASSERT_EQUAL_INT((int)MotionPhase::Failed, (int)snap.motion[0].phase);
        TEST_ASSERT_FALSE(snap.motion[0].known);
        for (size_t i = 0; i < t.command_count; ++i)
            TEST_ASSERT_FALSE(t.commands[i].kind == UpdateTransportCommandKind::Exchange && t.commands[i].smp == SmpCommand::ConfigWrite);
    }
}
void test_motion_waits_for_update_session_and_forget_cancels_queued_work() {
    FakeStorage storage; commissioned(storage); FakeTransport t;
    UpdateController c(&storage, &t); c.begin(1);
    uint32_t job; c.queue(0xb100, &job, 2);
    c.motion_request(0xb100, 300000, 0, 2);
    c.loop(3);
    UpdateControllerSnapshot snap; c.snapshot(snap);
    TEST_ASSERT_EQUAL_UINT32(job, snap.active_job_id);
    TEST_ASSERT_EQUAL_INT((int)MotionPhase::Queued, (int)snap.motion[0].phase);
    c.motion_forget(0xb100, 4); c.cancel(job, 4);
    t.reply_for_last(UpdateTransportEventKind::Disconnected); c.loop(5);
    size_t count = t.command_count; c.loop(1000000);
    TEST_ASSERT_EQUAL_UINT(count, t.command_count);
}
void test_motion_forget_during_session_removes_state_after_disconnect() {
    FakeStorage storage; commissioned(storage); FakeTransport t;
    UpdateController c(&storage, &t); c.begin(1);
    c.motion_request(0xb100, 300000, 0, 2); c.loop(3);
    c.motion_forget(0xb100, 4);
    t.reply_for_last(UpdateTransportEventKind::Disconnected); c.loop(5);
    UpdateControllerSnapshot snap; c.snapshot(snap);
    for (const auto &m : snap.motion) TEST_ASSERT_NOT_EQUAL(0xb100, m.tag);
}
