#include <unity.h>

#include <cstring>
#include <vector>

#include "core/update_storage.h"

void setUp() {}
void tearDown() {}

namespace {
// Independent wire fixture: 32-byte header + 32-byte payload (signed prefix),
// then 88 bytes of regular TLVs. Never derived from the parser or SHA code.
const uint8_t expected_image[] = {
    0x3d,0xb8,0xf3,0x96,0,0,0,0, 0x20,0,0,0,0x20,0,0,0,
    0,0,0,0,1,2,3,0, 4,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0x07,0x69,0x58,0, 0x10,0,0x20,0,
    0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,
    0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,
    0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,
    0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,0xa5,
    1,0,0x20,0,
    0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,
    0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,
    0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,
    0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,0xb6,
    0x22,0,8,0, 0x30,6,2,1,1,2,1,1
};

struct FakeBackend {
    UpdateStorageRead read = UpdateStorageRead::Missing;
    bool read_ok = true;
    unsigned size_calls = 0;
    unsigned read_calls = 0;
    uint8_t blob[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE] = {};
    size_t blob_size = 0;
    bool write_ok = true;
    bool allocate_ok = false;
    bool external = true;
    size_t allocation_bytes = 212992;
    unsigned fail_hash_call = 0;  // 0 succeeds, 1/2 fails that SHA operation.
    unsigned hash_calls = 0;
    const uint8_t *hash_bytes[2] = {};
    size_t hash_sizes[2] = {};
    bool hash_spans_match[2] = {};
    unsigned write_calls = 0;
};

uint8_t fake_stage[212992];

UpdateStorageRead fake_size(void *raw, size_t &size) {
    FakeBackend &fake = *static_cast<FakeBackend *>(raw);
    ++fake.size_calls;
    size = fake.blob_size;
    return fake.read;
}
UpdateStorageRead fake_read(void *raw, uint8_t *bytes, size_t size) {
    FakeBackend &fake = *static_cast<FakeBackend *>(raw);
    ++fake.read_calls;
    if (fake.read != UpdateStorageRead::Ok || size != fake.blob_size)
        return UpdateStorageRead::Error;
    memcpy(bytes, fake.blob, size);
    // Supply decodable bytes even on error: ignoring the IO result must not
    // accidentally pass the rejection test because the decoder also fails.
    return fake.read_ok ? UpdateStorageRead::Ok : UpdateStorageRead::Error;
}
bool fake_write(void *raw, const uint8_t *bytes, size_t size) {
    FakeBackend &fake = *static_cast<FakeBackend *>(raw);
    ++fake.write_calls;
    if (!fake.write_ok) return false;
    if (size > sizeof(fake.blob)) return false;
    memcpy(fake.blob, bytes, size);
    fake.blob_size = size;
    fake.read = UpdateStorageRead::Ok;
    return true;
}
void *fake_allocate(void *raw, size_t size) {
    FakeBackend &fake = *static_cast<FakeBackend *>(raw);
    return fake.allocate_ok && size == sizeof(fake_stage) ? fake_stage : nullptr;
}
void fake_free(void *, void *) {}
size_t fake_allocation_size(void *raw, const void *) {
    return static_cast<FakeBackend *>(raw)->allocation_bytes;
}
bool fake_external(void *raw, const void *) {
    return static_cast<FakeBackend *>(raw)->external;
}
bool fake_sha(void *raw, const uint8_t *bytes, size_t size, uint8_t digest[32]) {
    FakeBackend &fake = *static_cast<FakeBackend *>(raw);
    const unsigned call = fake.hash_calls++;
    if (call >= 2) return false;
    fake.hash_bytes[call] = bytes;
    fake.hash_sizes[call] = size;
    const size_t expected_size = call == 0 ? 152 : 64;
    fake.hash_spans_match[call] = bytes == fake_stage && size == expected_size &&
                                 memcmp(bytes, expected_image, expected_size) == 0;
    if (!fake.hash_spans_match[call]) return false;
    // A digest buffer is untrusted on failure, even if its bytes happen to
    // match. This catches ignoring the return code of either SHA operation.
    memset(digest, call == 0 ? 0x11 : 0xa5, 32);
    return fake.fail_hash_call != call + 1;
}

UpdateStorageBackend backend(FakeBackend &fake) {
    return {&fake, fake_size, fake_read, fake_write, fake_allocate, fake_free,
            fake_allocation_size, fake_external, fake_sha};
}

UpdateRelease release() {
    UpdateRelease result = {};
    result.size = 152;
    memset(result.file_sha, 0x11, sizeof(result.file_sha));
    memset(result.image_hash, 0xa5, sizeof(result.image_hash));
    strcpy(result.version, "1.2.3+4");
    return result;
}

void put16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
void append16(std::vector<uint8_t> &bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}
void append_tlv(std::vector<uint8_t> &bytes, uint16_t type,
                const std::vector<uint8_t> &value) {
    append16(bytes, type);
    append16(bytes, static_cast<uint16_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}
std::vector<uint8_t> valid_image() {
    std::vector<uint8_t> bytes(64, 0);
    put32(bytes, 0, 0x96f3b83d);
    put16(bytes, 8, 32);
    put32(bytes, 12, 32);
    bytes[20] = 1; bytes[21] = 2; put16(bytes, 22, 3); put32(bytes, 24, 4);
    append16(bytes, 0x6907); append16(bytes, 0);
    append_tlv(bytes, 0x10, std::vector<uint8_t>(32, 0xa5));
    append_tlv(bytes, 0x01, std::vector<uint8_t>(32, 0xb6));
    append_tlv(bytes, 0x22, {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01});
    put16(bytes, 66, 88);
    return bytes;
}

UpdateSnapshot live_snapshot() {
    UpdateSnapshot result = {};
    result.association_count = result.job_count = 1;
    result.associations[0].target = {0xb100, "f00dbaad12345678"};
    const uint8_t address[] = {0xd1, 0x2a, 0x83, 0x8c, 0xf0, 0xb4};
    memcpy(result.associations[0].address, address, sizeof(address));
    result.associations[0].address_type = 1;
    result.jobs[0] = {41, result.associations[0].target, release(),
                      UpdatePhase::Uploading, 99, 3, "", false};
    result.last_wall_seconds[0] = 1234;
    return result;
}

void load(FakeBackend &fake, const UpdateSnapshot &snapshot) {
    TEST_ASSERT_TRUE(update_snapshot_encode(snapshot, fake.blob, sizeof(fake.blob),
                                            fake.blob_size));
    fake.read = UpdateStorageRead::Ok;
}

void assert_hash_spans(const FakeBackend &fake, unsigned expected_calls) {
    TEST_ASSERT_EQUAL_UINT32(expected_calls, fake.hash_calls);
    for (unsigned i = 0; i < expected_calls; ++i) {
        TEST_ASSERT_EQUAL_PTR(fake_stage, fake.hash_bytes[i]);
        TEST_ASSERT_EQUAL_UINT32(i == 0 ? 152 : 64, fake.hash_sizes[i]);
        TEST_ASSERT_TRUE_MESSAGE(fake.hash_spans_match[i], "SHA byte span differs from literal fixture");
    }
}

void assert_job_unchanged(const UpdateJobState &before, uint64_t changed_ms,
                          const UpdateQueue &queue) {
    TEST_ASSERT_EQUAL_UINT32(1, queue.size());
    const UpdateJob *job = queue.at(0);
    TEST_ASSERT_EQUAL_UINT32(before.id, job->id());
    TEST_ASSERT_EQUAL_UINT16(before.target.tag, job->target().tag);
    TEST_ASSERT_EQUAL_STRING(before.target.hardware_id, job->target().hardware_id);
    TEST_ASSERT_EQUAL_UINT32(before.release.size, job->release().size);
    TEST_ASSERT_EQUAL_MEMORY(before.release.file_sha, job->release().file_sha, 32);
    TEST_ASSERT_EQUAL_MEMORY(before.release.image_hash, job->release().image_hash, 32);
    TEST_ASSERT_EQUAL_STRING(before.release.version, job->release().version);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before.phase), static_cast<int>(job->phase()));
    TEST_ASSERT_EQUAL_UINT32(before.acknowledged, job->acknowledged());
    TEST_ASSERT_EQUAL_UINT8(before.attempts, job->attempts());
    TEST_ASSERT_EQUAL_STRING(before.error, job->error());
    TEST_ASSERT_EQUAL(before.trial_may_be_armed, job->trial_may_be_armed());
    TEST_ASSERT_EQUAL_UINT64(changed_ms, job->changed_ms());
}

void assert_unavailable(UpdateStorage &storage, const UpdateRelease &wanted) {
    const uint8_t *bytes = expected_image;
    TEST_ASSERT_FALSE(storage.acquire(wanted, bytes));
    TEST_ASSERT_NULL(bytes);
    TEST_ASSERT_NULL(storage.staged_release());
    TEST_ASSERT_FALSE(storage.borrowed());
}
}  // namespace

// Break caught: treating an absent record as a storage fault would prevent a
// new controller from becoming ready.
void test_missing_snapshot_starts_ready_with_an_empty_queue() {
    FakeBackend fake;
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;

    TEST_ASSERT_TRUE(storage.begin(queue, 77));
    TEST_ASSERT_TRUE(storage.ready());
    TEST_ASSERT_EQUAL_UINT32(0, queue.size());
    TEST_ASSERT_EQUAL_UINT32(0, storage.snapshot().job_count);
}

// Break caught: replacing a caller's queue before durable boot checkpointing
// would discard active work when NVS commit fails.
void test_boot_checkpoint_failure_leaves_existing_queue_untouched() {
    FakeBackend fake;
    fake.write_ok = false;
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;
    UpdateTarget target = {0xb100, "f00dbaad12345678"};
    TEST_ASSERT_NOT_NULL(queue.enqueue(target, release(), 9));

    TEST_ASSERT_FALSE(storage.begin(queue, 77));
    TEST_ASSERT_FALSE(storage.ready());
    TEST_ASSERT_EQUAL_UINT32(1, queue.size());
    TEST_ASSERT_EQUAL_UINT32(1, queue.at(0)->id());
}

// Break caught: interpreting a real backend read fault as a blank record would
// silently discard the caller's active queue.
void test_read_error_startup_leaves_queue_untouched() {
    FakeBackend fake;
    fake.read = UpdateStorageRead::Error;
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;
    TEST_ASSERT_NOT_NULL(queue.enqueue({0xb100, "f00dbaad12345678"}, release(), 9));

    TEST_ASSERT_FALSE(storage.begin(queue, 77));
    TEST_ASSERT_FALSE(storage.ready());
    TEST_ASSERT_EQUAL_UINT32(1, queue.size());
    TEST_ASSERT_EQUAL_UINT32(0, fake.write_calls);
}

// Break caught: ignoring a failed blob read after an OK size query would import
// untrusted bytes and write a boot checkpoint over the existing record.
void test_blob_read_failure_after_successful_size_preserves_state() {
    FakeBackend fake;
    load(fake, live_snapshot());
    fake.read_ok = false;
    const std::vector<uint8_t> durable(fake.blob, fake.blob + fake.blob_size);
    UpdateStorage storage(backend(fake));
    uint8_t before_snapshot[sizeof(UpdateSnapshot)];
    memcpy(before_snapshot, &storage.snapshot(), sizeof(before_snapshot));
    UpdateQueue queue(7);
    TEST_ASSERT_NOT_NULL(queue.enqueue({0xb101, "0123456789abcdef"}, release(), 9));
    const UpdateJobState before = update_export_state(*queue.at(0));

    TEST_ASSERT_FALSE(storage.begin(queue, 77));
    TEST_ASSERT_FALSE(storage.ready());
    TEST_ASSERT_EQUAL_UINT32(1, fake.size_calls);
    TEST_ASSERT_EQUAL_UINT32(1, fake.read_calls);
    TEST_ASSERT_EQUAL_UINT32(0, fake.write_calls);
    TEST_ASSERT_EQUAL_UINT32(durable.size(), fake.blob_size);
    TEST_ASSERT_EQUAL_MEMORY(durable.data(), fake.blob, durable.size());
    TEST_ASSERT_EQUAL_MEMORY(before_snapshot, &storage.snapshot(), sizeof(before_snapshot));
    assert_job_unchanged(before, 9, queue);
    assert_unavailable(storage, release());
}

// Break caught: accepting corrupt or oversized persisted bytes would import a
// fabricated update state instead of retaining the caller's existing work.
void test_corrupt_and_oversize_startup_leave_queue_untouched() {
    for (unsigned mode = 0; mode < 2; ++mode) {
        FakeBackend fake;
        fake.read = UpdateStorageRead::Ok;
        fake.blob_size = mode ? UPDATE_SNAPSHOT_MAX_ENCODED_SIZE + 1 : 1;
        fake.blob[0] = 0;
        UpdateStorage storage(backend(fake));
        UpdateQueue queue;
    TEST_ASSERT_NOT_NULL(queue.enqueue({0xb100, "f00dbaad12345678"}, release(), 9));

        TEST_ASSERT_FALSE(storage.begin(queue, 77));
        TEST_ASSERT_FALSE(storage.ready());
        TEST_ASSERT_EQUAL_UINT32(1, queue.size());
        TEST_ASSERT_EQUAL_UINT32(0, fake.write_calls);
    }
}

// Break caught: a recovery path that retains upload offsets or changes historic
// wall time could resume an unsafe partial transfer or destroy audit history.
void test_restore_persists_restart_mapping_before_replacing_queue() {
    FakeBackend fake;
    UpdateSnapshot saved = live_snapshot();
    saved.restart_count = 8;
    load(fake, saved);
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;

    TEST_ASSERT_TRUE(storage.begin(queue, 77));
    const UpdateJob *job = queue.find(41);
    TEST_ASSERT_NOT_NULL(job);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdatePhase::NeedsRelease),
                          static_cast<int>(job->phase()));
    TEST_ASSERT_EQUAL_UINT32(0, job->acknowledged());
    TEST_ASSERT_EQUAL_UINT64(77, job->changed_ms());
    TEST_ASSERT_EQUAL_UINT32(9, storage.snapshot().restart_count);
    TEST_ASSERT_EQUAL_UINT64(1234, storage.snapshot().last_wall_seconds[0]);
}

// Break caught: a missing external buffer must not turn a valid restored queue
// into an unavailable updater store.
void test_no_external_capacity_keeps_restored_store_ready() {
    FakeBackend fake;
    load(fake, live_snapshot());
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;

    TEST_ASSERT_TRUE(storage.begin(queue, 7));
    TEST_ASSERT_TRUE(storage.ready());
    TEST_ASSERT_EQUAL_UINT32(1, queue.size());
    TEST_ASSERT_EQUAL_UINT32(0, storage.capacity());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateStorageError::Allocate),
                          static_cast<int>(storage.staging_error()));
}

// Break caught: publishing a candidate as committed after a failed NVS commit
// would authorize effects from state that cannot be recovered on reboot.
void test_checkpoint_failure_retains_old_snapshot_and_revokes_readiness() {
    FakeBackend fake;
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;
    TEST_ASSERT_TRUE(storage.begin(queue, 1));
    fake.write_ok = false;
    const UpdateSnapshot candidate = live_snapshot();

    TEST_ASSERT_FALSE(storage.checkpoint(candidate));
    TEST_ASSERT_FALSE(storage.ready());
    TEST_ASSERT_EQUAL_UINT32(1, storage.snapshot().restart_count);
    TEST_ASSERT_EQUAL_UINT32(0, storage.snapshot().job_count);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateStorageError::Write),
                          static_cast<int>(storage.storage_error()));
}

// Break caught: an invalid caller snapshot must not erase the last good record
// or turn a still-durable store into an unavailable one.
void test_invalid_checkpoint_keeps_previous_committed_snapshot() {
    FakeBackend fake;
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;
    TEST_ASSERT_TRUE(storage.begin(queue, 1));
    UpdateSnapshot invalid = live_snapshot();
    invalid.association_count = 0;

    TEST_ASSERT_FALSE(storage.checkpoint(invalid));
    TEST_ASSERT_TRUE(storage.ready());
    TEST_ASSERT_EQUAL_UINT32(1, storage.snapshot().restart_count);
    TEST_ASSERT_EQUAL_UINT32(0, storage.snapshot().job_count);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdateStorageError::Encode),
                          static_cast<int>(storage.storage_error()));
}

// Break caught: malformed or duplicate checkpoint records must be rejected
// before any NVS write can replace the prior durable snapshot.
void test_malformed_and_duplicate_checkpoint_do_not_write() {
    for (unsigned mode = 0; mode < 2; ++mode) {
        FakeBackend fake;
        UpdateStorage storage(backend(fake));
        UpdateQueue queue;
        TEST_ASSERT_TRUE(storage.begin(queue, 1));
        const unsigned writes_before = fake.write_calls;
        UpdateSnapshot invalid = live_snapshot();
        if (mode == 0) invalid.jobs[0].release.version[0] = 0;
        else {
            invalid.job_count = 2;
            invalid.jobs[1] = invalid.jobs[0];
        }

        TEST_ASSERT_FALSE(storage.checkpoint(invalid));
        TEST_ASSERT_TRUE(storage.ready());
        TEST_ASSERT_EQUAL_UINT32(writes_before, fake.write_calls);
        TEST_ASSERT_EQUAL_UINT32(0, storage.snapshot().job_count);
    }
}

// Break caught: accepting mutable/partial staged bytes would expose an image
// before structure and both hashes have been checked.
void test_stage_valid_image_is_only_acquirable_after_finish() {
    FakeBackend fake;
    fake.allocate_ok = true;
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;
    TEST_ASSERT_TRUE(storage.begin(queue, 1));
    const UpdateRelease wanted = release();
    const std::vector<uint8_t> image = valid_image();
    const uint8_t *bytes = nullptr;

    TEST_ASSERT_TRUE(storage.stage_begin(wanted));
    TEST_ASSERT_FALSE(storage.acquire(wanted, bytes));
    TEST_ASSERT_TRUE(storage.stage_write(image.data(), image.size()));
    TEST_ASSERT_TRUE(storage.stage_finish());
    assert_hash_spans(fake, 2);
    TEST_ASSERT_TRUE(storage.acquire(wanted, bytes));
    TEST_ASSERT_EQUAL_MEMORY(image.data(), bytes, image.size());
    TEST_ASSERT_TRUE(storage.borrowed());
    storage.release();
    TEST_ASSERT_FALSE(storage.borrowed());
}

// Break caught: partial, overflowing, and null input must make an attempted
// image unavailable rather than leaving it eligible for acquire.
void test_incomplete_overflow_and_null_writes_clear_staged_availability() {
    for (unsigned mode = 0; mode < 3; ++mode) {
        FakeBackend fake;
        fake.allocate_ok = true;
        UpdateStorage storage(backend(fake));
        UpdateQueue queue;
        TEST_ASSERT_TRUE(storage.begin(queue, 1));
        const std::vector<uint8_t> image = valid_image();
        const uint8_t *bytes = nullptr;
        TEST_ASSERT_TRUE(storage.stage_begin(release()));
        if (mode == 0) {
            TEST_ASSERT_TRUE(storage.stage_write(image.data(), image.size() - 1));
            TEST_ASSERT_FALSE(storage.stage_finish());
        } else if (mode == 1) {
            TEST_ASSERT_FALSE(storage.stage_write(image.data(), image.size() + 1));
        } else {
            TEST_ASSERT_FALSE(storage.stage_write(nullptr, 1));
        }
        TEST_ASSERT_FALSE(storage.acquire(release(), bytes));
        TEST_ASSERT_NULL(storage.staged_release());
    }
}

// Exercise every stage fault with a recovered live job, and preserve both the
// committed snapshot and its durable wire bytes. Only the IO primitives vary.
static void reject_stage(const UpdateRelease &wanted, unsigned fail_hash_call,
                         unsigned expected_hash_calls, bool expected_write_ok = true) {
    FakeBackend fake;
    fake.allocate_ok = true;
    fake.fail_hash_call = fail_hash_call;
    load(fake, live_snapshot());
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;
    TEST_ASSERT_TRUE(storage.begin(queue, 77));
    const UpdateJobState before = update_export_state(*queue.at(0));
    TEST_ASSERT_EQUAL_UINT32(41, before.id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UpdatePhase::NeedsRelease), static_cast<int>(before.phase));
    const std::vector<uint8_t> durable(fake.blob, fake.blob + fake.blob_size);
    uint8_t before_snapshot[sizeof(UpdateSnapshot)];
    memcpy(before_snapshot, &storage.snapshot(), sizeof(before_snapshot));

    const std::vector<uint8_t> image = valid_image();
    TEST_ASSERT_TRUE(storage.stage_begin(wanted));
    TEST_ASSERT_EQUAL(expected_write_ok, storage.stage_write(image.data(), image.size()));
    TEST_ASSERT_FALSE(storage.stage_finish());
    assert_hash_spans(fake, expected_hash_calls);
    assert_unavailable(storage, wanted);
    assert_unavailable(storage, release());
    TEST_ASSERT_TRUE(storage.ready());
    TEST_ASSERT_EQUAL_UINT32(1, fake.write_calls);  // Only the boot checkpoint.
    TEST_ASSERT_EQUAL_UINT32(durable.size(), fake.blob_size);
    TEST_ASSERT_EQUAL_MEMORY(durable.data(), fake.blob, durable.size());
    TEST_ASSERT_EQUAL_MEMORY(before_snapshot, &storage.snapshot(), sizeof(before_snapshot));
    assert_job_unchanged(before, 77, queue);
    // A failed finish must leave no active receive session either.
    TEST_ASSERT_TRUE(storage.stage_begin(release()));
    TEST_ASSERT_TRUE(storage.stage_abort());
}

// Break caught: disregarding either SHA operation's error exposes unverified bytes.
void test_first_sha_failure_never_stages_a_release() {
    reject_stage(release(), 1, 1);
}

void test_second_sha_failure_never_stages_a_release() {
    reject_stage(release(), 2, 2);
}

// Break caught: omitting any one release metadata comparison accepts the wrong image.
void test_wrong_image_hash_never_stages_a_release() {
    UpdateRelease wanted = release();
    wanted.image_hash[0] = 0x33;
    reject_stage(wanted, 0, 2);
}

void test_wrong_whole_file_sha_never_stages_a_release() {
    UpdateRelease wanted = release();
    wanted.file_sha[0] = 0x33;
    reject_stage(wanted, 0, 2);
}

void test_wrong_size_never_stages_a_release() {
    UpdateRelease wanted = release();
    wanted.size = 153;  // Full 152-byte image is still short of declared length.
    reject_stage(wanted, 0, 0);
    wanted.size = 151;  // Full image exceeds the declared length.
    reject_stage(wanted, 0, 0, false);
}

void test_wrong_version_never_stages_a_release() {
    UpdateRelease wanted = release();
    strcpy(wanted.version, "1.2.3+5");
    reject_stage(wanted, 0, 2);
}

// Break caught: replacing a release that a live restored job still requires
// would make recovery impossible after a radio reconnect.
void test_live_job_protects_the_current_valid_staged_release_from_replacement() {
    FakeBackend fake;
    fake.allocate_ok = true;
    load(fake, live_snapshot());
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;
    TEST_ASSERT_TRUE(storage.begin(queue, 1));
    const std::vector<uint8_t> image = valid_image();
    TEST_ASSERT_TRUE(storage.stage_begin(release()));
    TEST_ASSERT_TRUE(storage.stage_write(image.data(), image.size()));
    TEST_ASSERT_TRUE(storage.stage_finish());
    UpdateRelease replacement = release();
    replacement.file_sha[0] = 0x22;

    TEST_ASSERT_FALSE(storage.stage_begin(replacement));
    TEST_ASSERT_NOT_NULL(storage.staged_release());
}

// Break caught: allowing a buffer to be cleared or overwritten while borrowed
// would invalidate immutable worker bytes.
void test_borrowed_stage_refuses_abort_and_replacement_until_release() {
    FakeBackend fake;
    fake.allocate_ok = true;
    UpdateStorage storage(backend(fake));
    UpdateQueue queue;
    TEST_ASSERT_TRUE(storage.begin(queue, 1));
    const std::vector<uint8_t> image = valid_image();
    const uint8_t *bytes = nullptr;
    TEST_ASSERT_TRUE(storage.stage_begin(release()));
    TEST_ASSERT_TRUE(storage.stage_write(image.data(), image.size()));
    TEST_ASSERT_TRUE(storage.stage_finish());
    TEST_ASSERT_TRUE(storage.acquire(release(), bytes));

    TEST_ASSERT_FALSE(storage.stage_abort());
    TEST_ASSERT_FALSE(storage.stage_begin(release()));
    TEST_ASSERT_EQUAL_MEMORY(image.data(), bytes, image.size());
    storage.release();
    TEST_ASSERT_TRUE(storage.stage_abort());
    TEST_ASSERT_NULL(storage.staged_release());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_missing_snapshot_starts_ready_with_an_empty_queue);
    RUN_TEST(test_boot_checkpoint_failure_leaves_existing_queue_untouched);
    RUN_TEST(test_read_error_startup_leaves_queue_untouched);
    RUN_TEST(test_blob_read_failure_after_successful_size_preserves_state);
    RUN_TEST(test_corrupt_and_oversize_startup_leave_queue_untouched);
    RUN_TEST(test_restore_persists_restart_mapping_before_replacing_queue);
    RUN_TEST(test_no_external_capacity_keeps_restored_store_ready);
    RUN_TEST(test_checkpoint_failure_retains_old_snapshot_and_revokes_readiness);
    RUN_TEST(test_invalid_checkpoint_keeps_previous_committed_snapshot);
    RUN_TEST(test_malformed_and_duplicate_checkpoint_do_not_write);
    RUN_TEST(test_stage_valid_image_is_only_acquirable_after_finish);
    RUN_TEST(test_incomplete_overflow_and_null_writes_clear_staged_availability);
    RUN_TEST(test_first_sha_failure_never_stages_a_release);
    RUN_TEST(test_second_sha_failure_never_stages_a_release);
    RUN_TEST(test_wrong_image_hash_never_stages_a_release);
    RUN_TEST(test_wrong_whole_file_sha_never_stages_a_release);
    RUN_TEST(test_wrong_size_never_stages_a_release);
    RUN_TEST(test_wrong_version_never_stages_a_release);
    RUN_TEST(test_live_job_protects_the_current_valid_staged_release_from_replacement);
    RUN_TEST(test_borrowed_stage_refuses_abort_and_replacement_until_release);
    return UNITY_END();
}
