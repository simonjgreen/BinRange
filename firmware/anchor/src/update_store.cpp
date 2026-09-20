#include "update_store.h"

#include <mbedtls/sha256.h>
#include <nvs.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp32-hal-psram.h>
#include <soc/soc_memory_types.h>

namespace {
constexpr char kNamespace[] = "binupdate";
constexpr char kSnapshotKey[] = "snapshot";

struct EspBackend {
    esp_err_t last_error = ESP_OK;
    uint32_t free_entries = 0;
};

void remember(EspBackend &state, esp_err_t error) {
    state.last_error = error;
    nvs_stats_t stats = {};
    if (nvs_get_stats(nullptr, &stats) == ESP_OK)
        state.free_entries = stats.free_entries;
}

UpdateStorageRead size(void *raw, size_t &length) {
    EspBackend &state = *static_cast<EspBackend *>(raw);
    nvs_handle_t handle;
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) { remember(state, error); return UpdateStorageRead::Error; }
    error = nvs_get_blob(handle, kSnapshotKey, nullptr, &length);
    nvs_close(handle);
    remember(state, error);
    if (error == ESP_ERR_NVS_NOT_FOUND) return UpdateStorageRead::Missing;
    return error == ESP_OK ? UpdateStorageRead::Ok : UpdateStorageRead::Error;
}

UpdateStorageRead read(void *raw, uint8_t *bytes, size_t length) {
    EspBackend &state = *static_cast<EspBackend *>(raw);
    nvs_handle_t handle;
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) { remember(state, error); return UpdateStorageRead::Error; }
    size_t actual = length;
    error = nvs_get_blob(handle, kSnapshotKey, bytes, &actual);
    nvs_close(handle);
    remember(state, error);
    return error == ESP_OK && actual == length ? UpdateStorageRead::Ok
                                                : UpdateStorageRead::Error;
}

bool write(void *raw, const uint8_t *bytes, size_t length) {
    EspBackend &state = *static_cast<EspBackend *>(raw);
    nvs_handle_t handle;
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    const bool opened = error == ESP_OK;
    if (error == ESP_OK) error = nvs_set_blob(handle, kSnapshotKey, bytes, length);
    if (error == ESP_OK) error = nvs_commit(handle);
    if (opened) nvs_close(handle);
    remember(state, error);
    return error == ESP_OK;
}

void *allocate(void *, size_t length) {
    if (!psramFound()) return nullptr;
    return heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void free_external(void *, void *bytes) { heap_caps_free(bytes); }
size_t allocation_size(void *, const void *bytes) {
    return heap_caps_get_allocated_size(const_cast<void *>(bytes));
}
bool external(void *, const void *bytes) { return bytes && esp_ptr_external_ram(bytes); }
bool sha256(void *, const uint8_t *bytes, size_t length, uint8_t digest[32]) {
    return mbedtls_sha256_ret(bytes, length, digest, 0) == 0;
}

EspBackend &esp_backend() { static EspBackend state; return state; }
UpdateStorageBackend backend() {
    EspBackend &state = esp_backend();
    return {&state, size, read, write, allocate, free_external, allocation_size,
            external, sha256};
}
}  // namespace

UpdateStorage &update_store() {
    static UpdateStorage store(backend());
    return store;
}

int32_t update_store_last_nvs_error() { return esp_backend().last_error; }
uint32_t update_store_nvs_free_entries() { return esp_backend().free_entries; }
