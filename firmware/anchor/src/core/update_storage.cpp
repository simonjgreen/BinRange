#include "core/update_storage.h"

#include <cstring>
#include <limits>

namespace {
constexpr size_t kStageCapacity = 212992;

bool callbacks_present(const UpdateStorageBackend &backend) {
    return backend.snapshot_size && backend.read_snapshot &&
           backend.write_snapshot;
}
}  // namespace

UpdateStorage::UpdateStorage(const UpdateStorageBackend &backend)
    : backend_(backend), queue_(nullptr), snapshot_{}, recovery_{}, restored_(), encoded_{},
      stage_bytes_(nullptr), capacity_(0), received_(0), receiving_{},
      staged_{}, ready_(false), receiving_active_(false), staged_valid_(false),
      borrowed_(false), storage_error_(UpdateStorageError::None),
      staging_error_(UpdateStorageError::None) {}

UpdateStorage::~UpdateStorage() {
    if (stage_bytes_ && backend_.free_external)
        backend_.free_external(backend_.context, stage_bytes_);
}

bool UpdateStorage::persist(const UpdateSnapshot &candidate) {
    size_t written = 0;
    if (!update_snapshot_encode(candidate, encoded_, sizeof(encoded_), written)) {
        storage_error_ = UpdateStorageError::Encode;
        return false;
    }
    if (!backend_.write_snapshot(backend_.context, encoded_, written)) {
        storage_error_ = UpdateStorageError::Write;
        return false;
    }
    snapshot_ = candidate;
    storage_error_ = UpdateStorageError::None;
    return true;
}

bool UpdateStorage::begin(UpdateQueue &queue, uint64_t new_boot_now) {
    if (ready_) return queue_ == &queue;
    if (!callbacks_present(backend_)) {
        storage_error_ = UpdateStorageError::Read;
        return false;
    }

    recovery_ = {};
    size_t size = 0;
    const UpdateStorageRead state = backend_.snapshot_size(backend_.context, size);
    if (state == UpdateStorageRead::Error ||
        (state == UpdateStorageRead::Ok &&
         (size == 0 || size > UPDATE_SNAPSHOT_MAX_ENCODED_SIZE))) {
        storage_error_ = UpdateStorageError::Read;
        return false;
    }
    if (state == UpdateStorageRead::Ok &&
        (backend_.read_snapshot(backend_.context, encoded_, size) != UpdateStorageRead::Ok ||
         !update_snapshot_decode(encoded_, size, recovery_))) {
        storage_error_ = UpdateStorageError::Decode;
        return false;
    }

    if (recovery_.restart_count != std::numeric_limits<uint32_t>::max())
        ++recovery_.restart_count;
    if (!restored_.restore(recovery_.jobs, recovery_.job_count, new_boot_now)) {
        storage_error_ = UpdateStorageError::Decode;
        return false;
    }
    for (size_t i = 0; i < recovery_.job_count; ++i)
        recovery_.jobs[i] = update_export_state(*restored_.at(i));
    if (!persist(recovery_) || !queue.restore(recovery_.jobs, recovery_.job_count,
                                               new_boot_now))
        return false;

    queue_ = &queue;
    ready_ = true;
    if (backend_.allocate_external && backend_.free_external &&
        backend_.allocation_size && backend_.is_external) {
        void *bytes = backend_.allocate_external(backend_.context, kStageCapacity);
        if (bytes && backend_.allocation_size(backend_.context, bytes) >= kStageCapacity &&
            backend_.is_external(backend_.context, bytes)) {
            stage_bytes_ = bytes;
            capacity_ = kStageCapacity;
        } else {
            if (bytes) backend_.free_external(backend_.context, bytes);
            staging_error_ = UpdateStorageError::Allocate;
        }
    } else {
        staging_error_ = UpdateStorageError::Allocate;
    }
    return true;
}

bool UpdateStorage::checkpoint(const UpdateSnapshot &candidate) {
    if (!ready_) return false;
    size_t written = 0;
    if (!update_snapshot_encode(candidate, encoded_, sizeof(encoded_), written)) {
        storage_error_ = UpdateStorageError::Encode;
        return false;
    }
    if (!backend_.write_snapshot(backend_.context, encoded_, written)) {
        storage_error_ = UpdateStorageError::Write;
        ready_ = false;
        return false;
    }
    snapshot_ = candidate;
    storage_error_ = UpdateStorageError::None;
    return true;
}

bool UpdateStorage::stage_begin(const UpdateRelease &release) {
    if (!ready_ || !stage_bytes_ || !capacity_ || receiving_active_ || borrowed_ ||
        release.size < 32 || release.size > capacity_ || !release.version[0] ||
        !memchr(release.version, 0, sizeof(release.version)) ||
        (staged_valid_ && release_in_use(staged_))) {
        staging_error_ = UpdateStorageError::Stage;
        return false;
    }
    // Do not let acquire expose the old buffer after reuse starts.
    staged_valid_ = false;
    receiving_ = release;
    received_ = 0;
    receiving_active_ = true;
    staging_error_ = UpdateStorageError::None;
    return true;
}

bool UpdateStorage::stage_write(const uint8_t *bytes, size_t size) {
    if (!receiving_active_ || borrowed_ || (!bytes && size) ||
        size > receiving_.size - received_) {
        receiving_active_ = false;
        staged_valid_ = false;
        received_ = 0;
        staging_error_ = UpdateStorageError::Stage;
        return false;
    }
    if (size) memcpy(static_cast<uint8_t *>(stage_bytes_) + received_, bytes, size);
    received_ += size;
    return true;
}

bool UpdateStorage::stage_finish() {
    if (!receiving_active_ || borrowed_ || received_ != receiving_.size ||
        !backend_.sha256) {
        receiving_active_ = false;
        staged_valid_ = false;
        received_ = 0;
        staging_error_ = UpdateStorageError::Stage;
        return false;
    }
    const uint8_t *bytes = static_cast<const uint8_t *>(stage_bytes_);
    ImageInfo image = {};
    uint8_t file_sha[32] = {}, image_sha[32] = {};
    const bool valid = parse_update_image(bytes, received_, image) &&
                       image.signed_region_size <= received_ &&
                       backend_.sha256(backend_.context, bytes, received_, file_sha) &&
                       backend_.sha256(backend_.context, bytes,
                                       image.signed_region_size, image_sha) &&
                       validate_release(image, static_cast<uint32_t>(received_), file_sha,
                                        image_sha, receiving_);
    receiving_active_ = false;
    received_ = 0;
    if (!valid) {
        staged_valid_ = false;
        staging_error_ = UpdateStorageError::Hash;
        return false;
    }
    staged_ = receiving_;
    staged_valid_ = true;
    staging_error_ = UpdateStorageError::None;
    return true;
}

bool UpdateStorage::stage_abort() {
    if (borrowed_) {
        staging_error_ = UpdateStorageError::Stage;
        return false;
    }
    receiving_active_ = false;
    staged_valid_ = false;
    received_ = 0;
    staging_error_ = UpdateStorageError::None;
    return true;
}

const UpdateRelease *UpdateStorage::staged_release() const {
    return staged_valid_ ? &staged_ : nullptr;
}

bool UpdateStorage::acquire(const UpdateRelease &release, const uint8_t *&bytes) {
    bytes = nullptr;
    if (!ready_ || !staged_valid_ || borrowed_ || !same_release(release, staged_))
        return false;
    bytes = static_cast<const uint8_t *>(stage_bytes_);
    borrowed_ = true;
    return true;
}

void UpdateStorage::release() { borrowed_ = false; }

bool UpdateStorage::release_in_use(const UpdateRelease &release) const {
    if (!queue_) return false;
    for (size_t i = 0; i < queue_->size(); ++i) {
        const UpdateJob *job = queue_->at(i);
        if (!update_terminal(job->phase()) &&
            memcmp(job->release().file_sha, release.file_sha, 32) == 0)
            return true;
    }
    return false;
}

bool UpdateStorage::same_release(const UpdateRelease &left,
                                 const UpdateRelease &right) {
    return left.size == right.size &&
           memcmp(left.file_sha, right.file_sha, sizeof(left.file_sha)) == 0 &&
           memcmp(left.image_hash, right.image_hash, sizeof(left.image_hash)) == 0 &&
           memcmp(left.version, right.version, sizeof(left.version)) == 0;
}
