#include "update_web.h"
#include <WebServer.h>
#include <esp_timer.h>
#include <cstring>
#include "admin.h"
#include "tag_updater.h"
#include "update_ble.h"
#include "publisher.h"
#include "webui.h"
#include "update_page.h"
#include "core/update_manifest.h"
#include "core/update_status.h"

namespace {
UpdateWebServer *server;
char *json = nullptr;
constexpr size_t json_capacity = 32768;
bool upload_started = false, upload_ended = false, upload_failed = false;
uint64_t upload_started_ms;
int upload_error = 400;
const char *upload_reason = "Invalid release upload";
uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }

void error(int status, const char *message) {
    server->sendHeader("Cache-Control", "no-store");
    server->send(status, "text/plain", message);
}
int result_code(UpdateQueueResult result) {
    switch (result) {
        case UpdateQueueResult::Accepted: return 202;
        case UpdateQueueResult::Invalid: return 400;
        case UpdateQueueResult::Conflict: case UpdateQueueResult::NeedsAction: return 409;
        case UpdateQueueResult::Capacity: return 507;
        default: return 503;
    }
}
const char *result_text(UpdateQueueResult result) {
    switch (result) {
        case UpdateQueueResult::Accepted: return "Accepted; observe status for the outcome";
        case UpdateQueueResult::Invalid: return "Invalid or unadopted target/release";
        case UpdateQueueResult::Conflict: return "Another operation or release owns this resource";
        case UpdateQueueResult::NeedsAction: return "Commission the tag before updating";
        case UpdateQueueResult::Capacity: return "Insufficient updater capacity";
        default: return "Updater unavailable; inspect status";
    }
}
bool number(const String &text, uint32_t &out, uint32_t max) {
    if (!text.length() || text.length() > 10) return false;
    out = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9' || out > max / 10 || (out == max / 10 && unsigned(c - '0') > max % 10)) return false;
        out = out * 10 + c - '0';
    }
    return true;
}
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool tag_arg(uint16_t &tag) {
    const String value = server->arg("tag");
    if (value.length() != 4) return false;
    tag = 0;
    for (size_t i = 0; i < 4; ++i) {
        const int v = nibble(value[i]); if (v < 0) return false;
        tag = uint16_t((tag << 4) | v);
    }
    return tag && tag != UINT16_MAX;
}
bool exact_fields(unsigned count) {
    // Duplicate and unexpected arguments must not choose a different target.
    if (server->args() != count) return false;
    for (unsigned i = 0; i < count; ++i)
        for (unsigned n = 0; n < i; ++n)
            if (server->argName(i) == server->argName(n)) return false;
    return true;
}
void ble_trace_json(UpdateJson &j, const UpdateBleTrace &trace) {
    j.raw("{\"operation\":"); j.number(trace.operation);
    j.raw(",\"session\":"); j.number(trace.session);
    j.raw(",\"command\":"); j.number(trace.command);
    j.raw(",\"stage\":"); j.number(trace.stage);
    j.raw(",\"started_ms\":"); j.number(trace.started_ms);
    j.raw(",\"tx_done_ms\":"); j.number(trace.tx_done_ms);
    j.raw(",\"notify_ms\":"); j.number(trace.notify_ms);
    j.raw(",\"handled_ms\":"); j.number(trace.handled_ms);
    j.raw(",\"ended_ms\":"); j.number(trace.ended_ms);
    j.raw(",\"sent\":"); j.number(trace.sent);
    j.raw(",\"received\":"); j.number(trace.received);
    j.raw(",\"outcome\":"); j.str(trace.outcome);
    j.raw(",\"status\":"); j.number(trace.status);
    j.raw("}");
}
void status() {
    if (!admin_authorize(*server, false)) return;
    if (!json) { error(507, "Status buffer unavailable (PSRAM required)"); return; }
    const auto &s = tag_updater_snapshot();
    const auto *release = tag_updater_release();
    UpdateJson j(json, json_capacity);
    j.raw("{\"hostname\":"); j.str(webui_hostname().c_str());
    j.raw(",\"local_ble_address\":");
    if (const char *address = update_ble_local_address()) j.str(address); else j.raw("null");
    j.raw(",\"disabled\":"); j.boolean(s.disabled);
    j.raw(",\"error\":"); j.str(s.error);
    j.raw(",\"active_job_id\":"); j.number(s.active_job_id);
    j.raw(",\"disconnecting\":"); j.boolean(s.disconnecting);
    j.raw(",\"restart_count\":"); j.number(tag_updater_restart_count());
    UpdateBleDiagnostics trace;
    update_ble_diagnostics(trace);
    j.raw(",\"ble_trace\":{\"current\":"); ble_trace_json(j, trace.current);
    j.raw(",\"first_failure\":"); ble_trace_json(j, trace.first_failure); j.raw("}");
    j.raw(",\"ble_fault\":{\"current\":"); j.number(trace.fault);
    j.raw(",\"first\":"); j.number(trace.first_fault);
    j.raw(",\"queue_or_frame_errors\":"); j.number(trace.queue_drops);
    j.raw(",\"store_type\":"); j.number(trace.store_type);
    j.raw(",\"store_stage\":"); j.number(trace.store_stage);
    char code[24];
    std::snprintf(code, sizeof(code), "%d", trace.store_rc);
    j.raw(",\"store_rc\":"); j.raw(code);
    j.raw(",\"security_seen\":"); j.boolean(trace.security_seen);
    std::snprintf(code, sizeof(code), "%d", trace.security_rc);
    j.raw(",\"security_rc\":"); j.raw(code);
    j.raw(",\"security_flags\":"); j.number(trace.security_flags); j.raw("}");
    j.raw(",\"staging\":{\"capacity\":"); j.number(s.staging_capacity);
    j.raw(",\"available\":"); j.boolean(release != nullptr);
    if (release) {
        j.raw(",\"version\":"); j.str(release->version);
        j.raw(",\"size\":"); j.number(release->size);
        j.raw(",\"file_sha256\":"); j.hex(release->file_sha, 32);
        j.raw(",\"image_hash\":"); j.hex(release->image_hash, 32);
    }
    j.raw("},\"tags\":[");
    Registry &registry = publisher_registry();
    bool first = true;
    for (size_t i = 0; i < registry.size(); ++i) {
        const auto *tag = registry.at(i);
        if (!tag->adopted) continue;
        if (!first) j.raw(","); first = false;
        char id[5]; snprintf(id, sizeof(id), "%04x", tag->addr);
        j.raw("{\"id\":"); j.str(id); j.raw(",\"name\":"); j.str(tag->name);
        const UpdateAssociation *association = nullptr;
        for (size_t n = 0; n < s.association_count; ++n)
            if (s.associations[n].target.tag == tag->addr) association = &s.associations[n];
        j.raw(",\"paired\":"); j.boolean(association != nullptr);
        if (association) {
            j.raw(",\"hardware_id\":"); j.str(association->target.hardware_id);
            char address[18];
            snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                association->address[0], association->address[1], association->address[2],
                association->address[3], association->address[4], association->address[5]);
            j.raw(",\"address\":"); j.str(address);
            j.raw(",\"address_type\":"); j.number(association->address_type);
        }
        j.raw("}");
    }
    j.raw("],\"jobs\":[");
    for (size_t i = 0; i < s.job_count; ++i) {
        if (i) j.raw(",");
        const auto *tag = registry.find(s.jobs[i].target.tag);
        update_job_json(j, s, i, tag ? tag->name : "", now_ms());
    }
    j.raw("],\"pairing\":{\"phase\":"); j.str(update_pairing_name(s.pairing.phase));
    j.raw(",\"error\":"); j.str(s.pairing.error);
    j.raw("}}");
    const size_t size = j.finish();
    if (!size) { error(507, "Status exceeded bounded buffer"); return; }
    server->sendHeader("Cache-Control", "no-store");
    server->setContentLength(size);
    server->send(200, "application/json", "");
    server->sendContent(json, size);
}
void pair() {
    if (!admin_authorize(*server, true)) return;
    UpdateAssociation association{};
    uint32_t type = 0, pin = 0;
    const String address = server->arg("address"), id = server->arg("hardware_id");
    String secret = server->arg("pin");
    bool valid = exact_fields(5) && tag_arg(association.target.tag) && address.length() == 17 &&
        id.length() == 16 && number(server->arg("address_type"), type, 1) &&
        secret.length() == 6 && number(secret, pin, 999999);
    for (size_t i = 0; valid && i < 6; ++i) {
        const int a = nibble(address[3*i]), b = nibble(address[3*i+1]);
        valid = a >= 0 && b >= 0 && (i == 5 || address[3*i+2] == ':');
        if (valid) association.address[i] = uint8_t(a * 16 + b);
    }
    for (size_t i = 0; valid && i < 16; ++i) {
        const int digit = nibble(id[i]); valid = digit >= 0;
        if (valid) association.target.hardware_id[i] = "0123456789abcdef"[digit];
    }
    association.address_type = uint8_t(type);
    const auto result = valid ? tag_updater_pair(association, pin) : UpdateQueueResult::Invalid;
    volatile uint32_t *p = &pin; *p = 0;
    for (size_t i = 0; i < secret.length(); ++i) secret.setCharAt(i, 0);
    error(result_code(result), result_text(result));
}
void queue() {
    if (!admin_authorize(*server, true)) return;
    uint16_t tag; uint32_t id;
    const auto result = exact_fields(1) && tag_arg(tag)
        ? tag_updater_queue(tag, &id) : UpdateQueueResult::Invalid;
    error(result_code(result), result_text(result));
}
void job_action(bool retry) {
    if (!admin_authorize(*server, true)) return;
    uint32_t id;
    if (!exact_fields(1) || !number(server->arg("id"), id, UINT32_MAX) || !id) {
        error(400, "Invalid job ID"); return;
    }
    const bool ok = retry ? tag_updater_retry(id) : tag_updater_cancel(id);
    error(ok ? 202 : 409, ok ? "Accepted; observe job status" : "Job cannot accept this action");
}
void fail_upload(int code, const char *reason) {
    if (!upload_failed) { upload_error = code; upload_reason = reason; }
    upload_failed = true;
    tag_updater_stage_abort();
}
void upload() {
    HTTPUpload &up = server->upload();
    if (!admin_authorize(*server, true)) { fail_upload(403, "Release upload not authorized"); return; }
    if (up.status == UPLOAD_FILE_START) {
        if (upload_started || up.name != "file") { fail_upload(400, "Exactly one image file required"); return; }
        upload_started = true; upload_started_ms = now_ms();
        UpdateRelease release{};
        String manifest;
        if (!server->upload_manifest(manifest) || !parse_update_manifest(manifest.c_str(), manifest.length(), release)) {
            fail_upload(400, "Manifest must precede the file and match the release schema"); return;
        }
        const auto result = tag_updater_stage_begin(release);
        if (result != UpdateQueueResult::Accepted) fail_upload(result_code(result), result_text(result));
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (upload_failed) return;
        if (!upload_started || upload_ended || now_ms() - upload_started_ms >= 60000 ||
            !tag_updater_stage_write(up.buf, up.currentSize)) fail_upload(400, "Incomplete, oversized or expired upload");
    } else if (up.status == UPLOAD_FILE_END) {
        if (!upload_started || upload_ended) fail_upload(400, "Invalid file boundary");
        upload_ended = true;
    } else if (up.status == UPLOAD_FILE_ABORTED) fail_upload(400, "Upload interrupted");
}
void uploaded() {
    if (!admin_authorize(*server, true)) { tag_updater_stage_abort(); return; }
    if (!upload_started || !upload_ended || upload_failed || !exact_fields(1) ||
        !server->hasArg("manifest") || now_ms() - upload_started_ms >= 60000) {
        tag_updater_stage_abort(); error(upload_error, upload_reason); return;
    }
    if (!tag_updater_stage_finish()) { error(400, "Image size, target, hashes or structure do not match the release"); return; }
    server->send(204);
}
}

bool UpdateWebServer::upload_manifest(String &out) const {
    if (_currentArgCount != 0 || !_postArgs || _postArgsLen != 1 ||
        _postArgs[0].key != "manifest" || _postArgs[0].value.length() > 512) return false;
    out = _postArgs[0].value;
    return true;
}
void UpdateWebServer::wipe_request_arguments() {
    auto wipe = [](RequestArgument *args, int count) {
        if (!args) return;
        for (int i = 0; i < count; ++i) {
            volatile char *p = const_cast<char *>(args[i].value.c_str());
            for (size_t n = 0; n < args[i].value.length(); ++n) p[n] = 0;
        }
    };
    wipe(_currentArgs, _currentArgCount);
    wipe(_postArgs, _postArgsLen);
}
void update_web_begin(UpdateWebServer &http) {
    server = &http;
    json = static_cast<char *>(ps_malloc(json_capacity));
    server->on("/tag-updates", HTTP_GET, [] {
        if (!admin_authorize(*server, false)) return;
        server->sendHeader("Cache-Control", "no-store");
        server->send_P(200, "text/html", UPDATE_PAGE);
    });
    server->on("/api/tag-updates", HTTP_GET, status);
    server->on("/api/tag-updates/pair", HTTP_POST, pair);
    server->on("/api/tag-updates/queue", HTTP_POST, queue);
    server->on("/api/tag-updates/cancel", HTTP_POST, [] { job_action(false); });
    server->on("/api/tag-updates/retry", HTTP_POST, [] { job_action(true); });
    server->on("/api/tag-updates/release", HTTP_POST, uploaded, upload);
}
void update_web_request_begin() {
    upload_started = upload_ended = upload_failed = false;
    upload_error = 400; upload_reason = "Invalid release upload";
}
void update_web_request_end() {
    tag_updater_stage_abort();
    if (server) server->wipe_request_arguments();
}
