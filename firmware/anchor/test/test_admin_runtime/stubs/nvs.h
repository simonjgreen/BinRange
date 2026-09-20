#pragma once
#include "Arduino.h"
#include <vector>

using esp_err_t = int;
using nvs_handle_t = unsigned;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 1;
constexpr esp_err_t ESP_FAIL = 2;
constexpr int NVS_READWRITE = 1;

namespace fake_sdk {
struct Nvs {
  std::vector<char> blob;
  bool missing = false;
  bool open_error = false;
  bool size_error = false;
  bool read_error = false;
  bool set_error = false;
  bool commit_error = false;
  unsigned opens = 0, closes = 0, sizes = 0, reads = 0, sets = 0, commits = 0;
  unsigned live_handles = 0;
  bool invalid_arguments = false;
};
inline Nvs nvs;
}

inline esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
  auto &s = fake_sdk::nvs;
  ++s.opens;
  if (std::strcmp(name, "binadmin") || mode != NVS_READWRITE) s.invalid_arguments = true;
  if (s.open_error) return ESP_FAIL;
  *out = ++s.live_handles;
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) {
  auto &s = fake_sdk::nvs;
  if (!handle || !s.live_handles) s.invalid_arguments = true;
  else --s.live_handles;
  ++s.closes;
}
inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length) {
  auto &s = fake_sdk::nvs;
  if (!handle || !s.live_handles || std::strcmp(key, "password")) s.invalid_arguments = true;
  if (out) ++s.reads; else ++s.sizes;
  if ((out && s.read_error) || (!out && s.size_error)) return ESP_FAIL;
  if (s.missing) return ESP_ERR_NVS_NOT_FOUND;
  if (!out) { *length = s.blob.size(); return ESP_OK; }
  if (*length < s.blob.size() || *length > 65) { s.invalid_arguments = true; return ESP_FAIL; }
  std::memcpy(out, s.blob.data(), s.blob.size());
  *length = s.blob.size();
  return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length) {
  auto &s = fake_sdk::nvs;
  ++s.sets;
  if (!handle || !s.live_handles || std::strcmp(key, "password") || length < 13 || length > 65)
    s.invalid_arguments = true;
  if (s.set_error) return ESP_FAIL;
  const char *bytes = static_cast<const char *>(data);
  s.blob.assign(bytes, bytes + length);
  s.missing = false; // A failed commit can already have changed storage.
  return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t handle) {
  auto &s = fake_sdk::nvs;
  ++s.commits;
  if (!handle || !s.live_handles) s.invalid_arguments = true;
  return s.commit_error ? ESP_FAIL : ESP_OK;
}
