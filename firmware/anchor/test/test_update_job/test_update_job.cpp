#include <unity.h>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include "core/update_job.h"

static_assert(std::is_same<decltype(std::declval<const UpdateJob &>().id()),
                           uint32_t>::value,
              "job IDs must be exposed by value");
static_assert(std::is_same<decltype(std::declval<const UpdateJob &>().target()),
                           const UpdateTarget &>::value,
              "targets must be read-only through jobs");
static_assert(std::is_same<decltype(std::declval<const UpdateJob &>().release()),
                           const UpdateRelease &>::value,
              "releases must be read-only through jobs");
static_assert(std::is_same<decltype(std::declval<const UpdateJob &>().phase()),
                           UpdatePhase>::value,
              "phases must be exposed by value, not a writable reference");
static_assert(!std::is_assignable<
                  decltype(std::declval<UpdateJob &>().target()), UpdateTarget>::value,
              "callers must not replace a queued target");
static_assert(!std::is_assignable<
                  decltype(std::declval<UpdateJob &>().release()), UpdateRelease>::value,
              "callers must not replace a queued release");
static_assert(!std::is_copy_assignable<UpdateJob>::value,
              "callers must not replace a queued job by copy assignment");
static_assert(!std::is_move_assignable<UpdateJob>::value,
              "callers must not replace a queued job by move assignment");

static UpdateTarget target(uint16_t tag = 0x4556) {
    UpdateTarget t = {tag, "0011223344556677"};
    return t;
}

static UpdateRelease release(uint32_t size = 64) {
    UpdateRelease r = {};
    r.size = size;
    memset(r.file_sha, 0x11, sizeof(r.file_sha));
    memset(r.image_hash, 0x22, sizeof(r.image_hash));
    strcpy(r.version, "1.2.3");
    return r;
}

static UpdateJob *enqueue(UpdateQueue &q, uint16_t tag = 0x4556,
                          uint64_t now = 10) {
    return q.enqueue(target(tag), release(), now);
}

static UpdateJob *checking_job(UpdateQueue &q) {
    UpdateJob *job = enqueue(q);
    TEST_ASSERT_NOT_NULL(job);
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Waiting, 11));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Connecting, 12));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Checking, 13));
    return job;
}

static UpdateJob *job_at_phase(UpdateQueue &q, UpdatePhase phase,
                               uint16_t tag = 0x4556) {
    UpdateJob *job = enqueue(q, tag);
    if (!job || phase == UpdatePhase::Queued) return job;
    if (!update_transition(*job, UpdatePhase::Waiting, 11)) return nullptr;
    if (phase == UpdatePhase::Waiting) return job;
    if (phase == UpdatePhase::NeedsRelease)
        return update_transition(*job, UpdatePhase::NeedsRelease, 12) ? job : nullptr;
    if (!update_transition(*job, UpdatePhase::Connecting, 12)) return nullptr;
    if (phase == UpdatePhase::Connecting) return job;
    if (phase == UpdatePhase::Checking || phase == UpdatePhase::NeedsAction)
        return update_transition(*job,
                                 phase == UpdatePhase::Checking
                                     ? UpdatePhase::Checking
                                     : UpdatePhase::NeedsAction,
                                 13)
                   ? job
                   : nullptr;
    if (!update_transition(*job, UpdatePhase::Uploading, 13)) return nullptr;
    if (phase == UpdatePhase::Uploading) return job;
    if (!update_transition(*job, UpdatePhase::Rebooting, 14)) return nullptr;
    return phase == UpdatePhase::Rebooting ? job : nullptr;
}

void test_uploaded_bytes_equal_size_is_not_success() {
    UpdateQueue q;
    UpdateJob *job = enqueue(q);
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Waiting, 11));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Connecting, 12));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Uploading, 13));

    TEST_ASSERT_TRUE(update_progress(*job, job->release().size, 14));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Uploading, (int)job->phase());
    TEST_ASSERT_EQUAL_UINT32(job->release().size, job->acknowledged());
}

void test_inspection_retry_preserves_acknowledged_bytes_but_cannot_resume_writes() {
    UpdateQueue q;
    UpdateJob *job = job_at_phase(q, UpdatePhase::Uploading);
    TEST_ASSERT_NOT_NULL(job);
    TEST_ASSERT_TRUE(update_progress(*job, 64, 14));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Rebooting, 15));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::NeedsAction, 16));
    TEST_ASSERT_TRUE(update_retry(*job, 17));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Checking, (int)job->phase());
    TEST_ASSERT_EQUAL_UINT32(64, job->acknowledged());
    TEST_ASSERT_TRUE(job->trial_may_be_armed());
    TEST_ASSERT_FALSE(update_progress(*job, 32, 18));
    TEST_ASSERT_FALSE(update_transition(*job, UpdatePhase::Uploading, 18));
}

void test_pretrial_retry_discards_the_old_receiver_offset() {
    UpdateQueue q;
    UpdateJob *job = job_at_phase(q, UpdatePhase::Uploading);
    TEST_ASSERT_NOT_NULL(job);
    TEST_ASSERT_TRUE(update_progress(*job, 32, 14));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::NeedsAction, 15));
    TEST_ASSERT_TRUE(update_retry(*job, 16));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Waiting, (int)job->phase());
    TEST_ASSERT_EQUAL_UINT32(0, job->acknowledged());
}

void test_exact_active_hash_and_confirmation_completes_checking_job() {
    UpdateQueue q;
    UpdateJob *job = checking_job(q);

    TEST_ASSERT_TRUE(update_complete(*job, job->release().image_hash, true, 14));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Successful, (int)job->phase());
}

void test_same_version_with_another_hash_does_not_complete() {
    UpdateQueue q;
    UpdateJob *job = checking_job(q);
    uint8_t other_hash[32];
    memcpy(other_hash, job->release().image_hash, sizeof(other_hash));
    other_hash[0] ^= 0xff;

    TEST_ASSERT_FALSE(update_complete(*job, other_hash, true, 14));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Checking, (int)job->phase());
}

void test_restart_does_not_resume_a_stale_offset() {
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsRelease,
                          (int)update_after_restart(UpdatePhase::Uploading));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Checking,
                          (int)update_after_restart(UpdatePhase::Rebooting));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Checking,
                          (int)update_after_restart(UpdatePhase::Checking));
}

void test_restart_maps_pre_transfer_states_to_needs_release() {
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsRelease,
                          (int)update_after_restart(UpdatePhase::Queued));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsRelease,
                          (int)update_after_restart(UpdatePhase::Waiting));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::NeedsRelease,
                          (int)update_after_restart(UpdatePhase::Connecting));
}

void test_restart_preserves_terminal_states() {
    const UpdatePhase terminal[] = {
        UpdatePhase::Successful, UpdatePhase::Failed, UpdatePhase::Cancelled};
    for (UpdatePhase phase : terminal)
        TEST_ASSERT_EQUAL_INT((int)phase, (int)update_after_restart(phase));
}

void test_transition_accepts_only_the_approved_policy_edges() {
    UpdateQueue q;
    UpdateJob *job = enqueue(q);

    TEST_ASSERT_FALSE(update_transition(*job, UpdatePhase::Uploading, 11));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Waiting, 11));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Connecting, 12));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Uploading, 13));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Rebooting, 14));
    TEST_ASSERT_TRUE(job->trial_may_be_armed());
    TEST_ASSERT_FALSE(update_transition(*job, UpdatePhase::Cancelled, 15));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Checking, 15));
    TEST_ASSERT_FALSE(update_transition(*job, UpdatePhase::Successful, 16));
}

void test_transition_table_allows_each_documented_edge() {
    struct Edge { UpdatePhase from; UpdatePhase to; };
    const Edge edges[] = {
        {UpdatePhase::Queued, UpdatePhase::Waiting},
        {UpdatePhase::Queued, UpdatePhase::Cancelled},
        {UpdatePhase::Queued, UpdatePhase::Failed},
        {UpdatePhase::Waiting, UpdatePhase::Connecting},
        {UpdatePhase::Waiting, UpdatePhase::Cancelled},
        {UpdatePhase::Waiting, UpdatePhase::Failed},
        {UpdatePhase::Waiting, UpdatePhase::NeedsRelease},
        {UpdatePhase::Connecting, UpdatePhase::Waiting},
        {UpdatePhase::Connecting, UpdatePhase::Uploading},
        {UpdatePhase::Connecting, UpdatePhase::Checking},
        {UpdatePhase::Connecting, UpdatePhase::Cancelled},
        {UpdatePhase::Connecting, UpdatePhase::Failed},
        {UpdatePhase::Connecting, UpdatePhase::NeedsAction},
        {UpdatePhase::Uploading, UpdatePhase::Waiting},
        {UpdatePhase::Uploading, UpdatePhase::Rebooting},
        {UpdatePhase::Uploading, UpdatePhase::Cancelled},
        {UpdatePhase::Uploading, UpdatePhase::Failed},
        {UpdatePhase::Uploading, UpdatePhase::NeedsRelease},
        {UpdatePhase::Rebooting, UpdatePhase::Checking},
        {UpdatePhase::Rebooting, UpdatePhase::NeedsAction},
        {UpdatePhase::Checking, UpdatePhase::Failed},
        {UpdatePhase::Checking, UpdatePhase::NeedsAction},
        {UpdatePhase::NeedsRelease, UpdatePhase::Waiting},
        {UpdatePhase::NeedsRelease, UpdatePhase::Cancelled},
        {UpdatePhase::NeedsAction, UpdatePhase::Checking},
        {UpdatePhase::NeedsAction, UpdatePhase::Cancelled},
    };
    for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        UpdateQueue q;
        UpdateJob *job = job_at_phase(q, edges[i].from, (uint16_t)(0x2000 + i));
        TEST_ASSERT_NOT_NULL(job);
        TEST_ASSERT_TRUE(update_transition(*job, edges[i].to, 20));
    }
}

void test_needs_action_cannot_cancel_when_trial_is_uncertain() {
    UpdateQueue q;
    UpdateJob *job = job_at_phase(q, UpdatePhase::Rebooting);
    TEST_ASSERT_NOT_NULL(job);
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::NeedsAction, 15));
    TEST_ASSERT_FALSE(update_transition(*job, UpdatePhase::Cancelled, 16));
    TEST_ASSERT_TRUE(update_cancel(*job, 16));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Checking, (int)job->phase());
}

void test_policy_helpers_reject_timestamp_regression() {
    UpdateQueue q;
    UpdateJob *queued = enqueue(q, 0x4556, 10);
    TEST_ASSERT_FALSE(update_transition(*queued, UpdatePhase::Waiting, 9));
    TEST_ASSERT_FALSE(update_cancel(*queued, 9));

    UpdateJob *uploading = job_at_phase(q, UpdatePhase::Uploading, 0x4557);
    TEST_ASSERT_FALSE(update_progress(*uploading, 1, 9));

    UpdateJob *checking = job_at_phase(q, UpdatePhase::Checking, 0x4558);
    TEST_ASSERT_FALSE(update_complete(*checking, checking->release().image_hash,
                                      true, 9));
}

void test_terminal_states_are_immutable() {
    UpdateQueue q;
    UpdateJob *job = enqueue(q);
    TEST_ASSERT_TRUE(update_cancel(*job, 11));
    TEST_ASSERT_TRUE(update_terminal(job->phase()));
    TEST_ASSERT_FALSE(update_transition(*job, UpdatePhase::Waiting, 12));
    TEST_ASSERT_FALSE(update_cancel(*job, 12));
}

void test_progress_is_uploading_only_bounded_and_can_rewind() {
    UpdateQueue q;
    UpdateJob *job = enqueue(q);
    TEST_ASSERT_FALSE(update_progress(*job, 0, 11));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Waiting, 11));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Connecting, 12));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Uploading, 13));
    TEST_ASSERT_FALSE(update_progress(*job, std::numeric_limits<uint32_t>::max(), 14));
    TEST_ASSERT_TRUE(update_progress(*job, 32, 14));
    TEST_ASSERT_TRUE(update_progress(*job, 4, 15));
    TEST_ASSERT_EQUAL_UINT32(4, job->acknowledged());
    TEST_ASSERT_FALSE(update_progress(*job, 5, 14));
}

void test_cancellation_before_trial_is_cancelled() {
    UpdateQueue q;
    UpdateJob *job = enqueue(q);
    TEST_ASSERT_TRUE(update_cancel(*job, 11));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Cancelled, (int)job->phase());
    TEST_ASSERT_FALSE(job->trial_may_be_armed());
}

void test_cancellation_after_trial_returns_to_checking_without_claiming_undo() {
    UpdateQueue q;
    UpdateJob *job = enqueue(q);
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Waiting, 11));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Connecting, 12));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Uploading, 13));
    TEST_ASSERT_TRUE(update_transition(*job, UpdatePhase::Rebooting, 14));

    TEST_ASSERT_TRUE(update_cancel(*job, 15));
    TEST_ASSERT_EQUAL_INT((int)UpdatePhase::Checking, (int)job->phase());
    TEST_ASSERT_TRUE(job->trial_may_be_armed());
}

void test_queue_rejects_duplicate_live_target_and_accepts_distinct_tags() {
    UpdateQueue q;
    UpdateJob *first = enqueue(q, 0x4556);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL(q.enqueue(target(0x4556), release(), 11));
    UpdateJob *second = enqueue(q, 0x4557, 12);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_UINT32(2, q.size());
    TEST_ASSERT_EQUAL_PTR(first, q.find(first->id()));
    TEST_ASSERT_EQUAL_PTR(second, q.at(1));
    TEST_ASSERT_NULL(q.at(2));
}

void test_full_live_queue_rejects_without_evicting_a_live_job() {
    UpdateQueue q;
    UpdateJob *first = nullptr;
    for (uint16_t i = 0; i < UPDATE_QUEUE_MAX; ++i) {
        UpdateJob *job = enqueue(q, (uint16_t)(0x1000 + i), i + 1);
        TEST_ASSERT_NOT_NULL(job);
        if (i == 0) first = job;
    }
    TEST_ASSERT_NULL(enqueue(q, 0x2000, 100));
    TEST_ASSERT_EQUAL_UINT32(UPDATE_QUEUE_MAX, q.size());
    TEST_ASSERT_EQUAL_UINT32(1, first->id());
}

void test_full_queue_reuses_its_oldest_terminal_slot_with_a_new_id() {
    UpdateQueue q;
    UpdateJob *oldest = nullptr;
    UpdateJob *newer = nullptr;
    for (uint16_t i = 0; i < UPDATE_QUEUE_MAX; ++i) {
        UpdateJob *job = enqueue(q, (uint16_t)(0x1000 + i), i + 1);
        TEST_ASSERT_NOT_NULL(job);
        if (i == 0) oldest = job;
        if (i == 1) newer = job;
    }
    TEST_ASSERT_TRUE(update_cancel(*oldest, 100));
    TEST_ASSERT_TRUE(update_cancel(*newer, 200));
    UpdateJob *replacement = enqueue(q, 0x2000, 101);
    TEST_ASSERT_EQUAL_PTR(oldest, replacement);
    TEST_ASSERT_EQUAL_UINT32(17, replacement->id());
    TEST_ASSERT_NULL(q.find(1));
}

void test_queue_never_wraps_job_ids() {
    UpdateQueue q(std::numeric_limits<uint32_t>::max());
    UpdateJob *last = enqueue(q);
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<uint32_t>::max(), last->id());
    TEST_ASSERT_NULL(enqueue(q, 0x4557, 11));
}

void test_queue_rejects_malformed_targets_and_releases() {
    UpdateQueue q;
    UpdateTarget bad_tag = target();
    bad_tag.tag = 0;
    TEST_ASSERT_NULL(q.enqueue(bad_tag, release(), 1));

    bad_tag.tag = 0xffff;
    TEST_ASSERT_NULL(q.enqueue(bad_tag, release(), 2));

    UpdateTarget unterminated = target();
    memset(unterminated.hardware_id, 'a', sizeof(unterminated.hardware_id));
    TEST_ASSERT_NULL(q.enqueue(unterminated, release(), 2));

    UpdateTarget non_hex = target();
    non_hex.hardware_id[15] = 'g';
    TEST_ASSERT_NULL(q.enqueue(non_hex, release(), 4));

    UpdateTarget uppercase = target();
    uppercase.hardware_id[0] = 'A';
    TEST_ASSERT_NULL(q.enqueue(uppercase, release(), 5));

    TEST_ASSERT_NULL(q.enqueue(target(), release(31), 6));
    TEST_ASSERT_NULL(q.enqueue(target(), release(212993), 7));

    UpdateRelease bad_version = release();
    memset(bad_version.version, 'x', sizeof(bad_version.version));
    TEST_ASSERT_NULL(q.enqueue(target(), bad_version, 8));
}

void test_queue_accepts_the_documented_tag_boundaries_and_canonical_id() {
    UpdateQueue q;
    UpdateTarget first = target(1);
    first.hardware_id[0] = 'a';
    UpdateTarget last = target(0xfffe);
    last.hardware_id[0] = 'b';
    TEST_ASSERT_NOT_NULL(q.enqueue(first, release(), 1));
    TEST_ASSERT_NOT_NULL(q.enqueue(last, release(), 2));
}

void test_queue_copies_enqueue_inputs_before_callers_can_change_them() {
    UpdateQueue q;
    UpdateTarget wanted = target();
    UpdateRelease wanted_release = release();
    UpdateJob *job = q.enqueue(wanted, wanted_release, 1);
    TEST_ASSERT_NOT_NULL(job);

    wanted.tag = 1;
    wanted.hardware_id[0] = 'a';
    wanted_release.size = 32;
    wanted_release.image_hash[0] = 0xff;
    strcpy(wanted_release.version, "changed");

    TEST_ASSERT_EQUAL_UINT16(0x4556, job->target().tag);
    TEST_ASSERT_EQUAL_STRING("0011223344556677", job->target().hardware_id);
    TEST_ASSERT_EQUAL_UINT32(64, job->release().size);
    TEST_ASSERT_EQUAL_HEX8(0x22, job->release().image_hash[0]);
    TEST_ASSERT_EQUAL_STRING("1.2.3", job->release().version);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_uploaded_bytes_equal_size_is_not_success);
    RUN_TEST(test_inspection_retry_preserves_acknowledged_bytes_but_cannot_resume_writes);
    RUN_TEST(test_pretrial_retry_discards_the_old_receiver_offset);
    RUN_TEST(test_exact_active_hash_and_confirmation_completes_checking_job);
    RUN_TEST(test_same_version_with_another_hash_does_not_complete);
    RUN_TEST(test_restart_does_not_resume_a_stale_offset);
    RUN_TEST(test_restart_maps_pre_transfer_states_to_needs_release);
    RUN_TEST(test_restart_preserves_terminal_states);
    RUN_TEST(test_transition_accepts_only_the_approved_policy_edges);
    RUN_TEST(test_transition_table_allows_each_documented_edge);
    RUN_TEST(test_needs_action_cannot_cancel_when_trial_is_uncertain);
    RUN_TEST(test_policy_helpers_reject_timestamp_regression);
    RUN_TEST(test_terminal_states_are_immutable);
    RUN_TEST(test_progress_is_uploading_only_bounded_and_can_rewind);
    RUN_TEST(test_cancellation_before_trial_is_cancelled);
    RUN_TEST(test_cancellation_after_trial_returns_to_checking_without_claiming_undo);
    RUN_TEST(test_queue_rejects_duplicate_live_target_and_accepts_distinct_tags);
    RUN_TEST(test_full_live_queue_rejects_without_evicting_a_live_job);
    RUN_TEST(test_full_queue_reuses_its_oldest_terminal_slot_with_a_new_id);
    RUN_TEST(test_queue_never_wraps_job_ids);
    RUN_TEST(test_queue_rejects_malformed_targets_and_releases);
    RUN_TEST(test_queue_accepts_the_documented_tag_boundaries_and_canonical_id);
    RUN_TEST(test_queue_copies_enqueue_inputs_before_callers_can_change_them);
    return UNITY_END();
}
