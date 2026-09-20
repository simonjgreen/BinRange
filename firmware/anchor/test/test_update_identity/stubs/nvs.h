#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>
using esp_err_t = int;
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_FAIL = 2, NVS_READWRITE = 1;
namespace identity_fake {
inline std::vector<uint8_t> saved;
inline int fail = 0;
inline unsigned writes = 0, live = 0;
}
inline int nvs_open(const char *ns, int mode, nvs_handle_t *out) {
    assert(!std::strcmp(ns, "binble") && mode == NVS_READWRITE);
    if (identity_fake::fail == 1) return ESP_FAIL;
    ++identity_fake::live; *out = 1; return ESP_OK;
}
inline void nvs_close(nvs_handle_t h) { assert(h && identity_fake::live); --identity_fake::live; }
inline int nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size) {
    assert(h && identity_fake::live && !std::strcmp(key, "identity"));
    if (identity_fake::fail == 2 || (out && identity_fake::fail == 3)) return ESP_FAIL;
    if (identity_fake::saved.empty()) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) { *size = identity_fake::saved.size(); return ESP_OK; }
    if (*size < identity_fake::saved.size()) return ESP_FAIL;
    std::memcpy(out, identity_fake::saved.data(), identity_fake::saved.size());
    *size = identity_fake::saved.size(); return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size) {
    assert(h && identity_fake::live && !std::strcmp(key, "identity"));
    ++identity_fake::writes;
    if (identity_fake::fail == 4) return ESP_FAIL;
    const auto *p = static_cast<const uint8_t *>(data);
    identity_fake::saved.assign(p, p + size); return ESP_OK;
}
inline int nvs_commit(nvs_handle_t h) {
    assert(h && identity_fake::live);
    if (identity_fake::fail == 6) identity_fake::saved.clear(); // apparent commit, no durable data
    return identity_fake::fail == 5 ? ESP_FAIL : ESP_OK;
}
