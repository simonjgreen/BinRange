#pragma once

#include <cstddef>
#include <cstdint>

#include "core/update_image.h"
#include "core/update_snapshot.h"

// These callbacks are the only platform-dependent part of UpdateStorage.
// snapshot_size must not copy data; this lets the core reject an oversize NVS
// blob before asking the backend to read it. write_snapshot returns true only
// after its backend has durably committed the whole blob.
enum class UpdateStorageRead { Missing, Ok, Error };

struct UpdateStorageBackend {
    void *context;
    UpdateStorageRead (*snapshot_size)(void *context, size_t &size);
    UpdateStorageRead (*read_snapshot)(void *context, uint8_t *bytes, size_t size);
    bool (*write_snapshot)(void *context, const uint8_t *bytes, size_t size);
    void *(*allocate_external)(void *context, size_t size);
    void (*free_external)(void *context, void *bytes);
    size_t (*allocation_size)(void *context, const void *bytes);
    bool (*is_external)(void *context, const void *bytes);
    bool (*sha256)(void *context, const uint8_t *bytes, size_t size,
                   uint8_t digest[32]);
};

enum class UpdateStorageError : uint8_t {
    None,
    Read,
    Decode,
    Encode,
    Write,
    Allocate,
    Hash,
    Stage,
};

// Main-loop-owned persistence and image staging. It is noncopyable and makes
// no thread-safety promise. A worker may inspect pinned bytes from acquire()
// until release(); it must not access them afterwards.
class UpdateStorage {
  public:
    explicit UpdateStorage(const UpdateStorageBackend &backend);
    ~UpdateStorage();
    UpdateStorage(const UpdateStorage &) = delete;
    UpdateStorage &operator=(const UpdateStorage &) = delete;

    bool begin(UpdateQueue &queue, uint64_t new_boot_now);
    bool checkpoint(const UpdateSnapshot &snapshot);
    bool ready() const { return ready_; }
    const UpdateSnapshot &snapshot() const { return snapshot_; }
    UpdateStorageError storage_error() const { return storage_error_; }
    UpdateStorageError staging_error() const { return staging_error_; }
    size_t capacity() const { return capacity_; }

    bool stage_begin(const UpdateRelease &release);
    bool stage_write(const uint8_t *bytes, size_t size);
    bool stage_finish();
    bool stage_abort();
    const UpdateRelease *staged_release() const;
    bool acquire(const UpdateRelease &release, const uint8_t *&bytes);
    void release();
    bool borrowed() const { return borrowed_; }

  private:
    bool persist(const UpdateSnapshot &candidate);
    bool release_in_use(const UpdateRelease &release) const;
    static bool same_release(const UpdateRelease &left, const UpdateRelease &right);

    UpdateStorageBackend backend_;
    UpdateQueue *queue_;
    UpdateSnapshot snapshot_;
    // Recovery workspace belongs to the long-lived owner, not begin()'s
    // constrained ESP32 main-loop stack.
    UpdateSnapshot recovery_;
    UpdateQueue restored_;
    uint8_t encoded_[UPDATE_SNAPSHOT_MAX_ENCODED_SIZE];
    void *stage_bytes_;
    size_t capacity_;
    size_t received_;
    UpdateRelease receiving_;
    UpdateRelease staged_;
    bool ready_;
    bool receiving_active_;
    bool staged_valid_;
    bool borrowed_;
    UpdateStorageError storage_error_;
    UpdateStorageError staging_error_;
};
