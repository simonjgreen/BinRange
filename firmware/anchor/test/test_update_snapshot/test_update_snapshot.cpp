#include <unity.h>
#include <cstring>
#include "core/update_job.h"
#include "core/update_snapshot.h"

void setUp() {}
void tearDown() {}

static UpdateJobState saved(UpdatePhase phase = UpdatePhase::Uploading,
                            uint32_t id = 41) {
    UpdateJobState s = {};
    s.id = id;
    s.target = {0xb100, "f00dbaad12345678"};
    s.release.size = 64;
    memset(s.release.file_sha, 0x11, 32);
    memset(s.release.image_hash, 0x22, 32);
    strcpy(s.release.version, "1.2.3");
    s.phase = phase;
    s.acknowledged = 32;
    s.attempts = 3;
    strcpy(s.error, "receiver unavailable");
    s.trial_may_be_armed = phase == UpdatePhase::Rebooting;
    return s;
}

void test_restore_applies_every_restart_class_and_new_boot_epoch() {
    const UpdatePhase expected[] = {
        UpdatePhase::NeedsRelease, UpdatePhase::NeedsRelease,
        UpdatePhase::NeedsRelease, UpdatePhase::NeedsRelease,
        UpdatePhase::Checking, UpdatePhase::Checking,
        UpdatePhase::Successful, UpdatePhase::Failed, UpdatePhase::Cancelled,
        UpdatePhase::NeedsRelease, UpdatePhase::NeedsAction
    };
    for (unsigned i = 0; i < 11; ++i) {
        UpdateQueue q;
        UpdateJobState s = saved(static_cast<UpdatePhase>(i));
        TEST_ASSERT_NOT_NULL(q.enqueue(s.target, s.release, 999999));
        TEST_ASSERT_TRUE(q.restore(&s, 1, 2));
        TEST_ASSERT_EQUAL_UINT32(1, q.size());
        const UpdateJob *job = q.find(41);
        TEST_ASSERT_NOT_NULL(job);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected[i]), static_cast<int>(job->phase()));
        TEST_ASSERT_EQUAL_UINT64(2, job->changed_ms());
        TEST_ASSERT_EQUAL_UINT32(update_terminal(s.phase) ? 32 : 0, job->acknowledged());
        TEST_ASSERT_EQUAL_UINT8(3, job->attempts());
        TEST_ASSERT_EQUAL_STRING(s.error, job->error());
        TEST_ASSERT_EQUAL(s.trial_may_be_armed, job->trial_may_be_armed());
        UpdateJobState exported = update_export_state(*job);
        TEST_ASSERT_EQUAL_UINT32(41, exported.id);
        TEST_ASSERT_EQUAL_STRING(s.target.hardware_id, exported.target.hardware_id);
        TEST_ASSERT_EQUAL_MEMORY(s.release.image_hash, exported.release.image_hash, 32);
        TEST_ASSERT_EQUAL_STRING(s.release.version, exported.release.version);
    }
}

void test_empty_snapshot_has_literal_v1_wire_bytes_and_crc_vector() {
    const uint8_t vector[] = {'1','2','3','4','5','6','7','8','9'};
    TEST_ASSERT_EQUAL_HEX32(0xcbf43926, update_snapshot_crc32(vector, 9));
    const uint8_t golden[] = {
        0x55,0x53,0x4e,0x50, 1,0, 24,0, 24,0,0,0,
        0,0,0,0, 0,0,0,0, 0x83,0xa7,0x5d,0x14
    };
    UpdateSnapshot snapshot = {};
    uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    size_t written = 99;
    TEST_ASSERT_TRUE(update_snapshot_encode(snapshot, bytes, sizeof(bytes), written));
    TEST_ASSERT_EQUAL_UINT32(sizeof(golden), written);
    TEST_ASSERT_EQUAL_MEMORY(golden, bytes, sizeof(golden));
    snapshot.restart_count = 123;
    TEST_ASSERT_TRUE(update_snapshot_decode(golden, sizeof(golden), snapshot));
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.restart_count);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.association_count);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.job_count);
}

static UpdateSnapshot filled() {
    UpdateSnapshot s = {};
    s.restart_count = 0x12345678;
    s.association_count = 1;
    s.job_count = 1;
    s.jobs[0] = saved();
    s.associations[0].target = s.jobs[0].target;
    const uint8_t address[] = {0xc0, 0x00, 0x00, 0x00, 0x00, 0x01};
    memcpy(s.associations[0].address, address, 6);
    s.associations[0].address_type = 1;
    s.last_wall_seconds[0] = 0x0102030405060708ULL;
    return s;
}

void test_filled_wire_preserves_identity_release_and_wall_time() {
    UpdateSnapshot input = filled();
    uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    size_t written = 0;
    TEST_ASSERT_TRUE(update_snapshot_encode(input, bytes, sizeof(bytes), written));
    TEST_ASSERT_EQUAL_UINT32(262, written);
    const uint8_t header[] = {
        0x55,0x53,0x4e,0x50, 1,0,24,0, 6,1,0,0,
        0x78,0x56,0x34,0x12, 1,1,0,0
    };
    const uint8_t association[] = {
        0,0xb1, 'f','0','0','d','b','a','a','d','1','2','3','4','5','6','7','8',
        0xc0,0x00,0x00,0x00,0x00,0x01, 1,0
    };
    const uint8_t job_prefix[] = {
        41,0,0,0, 0,0xb1, 'f','0','0','d','b','a','a','d','1','2','3','4','5','6','7','8',
        3,3,0,0,0,0, 32,0,0,0, 64,0,0,0
    };
    const uint8_t epoch[] = {8,7,6,5,4,3,2,1};
    TEST_ASSERT_EQUAL_MEMORY(header, bytes, sizeof(header));
    TEST_ASSERT_EQUAL_MEMORY(association, bytes + 24, sizeof(association));
    TEST_ASSERT_EQUAL_MEMORY(job_prefix, bytes + 50, sizeof(job_prefix));
    TEST_ASSERT_EQUAL_MEMORY(epoch, bytes + 254, 8);
    TEST_ASSERT_EQUAL_STRING("1.2.3", bytes + 150);
    TEST_ASSERT_EQUAL_STRING("receiver unavailable", bytes + 174);
    UpdateSnapshot output = {};
    TEST_ASSERT_TRUE(update_snapshot_decode(bytes, written, output));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, output.restart_count);
    TEST_ASSERT_EQUAL_UINT32(1, output.association_count);
    TEST_ASSERT_EQUAL_UINT32(1, output.job_count);
    TEST_ASSERT_EQUAL_MEMORY(association + 18, output.associations[0].address, 6);
    TEST_ASSERT_EQUAL_UINT8(1, output.associations[0].address_type);
    TEST_ASSERT_EQUAL_UINT64(input.last_wall_seconds[0], output.last_wall_seconds[0]);
    const UpdateJobState &job = output.jobs[0];
    TEST_ASSERT_EQUAL_UINT32(41, job.id);
    TEST_ASSERT_EQUAL_HEX16(0xb100, job.target.tag);
    TEST_ASSERT_EQUAL_STRING("f00dbaad12345678", job.target.hardware_id);
    TEST_ASSERT_EQUAL_UINT32(64, job.release.size);
    TEST_ASSERT_EQUAL_MEMORY(input.jobs[0].release.file_sha, job.release.file_sha, 32);
    TEST_ASSERT_EQUAL_MEMORY(input.jobs[0].release.image_hash, job.release.image_hash, 32);
    TEST_ASSERT_EQUAL_STRING("1.2.3", job.release.version);
    TEST_ASSERT_EQUAL_STRING("receiver unavailable", job.error);
    TEST_ASSERT_EQUAL_UINT32(32, job.acknowledged);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdatePhase::Uploading), static_cast<int>(job.phase));
    UpdateQueue queue;
    TEST_ASSERT_TRUE(queue.restore(output.jobs, output.job_count, 1));
    TEST_ASSERT_EQUAL_UINT32(0, queue.find(41)->acknowledged());
}

static void assert_state(const UpdateJobState &expected, const UpdateJob &actual) {
    TEST_ASSERT_EQUAL_UINT32(expected.id, actual.id());
    TEST_ASSERT_EQUAL_UINT16(expected.target.tag, actual.target().tag);
    TEST_ASSERT_EQUAL_STRING(expected.target.hardware_id, actual.target().hardware_id);
    TEST_ASSERT_EQUAL_UINT32(expected.release.size, actual.release().size);
    TEST_ASSERT_EQUAL_MEMORY(expected.release.file_sha, actual.release().file_sha, 32);
    TEST_ASSERT_EQUAL_MEMORY(expected.release.image_hash, actual.release().image_hash, 32);
    TEST_ASSERT_EQUAL_STRING(expected.release.version, actual.release().version);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.phase), static_cast<int>(actual.phase()));
    TEST_ASSERT_EQUAL_UINT32(expected.acknowledged, actual.acknowledged());
    TEST_ASSERT_EQUAL_UINT8(expected.attempts, actual.attempts());
    TEST_ASSERT_EQUAL_STRING(expected.error, actual.error());
    TEST_ASSERT_EQUAL(expected.trial_may_be_armed, actual.trial_may_be_armed());
}

void test_restore_rejects_bad_records_atomically_including_allocator() {
    for (unsigned bad = 0; bad < 18; ++bad) {
        UpdateQueue q(7);
        UpdateJobState original = saved();
        UpdateJob *old = q.enqueue(original.target, original.release, 90000);
        TEST_ASSERT_NOT_NULL(old);
        original = update_export_state(*old);
        UpdateJobState states[2] = {saved(UpdatePhase::Failed, 55), saved()};
        UpdateJobState &s = states[1];
        switch (bad) {
            case 0: s.id = 0; break;
            case 1: s.target.tag = 0; break;
            case 2: s.target.tag = 0xffff; break;
            case 3: s.target.hardware_id[0] = 'A'; break;
            case 4: s.target.hardware_id[4] = 0; break;
            case 5: s.target.hardware_id[16] = 'x'; break;
            case 6: s.release.size = 31; break;
            case 7: s.release.size = 212993; break;
            case 8: s.release.version[0] = 0; break;
            case 9: memset(s.release.version, 'v', 24); break;
            case 10: s.phase = static_cast<UpdatePhase>(255); break;
            case 11: s.acknowledged = 65; break;
            case 12: s.attempts = 4; break;
            case 13: memset(s.error, 'e', 80); break;
            case 14: s.id = states[0].id; break;
            case 15: states[0].phase = UpdatePhase::Checking; break;
            case 16:
                states[0].phase = UpdatePhase::Checking;
                states[0].target.hardware_id[0] = '0';
                break;
            case 17: s.trial_may_be_armed = true; break;
        }
        TEST_ASSERT_FALSE(UpdateQueue::valid_restore(states, 2));
        TEST_ASSERT_FALSE(q.restore(states, 2, 1));
        TEST_ASSERT_EQUAL_UINT32(1, q.size());
        TEST_ASSERT_EQUAL_PTR(old, q.find(7));
        assert_state(original, *old);
        TEST_ASSERT_EQUAL_UINT64(90000, old->changed_ms());
        UpdateTarget next = original.target;
        ++next.tag;
        UpdateJob *added = q.enqueue(next, original.release, 90001);
        TEST_ASSERT_NOT_NULL(added);
        TEST_ASSERT_EQUAL_UINT32(8, added->id());
    }
}

void test_restore_trial_matrix_keeps_uncertain_jobs_inspection_only() {
    for (unsigned phase = 0; phase < 11; ++phase) {
        for (unsigned trial = 0; trial < 2; ++trial) {
            UpdateJobState s = saved(static_cast<UpdatePhase>(phase));
            s.trial_may_be_armed = trial;
            const bool valid = trial ? (phase == 4 || phase == 5 || phase == 6 ||
                                       phase == 7 || phase == 10) : phase != 4;
            UpdateQueue q;
            TEST_ASSERT_EQUAL(valid, q.restore(&s, 1, 0));
            if (!valid) continue;
            UpdateJob *j = q.find(41);
            TEST_ASSERT_NOT_NULL(j);
            TEST_ASSERT_EQUAL(trial != 0, j->trial_may_be_armed());
            if (trial && !update_terminal(j->phase())) {
                TEST_ASSERT_FALSE(update_transition(*j, UpdatePhase::Waiting, 1));
                TEST_ASSERT_FALSE(update_transition(*j, UpdatePhase::Uploading, 1));
                TEST_ASSERT_FALSE(update_transition(*j, UpdatePhase::Cancelled, 1));
                TEST_ASSERT_TRUE(update_cancel(*j, 1));
                TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdatePhase::Checking), static_cast<int>(j->phase()));
                TEST_ASSERT_TRUE(update_complete(*j, j->release().image_hash, true, 2));
            }
        }
    }
}

void test_restore_ids_are_preserved_unsorted_and_never_wrap() {
    UpdateJobState states[] = {saved(UpdatePhase::Failed, 90), saved(UpdatePhase::Failed, 3)};
    UpdateQueue q;
    TEST_ASSERT_TRUE(q.restore(states, 2, 0));
    TEST_ASSERT_EQUAL_UINT32(90, q.at(0)->id());
    TEST_ASSERT_EQUAL_UINT32(3, q.at(1)->id());
    TEST_ASSERT_EQUAL_UINT32(91, q.enqueue(states[0].target, states[0].release, 1)->id());
    states[0].id = UINT32_MAX - 1;
    TEST_ASSERT_TRUE(q.restore(states, 2, 0));
    UpdateJob *last = q.enqueue(states[0].target, states[0].release, 1);
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, last->id());
    TEST_ASSERT_TRUE(update_cancel(*last, 2));
    TEST_ASSERT_NULL(q.enqueue(states[0].target, states[0].release, 3));
    states[0].id = UINT32_MAX;
    TEST_ASSERT_TRUE(q.restore(states, 2, 0));
    TEST_ASSERT_NULL(q.enqueue(states[0].target, states[0].release, 1));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, q.find(UINT32_MAX)->id());
}

void test_restore_capacity_empty_and_detached_export() {
    UpdateJobState states[17];
    for (unsigned i = 0; i < 17; ++i) states[i] = saved(UpdatePhase::Failed, i + 1);
    UpdateQueue q;
    TEST_ASSERT_TRUE(q.restore(states, 16, 0));
    TEST_ASSERT_EQUAL_UINT32(16, q.size());
    TEST_ASSERT_FALSE(q.restore(states, 17, 1));
    TEST_ASSERT_FALSE(q.restore(states, SIZE_MAX, 1));
    TEST_ASSERT_FALSE(q.restore(nullptr, 1, 1));
    TEST_ASSERT_EQUAL_UINT32(16, q.size());
    states[0].target.tag = 1;
    UpdateJobState exported = update_export_state(*q.at(0));
    exported.target.tag = 2;
    exported.release.size = 100;
    exported.phase = UpdatePhase::Queued;
    TEST_ASSERT_EQUAL_HEX16(0xb100, q.at(0)->target().tag);
    TEST_ASSERT_EQUAL_UINT32(64, q.at(0)->release().size);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdatePhase::Failed), static_cast<int>(q.at(0)->phase()));
    TEST_ASSERT_TRUE(q.restore(nullptr, 0, 1));
    TEST_ASSERT_EQUAL_UINT32(0, q.size());
    TEST_ASSERT_NULL(q.at(0));
    TEST_ASSERT_NULL(q.find(1));
    TEST_ASSERT_EQUAL_UINT32(1, q.enqueue(exported.target, exported.release, 2)->id());
}

static void reject_encode(const UpdateSnapshot &s, size_t capacity = UPDATE_SNAPSHOT_MAX_ENCODED_SIZE) {
    uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    uint8_t before[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    memset(bytes, 0xa5, sizeof(bytes));
    memcpy(before, bytes, sizeof(bytes));
    size_t written = 123;
    TEST_ASSERT_FALSE(update_snapshot_encode(s, bytes, capacity, written));
    TEST_ASSERT_EQUAL_UINT32(0, written);
    TEST_ASSERT_EQUAL_MEMORY(before, bytes, sizeof(bytes));
}

static size_t encode(const UpdateSnapshot &s, uint8_t *bytes) {
    size_t written = 0;
    TEST_ASSERT_TRUE(update_snapshot_encode(s, bytes, UPDATE_SNAPSHOT_MAX_ENCODED_SIZE, written));
    return written;
}

static void reject_decode(const uint8_t *bytes, size_t size) {
    UpdateSnapshot output = filled();
    uint8_t before[sizeof(output)];
    memcpy(before, &output, sizeof(output));
    TEST_ASSERT_FALSE(update_snapshot_decode(bytes, size, output));
    TEST_ASSERT_EQUAL_MEMORY(before, &output, sizeof(output));
}

// Test-side CRC assembly deliberately concatenates protected regions using
// the public CRC primitive, unlike the encoder's skipped-field traversal.
static void repair_crc(uint8_t *bytes, size_t size) {
    uint8_t protected_bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    memcpy(protected_bytes, bytes, 20);
    memcpy(protected_bytes + 20, bytes + 24, size - 24);
    const uint32_t crc = update_snapshot_crc32(protected_bytes, size - 4);
    for (unsigned i = 0; i < 4; ++i) bytes[20 + i] = static_cast<uint8_t>(crc >> (8 * i));
}

void test_snapshot_capacity_and_length_failures_leave_outputs_untouched() {
    UpdateSnapshot s = filled();
    reject_encode(s, 0);
    reject_encode(s, 261);
    size_t written = 123;
    TEST_ASSERT_FALSE(update_snapshot_encode(s, nullptr, SIZE_MAX, written));
    TEST_ASSERT_EQUAL_UINT32(0, written);
    uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE + 1];
    const size_t size = encode(s, bytes);
    for (size_t i = 0; i < size; ++i) reject_decode(bytes, i);
    bytes[size] = 0;
    reject_decode(bytes, size + 1);
    reject_decode(bytes, UPDATE_SNAPSHOT_MAX_ENCODED_SIZE + 1);
    reject_decode(bytes, SIZE_MAX);
    reject_decode(nullptr, 0);
    reject_decode(nullptr, size);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] ^= 0x01;
        reject_decode(bytes, size);
        bytes[i] ^= 0x01;
    }
}

void test_snapshot_semantic_wire_errors_are_rejected_even_with_valid_crc() {
    // Offsets are literal v1 positions, independent of implementation constants.
    const struct {size_t offset; uint8_t value;} cases[] = {
        {0,'X'}, {4,2}, {5,1}, {6,25}, {7,1}, {8,0xff}, {11,0xff},
        {16,17}, {17,17}, {16,0}, {17,0}, {18,1}, {19,1},
        {26,'G'}, {27,'A'}, {28,0}, {48,2}, {42,0x91}, {49,1},
        {50,0}, {56,'A'}, {57,0}, {72,11}, {72,255}, {73,4}, {74,2},
        {74,1}, {72,4}, {75,1}, {76,1}, {77,1}, {78,65},
        {82,31}, {85,1}, {150,0}, {173,'v'}, {156,'v'}, {253,'e'},
        {196,'x'}, {54,1}, {56,'0'}
    };
    for (const auto &c : cases) {
        uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
        const size_t size = encode(filled(), bytes);
        bytes[c.offset] = c.value;
        repair_crc(bytes, size);
        reject_decode(bytes, size);
    }
    for (unsigned bad = 0; bad < 6; ++bad) {
        uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
        const size_t size = encode(filled(), bytes);
        switch (bad) {
            case 0: bytes[24] = bytes[25] = 0; break;
            case 1: bytes[24] = bytes[25] = 0xff; break;
            case 2: memset(bytes + 42, 0, 6); bytes[48] = 0; break;
            case 3: memset(bytes + 42, 0xff, 6); break;
            case 4: memset(bytes + 150, 'v', 24); break;
            case 5: memset(bytes + 174, 'e', 80); break;
        }
        repair_crc(bytes, size);
        reject_decode(bytes, size);
    }
}

void test_snapshot_encoder_validates_fields_before_any_output() {
    for (unsigned bad = 0; bad < 27; ++bad) {
        UpdateSnapshot s = filled();
        switch (bad) {
            case 0: s.association_count = 17; break;
            case 1: s.job_count = 17; break;
            case 2: s.association_count = SIZE_MAX; break;
            case 3: s.job_count = SIZE_MAX; break;
            case 4: s.associations[0].target.tag = 0; break;
            case 5: s.associations[0].target.tag = 0xffff; break;
            case 6: s.associations[0].target.hardware_id[0] = 'A'; break;
            case 7: s.associations[0].target.hardware_id[4] = 0; break;
            case 8: s.associations[0].target.hardware_id[16] = 'x'; break;
            case 9: s.associations[0].address_type = 2; break;
            case 10: s.associations[0].address[0] = 0x81; break;
            case 11: memset(s.associations[0].address, 0, 6); s.associations[0].address_type = 0; break;
            case 12: memset(s.associations[0].address, 0xff, 6); break;
            case 13: s.jobs[0].id = 0; break;
            case 14: s.jobs[0].phase = static_cast<UpdatePhase>(255); break;
            case 15: s.jobs[0].attempts = 4; break;
            case 16: s.jobs[0].acknowledged = 65; break;
            case 17: s.jobs[0].release.size = 31; break;
            case 18: s.jobs[0].release.size = 212993; break;
            case 19: memset(s.jobs[0].release.version, 'v', 24); break;
            case 20: s.jobs[0].release.version[0] = 0; break;
            case 21: memset(s.jobs[0].error, 'e', 80); break;
            case 22: s.jobs[0].trial_may_be_armed = true; break;
            case 23: s.jobs[0].phase = UpdatePhase::Rebooting; break;
            case 24: s.association_count = 0; break;
            case 25: s.jobs[0].target.tag = 1; break;
            case 26: s.jobs[0].target.hardware_id[0] = '0'; break;
        }
        reject_encode(s);
    }
}

static UpdateSnapshot full_snapshot() {
    UpdateSnapshot s = filled();
    s.association_count = 16;
    s.job_count = 16;
    for (unsigned i = 0; i < 16; ++i) {
        s.jobs[i] = saved(UpdatePhase::Checking, 16 - i);
        s.jobs[i].target.tag = static_cast<uint16_t>(0xb100 + i);
        s.jobs[i].target.hardware_id[15] = "0123456789abcdef"[i];
        s.associations[i] = s.associations[0];
        s.associations[i].target = s.jobs[i].target;
        s.associations[i].address[5] = static_cast<uint8_t>(i);
        s.last_wall_seconds[i] = i;
    }
    return s;
}

void test_full_snapshot_16_records_and_duplicate_identity_dimensions() {
    UpdateSnapshot s = full_snapshot();
    uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    const size_t size = encode(s, bytes);
    TEST_ASSERT_EQUAL_UINT32(3832, size);
    UpdateSnapshot decoded = {};
    TEST_ASSERT_TRUE(update_snapshot_decode(bytes, size, decoded));
    TEST_ASSERT_EQUAL_UINT32(16, decoded.association_count);
    TEST_ASSERT_EQUAL_UINT32(16, decoded.job_count);
    UpdateQueue q;
    TEST_ASSERT_TRUE(q.restore(decoded.jobs, decoded.job_count, 9));
    TEST_ASSERT_EQUAL_UINT32(16, q.size());
    for (unsigned i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT32(16 - i, q.at(i)->id());
        TEST_ASSERT_EQUAL_UINT64(i, decoded.last_wall_seconds[i]);
        TEST_ASSERT_EQUAL_UINT32(0, q.at(i)->acknowledged());
    }
    TEST_ASSERT_NULL(q.enqueue(saved().target, saved().release, 10));
    for (unsigned bad = 0; bad < 5; ++bad) {
        s = full_snapshot();
        encode(s, bytes);
        switch (bad) {
            case 0: // Duplicate tag, but different hardware.
                s.associations[1].target.tag = s.associations[0].target.tag;
                memcpy(bytes + 50, bytes + 24, 2);
                break;
            case 1: // Duplicate hardware ID, but different tag.
                memcpy(s.associations[1].target.hardware_id, s.associations[0].target.hardware_id, 17);
                memcpy(bytes + 52, bytes + 26, 16);
                break;
            case 2: // Duplicate BLE tuple.
                memcpy(s.associations[1].address, s.associations[0].address, 6);
                memcpy(bytes + 68, bytes + 42, 6);
                break;
            case 3: // Duplicate job ID.
                s.jobs[1].id = s.jobs[0].id;
                memcpy(bytes + 652, bytes + 440, 4);
                break;
            case 4: // Duplicate live job target with distinct IDs.
                s.jobs[1].target = s.jobs[0].target;
                memcpy(bytes + 656, bytes + 444, 18);
                break;
        }
        reject_encode(s);
        repair_crc(bytes, size);
        reject_decode(bytes, size);
    }
    // The identity key includes type: the same bytes in the public and random
    // namespaces are distinct (each target/hardware ID remains unique).
    s = full_snapshot();
    memcpy(s.associations[1].address, s.associations[0].address, 6);
    s.associations[1].address_type = 0;
    TEST_ASSERT_TRUE(update_snapshot_decode(bytes, encode(s, bytes), decoded));
}

void test_terminal_history_needs_no_current_association_and_strings_are_canonical() {
    const UpdatePhase terminal[] = {UpdatePhase::Successful, UpdatePhase::Failed, UpdatePhase::Cancelled};
    for (UpdatePhase phase : terminal) {
        UpdateSnapshot s = filled();
        s.association_count = 0;
        s.jobs[0] = saved(phase);
        s.jobs[0].release.version[20] = 'x';
        s.jobs[0].error[70] = 'x';
        s.last_wall_seconds[0] = UINT64_MAX;
        uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
        const size_t size = encode(s, bytes);
        TEST_ASSERT_EQUAL_UINT8(0, bytes[24 + 120]);
        TEST_ASSERT_EQUAL_UINT8(0, bytes[24 + 194]);
        UpdateSnapshot out = {};
        TEST_ASSERT_TRUE(update_snapshot_decode(bytes, size, out));
        TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, out.last_wall_seconds[0]);
        TEST_ASSERT_EQUAL_STRING("receiver unavailable", out.jobs[0].error);
        UpdateQueue q;
        TEST_ASSERT_TRUE(q.restore(out.jobs, out.job_count, 0));
        TEST_ASSERT_FALSE(update_cancel(*q.find(41), 1));
    }
    UpdateSnapshot s = filled();
    s.job_count = 2;
    s.jobs[1] = saved(UpdatePhase::Successful, 99);
    s.jobs[1].target.hardware_id[0] = '0'; // Historical hardware replacement.
    s.last_wall_seconds[1] = 0;
    uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    UpdateSnapshot decoded = {};
    TEST_ASSERT_TRUE(update_snapshot_decode(bytes, encode(s, bytes), decoded));
    s.jobs[1].phase = UpdatePhase::Checking;
    reject_encode(s);
}

void test_valid_boundaries_include_long_strings_public_addresses_and_max_release() {
    UpdateSnapshot s = full_snapshot();
    s.associations[0].target.tag = s.jobs[0].target.tag = 1;
    s.associations[1].target.tag = s.jobs[1].target.tag = 0xfffe;
    s.associations[0].address_type = 0;
    s.associations[0].address[0] = 1;
    s.jobs[0].release.size = s.jobs[0].acknowledged = 32;
    s.jobs[1].release.size = s.jobs[1].acknowledged = 212992;
    s.jobs[0].attempts = 0;
    s.jobs[0].error[0] = 0;
    memset(s.jobs[1].error, 'e', 79);
    s.jobs[1].error[79] = 0;
    memset(s.jobs[1].release.version, 'v', 23);
    s.jobs[1].release.version[23] = 0;
    uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    UpdateSnapshot decoded = {};
    TEST_ASSERT_TRUE(update_snapshot_decode(bytes, encode(s, bytes), decoded));
    TEST_ASSERT_EQUAL_STRING(s.jobs[1].error, decoded.jobs[1].error);
    TEST_ASSERT_EQUAL_STRING(s.jobs[1].release.version, decoded.jobs[1].release.version);
    TEST_ASSERT_EQUAL_UINT32(212992, decoded.jobs[1].release.size);
    TEST_ASSERT_EQUAL_STRING("", decoded.jobs[0].error);
}

void test_snapshot_trial_matrix_validates_wire_and_restores_preserved_state() {
    for (unsigned phase = 0; phase < 11; ++phase) {
        for (unsigned trial = 0; trial < 2; ++trial) {
            UpdateSnapshot s = filled();
            uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
            const size_t size = encode(s, bytes);
            s.jobs[0].phase = static_cast<UpdatePhase>(phase);
            s.jobs[0].trial_may_be_armed = trial;
            bytes[72] = static_cast<uint8_t>(phase);
            bytes[74] = static_cast<uint8_t>(trial);
            repair_crc(bytes, size);
            const bool valid = trial ? (phase == 4 || phase == 5 || phase == 6 ||
                                       phase == 7 || phase == 10) : phase != 4;
            if (!valid) {
                reject_encode(s);
                reject_decode(bytes, size);
                continue;
            }
            UpdateSnapshot out = {};
            TEST_ASSERT_TRUE(update_snapshot_decode(bytes, size, out));
            TEST_ASSERT_EQUAL_UINT32(size, encode(s, bytes));
            TEST_ASSERT_EQUAL_UINT64(s.last_wall_seconds[0], out.last_wall_seconds[0]);
            UpdateQueue q;
            TEST_ASSERT_TRUE(q.restore(out.jobs, out.job_count, 0));
            UpdateJobState expected = s.jobs[0];
            expected.phase = update_after_restart(expected.phase);
            if (!update_terminal(expected.phase)) expected.acknowledged = 0;
            assert_state(expected, *q.find(41));
            TEST_ASSERT_EQUAL_UINT64(0, q.find(41)->changed_ms());
        }
    }
}

void test_associations_only_and_exact_output_capacity() {
    UpdateSnapshot s = filled();
    s.job_count = 0;
    s.restart_count = UINT32_MAX;
    uint8_t bytes[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    memset(bytes, 0xa5, sizeof(bytes));
    size_t written = 999;
    TEST_ASSERT_TRUE(update_snapshot_encode(s, bytes, 50, written));
    TEST_ASSERT_EQUAL_UINT32(50, written);
    for (size_t i = written; i < sizeof(bytes); ++i) TEST_ASSERT_EQUAL_HEX8(0xa5, bytes[i]);
    UpdateSnapshot out = {};
    TEST_ASSERT_TRUE(update_snapshot_decode(bytes, written, out));
    TEST_ASSERT_EQUAL_UINT32(1, out.association_count);
    TEST_ASSERT_EQUAL_UINT32(0, out.job_count);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, out.restart_count);
    // Trailing junk with a corrected declared length and CRC still cannot
    // become an ignored record or extension.
    bytes[50] = 0;
    bytes[8] = 51;
    repair_crc(bytes, 51);
    reject_decode(bytes, 51);
    s = {};
    reject_encode(s, 23);
    TEST_ASSERT_TRUE(update_snapshot_encode(s, bytes, 24, written));
    TEST_ASSERT_EQUAL_UINT32(24, written);
    TEST_ASSERT_EQUAL_HEX32(0, update_snapshot_crc32(nullptr, 0));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_restore_applies_every_restart_class_and_new_boot_epoch);
    RUN_TEST(test_empty_snapshot_has_literal_v1_wire_bytes_and_crc_vector);
    RUN_TEST(test_filled_wire_preserves_identity_release_and_wall_time);
    RUN_TEST(test_restore_rejects_bad_records_atomically_including_allocator);
    RUN_TEST(test_restore_trial_matrix_keeps_uncertain_jobs_inspection_only);
    RUN_TEST(test_restore_ids_are_preserved_unsorted_and_never_wrap);
    RUN_TEST(test_restore_capacity_empty_and_detached_export);
    RUN_TEST(test_snapshot_capacity_and_length_failures_leave_outputs_untouched);
    RUN_TEST(test_snapshot_semantic_wire_errors_are_rejected_even_with_valid_crc);
    RUN_TEST(test_snapshot_encoder_validates_fields_before_any_output);
    RUN_TEST(test_full_snapshot_16_records_and_duplicate_identity_dimensions);
    RUN_TEST(test_terminal_history_needs_no_current_association_and_strings_are_canonical);
    RUN_TEST(test_valid_boundaries_include_long_strings_public_addresses_and_max_release);
    RUN_TEST(test_snapshot_trial_matrix_validates_wire_and_restores_preserved_state);
    RUN_TEST(test_associations_only_and_exact_output_capacity);
    return UNITY_END();
}
