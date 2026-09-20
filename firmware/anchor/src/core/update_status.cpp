#include "core/update_status.h"
#include <cstdio>
#include <cstring>

UpdateJson::UpdateJson(char *buffer, size_t capacity)
    : buffer_(buffer), capacity_(capacity), ok_(buffer && capacity) {
    if (ok_) buffer_[0] = 0;
}
void UpdateJson::raw(const char *text) {
    if (!ok_) return;
    const size_t n = std::strlen(text);
    if (n >= capacity_ - length_) { ok_ = false; return; }
    std::memcpy(buffer_ + length_, text, n + 1);
    length_ += n;
}
void UpdateJson::str(const char *text) {
    raw("\"");
    if (text) for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
        char escaped[7];
        if (*p < 0x20) std::snprintf(escaped, sizeof(escaped), "\\u%04x", unsigned(*p));
        else if (*p == '\\' || *p == '"') { escaped[0] = '\\'; escaped[1] = *p; escaped[2] = 0; }
        else { escaped[0] = *p; escaped[1] = 0; }
        raw(escaped);
    }
    raw("\"");
}
void UpdateJson::number(uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    raw(buffer);
}
void UpdateJson::hex(const uint8_t *bytes, size_t size) {
    raw("\"");
    for (size_t i = 0; i < size; ++i) {
        char pair[3]; std::snprintf(pair, sizeof(pair), "%02x", bytes[i]); raw(pair);
    }
    raw("\"");
}
size_t UpdateJson::finish() {
    if (ok_) return length_;
    if (buffer_ && capacity_) buffer_[0] = 0;
    return 0;
}
const char *update_phase_name(UpdatePhase phase) {
    static const char *names[] = {"queued", "waiting", "connecting", "uploading", "rebooting",
        "checking", "successful", "failed", "cancelled", "needs_release", "needs_action"};
    return size_t(phase) < sizeof(names) / sizeof(names[0]) ? names[size_t(phase)] : "unknown";
}
const char *update_pairing_name(UpdatePairingPhase phase) {
    static const char *names[] = {"idle", "connecting", "securing", "checking", "disconnecting", "successful", "failed"};
    return size_t(phase) < sizeof(names) / sizeof(names[0]) ? names[size_t(phase)] : "unknown";
}
void update_job_json(UpdateJson &j, const UpdateControllerSnapshot &s,
                     size_t index, const char *name, uint64_t now) {
    if (index >= s.job_count || index >= UPDATE_QUEUE_MAX) { j.raw("null"); return; }
    const auto &job = s.jobs[index];
    const auto &observation = s.observed[index];
    char tag[5]; std::snprintf(tag, sizeof(tag), "%04x", job.target.tag);
    j.raw("{\"id\":"); j.number(job.id);
    j.raw(",\"tag\":"); j.str(tag);
    j.raw(",\"name\":"); j.str(name);
    j.raw(",\"hardware_id\":"); j.str(job.target.hardware_id);
    j.raw(",\"phase\":"); j.str(update_phase_name(job.phase));
    j.raw(",\"acknowledged\":"); j.number(job.acknowledged);
    j.raw(",\"size\":"); j.number(job.release.size);
    j.raw(",\"attempts\":"); j.number(job.attempts);
    j.raw(",\"error\":"); j.str(job.error);
    j.raw(",\"version\":"); j.str(job.release.version);
    j.raw(",\"image_hash\":"); j.hex(job.release.image_hash, 32);
    j.raw(",\"file_sha256\":"); j.hex(job.release.file_sha, 32);
    j.raw(",\"changed_ms\":"); j.number(s.changed_ms[index]);
    j.raw(",\"observed_version\":");
    if (observation.image_valid) j.str(observation.active.version); else j.raw("null");
    j.raw(",\"observed_hash\":");
    if (observation.image_valid) j.hex(observation.active.hash, 32); else j.raw("null");
    j.raw(",\"confirmed\":");
    if (observation.image_valid) j.boolean(observation.active.confirmed); else j.raw("null");
    j.raw(",\"observed_age_ms\":");
    if ((observation.image_valid || observation.identity_valid) && now >= observation.seen_ms)
        j.number(now - observation.seen_ms);
    else j.raw("null");
    j.raw(",\"maintenance\":");
    // This is the controller's live session ownership, not stale tag telemetry.
    j.boolean(s.active_job_id == job.id);
    j.raw(",\"tag_maintenance_observed\":");
    if (observation.identity_valid) j.boolean(observation.tag.maintenance); else j.raw("null");
    j.raw(",\"reset_reason\":");
    if (observation.identity_valid) j.number(observation.tag.reset_reason); else j.raw("null");
    j.raw(",\"trial_may_be_armed\":"); j.boolean(job.trial_may_be_armed);
    j.raw("}");
}
