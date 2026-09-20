#include "update_ble.h"
#include "update_identity.h"
#include "anchor_identity_bootstrap.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include <cstring>
#include <new>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "nimble/nimble/host/include/host/ble_gatt.h"
#include "nimble/nimble/host/include/host/ble_hs.h"
#include "nimble/nimble/host/include/host/ble_sm.h"
#include "nimble/nimble/host/include/host/ble_store.h"
#include "nimble/porting/nimble/include/nimble/nimble_port.h"

extern "C" int binrange_bond_restore_status(void);

namespace {
constexpr uint32_t kScanMs = 15000;
uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
void erase(void *data, size_t size) {
    volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
    while (size--) *p++ = 0;
}
bool same_address(const ble_addr_t &a, const ble_addr_t &b) {
    return a.type == b.type && std::memcmp(a.val, b.val, sizeof(a.val)) == 0;
}

// Native key persistence updates RAM before NVS and ignores some return codes.
// Latch failures here; no RAM-only bond may authorize management afterwards.
std::atomic<bool> bond_store_failed{true};
// First failed store operation only. No key/address bytes enter diagnostics.
// Stage: 0 invalid backend/input, 1 read, 2 immutable-key guard, 3 write,
// 4 boot restore. Pack type/stage into one word to conserve static DRAM.
std::atomic<uint32_t> bond_failure_location{0};
std::atomic<int> bond_failure_rc{0};
std::atomic<UpdateBleFault> first_worker_fault{UpdateBleFault::None};
void record_first_fault(UpdateBleFault fault) {
    auto expected = UpdateBleFault::None;
    first_worker_fault.compare_exchange_strong(expected, fault);
}
ble_store_write_fn *bond_backend_write = nullptr;
ble_store_read_fn *bond_backend_read = nullptr;

bool same_security(const ble_store_value_sec &a, const ble_store_value_sec &b) {
    return same_address(a.peer_addr, b.peer_addr) &&
        a.key_size == b.key_size && a.authenticated == b.authenticated && a.sc == b.sc &&
        a.ltk_present == b.ltk_present && a.irk_present == b.irk_present &&
        a.csrk_present == b.csrk_present &&
        (!a.ltk_present || (a.ediv == b.ediv && a.rand_num == b.rand_num &&
                           std::memcmp(a.ltk, b.ltk, sizeof(a.ltk)) == 0)) &&
        (!a.irk_present || std::memcmp(a.irk, b.irk, sizeof(a.irk)) == 0) &&
        (!a.csrk_present || (a.sign_counter == b.sign_counter &&
                            std::memcmp(a.csrk, b.csrk, sizeof(a.csrk)) == 0));
}

int checked_bond_write(int type, const ble_store_value *value) {
    if (bond_store_failed.load()) return BLE_HS_ESTORE_FAIL;
    int rc = 0;
    int stage = 0;
    if (!value || !bond_backend_write || !bond_backend_read) rc = BLE_HS_ESTORE_FAIL;
    else if (type == BLE_STORE_OBJ_TYPE_PEER_SEC || type == BLE_STORE_OBJ_TYPE_OUR_SEC) {
        ble_store_key key{};
        key.sec.peer_addr = value->sec.peer_addr;
        ble_store_value previous{};
        stage = 1;
        rc = bond_backend_read(type, &key, &previous);
        const bool unchanged = rc == 0 && same_security(previous.sec, value->sec);
        erase(&previous, sizeof(previous));
        // The pinned NVS backend silently skips equal-count updates. Existing
        // key material is immutable: repeat pairing/replacement is not allowed.
        if (unchanged) return 0;
        if (rc == 0) { stage = 2; rc = BLE_HS_ESTORE_FAIL; }
        else if (rc == BLE_HS_ENOENT) { stage = 3; rc = bond_backend_write(type, value); }
    } else { stage = 3; rc = bond_backend_write(type, value); }
    if (rc != 0) {
        record_first_fault(UpdateBleFault::BondStore);
        bond_failure_location = (static_cast<uint32_t>(type) << 16) | stage;
        bond_failure_rc = rc;
        bond_store_failed = true;
    }
    return rc;
}

enum class Step : uint8_t {
    Idle, Scanning, Connecting, Connected, Security, Mtu, Service, Characteristic,
    Descriptor, Subscribe, Ready, Writing, Reply, Closing, Quiet
};
enum class NoteKind : uint8_t {
    Found, ScanDone, Connected, Disconnected, Security, Passkey, Mtu,
    Service, Characteristic, Descriptor, Written, Notify
};
struct Note {
    NoteKind kind;
    Step step;
    uint32_t operation, session;
    uint16_t connection, a, b, c;
    int status;
    bool match;
    size_t size;
    uint8_t bytes[512];
    uint64_t arrived_ms;
};
class Worker;
struct GapContext { Worker *owner; uint32_t session; ble_addr_t address; };
struct GattContext { Worker *owner; uint32_t session, operation; Step step; };

class StorePolicy final : public NimBLEDeviceCallbacks {
    int onStoreStatus(ble_store_status_event *, void *) override {
        // Never invoke NimBLE's default round-robin eviction policy.
        return BLE_HS_ESTORE_CAP;
    }
};

class Worker final : public UpdateControllerTransport {
  public:
    bool begin(bool has_associations);
    bool submit(const UpdateTransportCommand &command) override;
    bool poll(UpdateTransportEvent &event) override {
        return events_ && xQueueReceive(events_, &event, 0) == pdTRUE;
    }
    bool ready() const { return ready_.load() && fault() == UpdateBleFault::None; }
    const char *local_address() const { return ready() ? local_address_ : nullptr; }
    UpdateBleFault fault() const {
        const auto error = fault_.load();
        return error != UpdateBleFault::None ? error :
            (ready_.load() && bond_store_failed.load() ? UpdateBleFault::BondStore : UpdateBleFault::None);
    }
    void note(const Note &n) {
        if (xQueueSend(notes_, &n, 0) != pdTRUE) {
            ++queue_drops_; set_fault(UpdateBleFault::QueueOverflow);
        }
    }
    void diagnostics(UpdateBleDiagnostics &out) {
        portENTER_CRITICAL(&trace_lock_);
        out = published_trace_ ? *published_trace_ : UpdateBleDiagnostics{};
        portEXIT_CRITICAL(&trace_lock_);
        out.fault = static_cast<uint8_t>(fault());
        out.first_fault = static_cast<uint8_t>(first_worker_fault.load());
        out.queue_drops = queue_drops_.load();
        if (bond_store_failed.load()) {
            const uint32_t location = bond_failure_location.load();
            out.store_type = location >> 16;
            out.store_stage = location & 0xffff;
            out.store_rc = bond_failure_rc.load();
        }
    }

  private:
    void set_fault(UpdateBleFault error) {
        record_first_fault(error);
        fault_ = error;
    }
    static void task(void *arg) { static_cast<Worker *>(arg)->run(); }
    static int gap(ble_gap_event *event, void *arg);
    static int mtu(uint16_t conn, const ble_gatt_error *error, uint16_t value, void *arg);
    static int service(uint16_t conn, const ble_gatt_error *error, const ble_gatt_svc *value, void *arg);
    static int characteristic(uint16_t conn, const ble_gatt_error *error, const ble_gatt_chr *value, void *arg);
    static int descriptor(uint16_t conn, const ble_gatt_error *error, uint16_t value,
                          const ble_gatt_dsc *dsc, void *arg);
    static int written(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *, void *arg);
    static void barrier(ble_npl_event *event);
    static Note from_gatt(NoteKind kind, uint16_t conn, const ble_gatt_error *error, void *arg);
    void run();
    bool load_identity();
    bool apply_identity();
    void command(UpdateTransportCommand &cmd);
    void handle(const Note &n);
    bool emit(UpdateTransportEventKind kind, UpdateTransportFailure failure = UpdateTransportFailure::Transient);
    void fail(UpdateTransportFailure failure);
    void close();
    void closing();
    bool physically_closed();
    bool peer(ble_gap_conn_desc &desc) const;
    bool bond(ble_store_value_sec &value) const;
    bool secure() const;
    void security();
    void discover(Step step);
    void write_fragment();
    void reply_if_complete();
    void trace_end(const char *outcome, uint32_t status = 0);
    void trace_publish();
    void context(Step next) {
        step_ = next;
        gatt_ = {this, current_.session, current_.operation, next};
    }

    // A consumed FreeRTOS queue retains its backing bytes. Use a wiped mailbox
    // for commands, since explicit commissioning carries a one-use PIN.
    SemaphoreHandle_t command_lock_ = nullptr;
    // Allocate the wiped mailbox at startup to leave static DRAM for NimBLE's
    // per-bond tables. Allocation failure prevents worker startup.
    UpdateTransportCommand *pending_command_ = nullptr;
    bool command_pending_ = false;
    QueueHandle_t notes_ = nullptr, events_ = nullptr;
    std::atomic<bool> started_{false}, ready_{false}, found_{false};
    std::atomic<bool> barrier_complete_{false};
    std::atomic<UpdateBleFault> fault_{UpdateBleFault::None};
    std::atomic<uint32_t> queue_drops_{0};
    int security_rc_ = 0;
    uint8_t security_flags_ = 0;
    bool security_seen_ = false;
    std::atomic<uint32_t> callback_operation_{0};
    Step step_ = Step::Idle;
    UpdateTransportCommand current_{};
    GapContext gap_{};
    GattContext gatt_{};
    StorePolicy store_policy_;
    ble_npl_event barrier_{};
    uint8_t own_type_ = BLE_OWN_ADDR_RANDOM;
    uint8_t local_identity_[6]{};
    char local_address_[18]{};
    bool has_associations_ = false;
    uint16_t conn_ = BLE_HS_CONN_HANDLE_NONE;
    uint16_t svc_start_ = 0, svc_end_ = 0, value_ = 0, value_end_ = 0, cccd_ = 0;
    unsigned matches_ = 0;
    bool without_response_ = false, secured_ = false, write_pending_ = false;
    bool reply_complete_ = false, barrier_pending_ = false, want_disconnect_ack_ = false;
    bool failure_sent_ = false;
    bool pin_available_ = false;
    size_t sent_ = 0, writing_size_ = 0;
    uint64_t deadline_ = 0, session_deadline_ = 0, idle_deadline_ = 0, next_close_try_ = 0;
    SmpAssembler assembler_;
    UpdateBleTrace trace_{};
    UpdateBleDiagnostics *published_trace_ = nullptr;
    portMUX_TYPE trace_lock_ = portMUX_INITIALIZER_UNLOCKED;
    const NimBLEUUID service_uuid_{"8d53dc1d-1db7-4cd3-868b-8a527460aa84"};
    const NimBLEUUID characteristic_uuid_{"da2e7828-fbce-4e01-ae9e-261174997c48"};
};

bool Worker::begin(bool has_associations) {
    if (started_.exchange(true)) return false;
    has_associations_ = has_associations;
    published_trace_ = new (std::nothrow) UpdateBleDiagnostics{};
    pending_command_ = new (std::nothrow) UpdateTransportCommand{};
    command_lock_ = xSemaphoreCreateMutex();
    notes_ = xQueueCreate(12, sizeof(Note));
    events_ = xQueueCreate(4, sizeof(UpdateTransportEvent));
    if (!published_trace_ || !pending_command_ || !command_lock_ || !notes_ || !events_ ||
        xTaskCreatePinnedToCore(task, "tag-ble", 8192, this, 1, nullptr, 0) != pdPASS) {
        set_fault(UpdateBleFault::Startup);
        return false;
    }
    return true;
}

bool Worker::submit(const UpdateTransportCommand &cmd) {
    if (!ready_.load() || !cmd.operation || !cmd.session || cmd.frame_size > sizeof(cmd.frame) ||
        (cmd.commissioning && (cmd.kind != UpdateTransportCommandKind::Security || cmd.passkey > 999999)) ||
        (!cmd.commissioning && cmd.passkey) ||
        (fault() != UpdateBleFault::None && cmd.kind != UpdateTransportCommandKind::Disconnect))
        return false;
    if (cmd.kind == UpdateTransportCommandKind::Connect) {
        bool zero = true, ff = true;
        for (uint8_t b : cmd.association.address) { zero &= b == 0; ff &= b == 255; }
        if (zero || ff || cmd.association.address_type > 1 ||
            (cmd.association.address_type == 1 && (cmd.association.address[0] & 0xc0) != 0xc0))
            return false;
    }
    if (xSemaphoreTake(command_lock_, 0) != pdTRUE) return false;
    const bool accepted = !command_pending_;
    if (accepted) { *pending_command_ = cmd; command_pending_ = true; }
    xSemaphoreGive(command_lock_);
    return accepted;
}

bool Worker::emit(UpdateTransportEventKind kind, UpdateTransportFailure failure) {
    UpdateTransportEvent event{};
    event.kind = kind;
    event.operation = current_.operation;
    event.session = current_.session;
    event.bonded_mitm_sc = kind == UpdateTransportEventKind::Secured && secured_;
    event.failure = failure;
    if (kind == UpdateTransportEventKind::Reply && assembler_.reply()) event.reply = *assembler_.reply();
    if (xQueueSend(events_, &event, 0) == pdTRUE) return true;
    ++queue_drops_; set_fault(UpdateBleFault::QueueOverflow);
    return false;
}

void Worker::run() {
    wifi_ps_type_t wifi_sleep;
    if (esp_wifi_get_ps(&wifi_sleep) != ESP_OK || wifi_sleep == WIFI_PS_NONE) {
        set_fault(UpdateBleFault::Startup);
        vTaskDelete(nullptr);
        return;
    }
    NimBLEDevice::setDeviceCallbacks(&store_policy_);
    if (!NimBLEDevice::init("BinRange anchor") || !NimBLEDevice::setMTU(256)) {
        set_fault(UpdateBleFault::Startup);
        vTaskDelete(nullptr);
        return;
    }
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
    const int restore_rc = binrange_bond_restore_status();
    if (restore_rc != 0 || !ble_hs_cfg.store_write_cb || !ble_hs_cfg.store_read_cb) {
        bond_failure_location = restore_rc ? 4 : 0;
        bond_failure_rc = restore_rc ? restore_rc : BLE_HS_ESTORE_FAIL;
        set_fault(UpdateBleFault::BondStore);
        vTaskDelete(nullptr);
        return;
    }
    bond_backend_write = ble_hs_cfg.store_write_cb;
    bond_backend_read = ble_hs_cfg.store_read_cb;
    ble_hs_cfg.store_write_cb = checked_bond_write;
    if (!load_identity()) {
        set_fault(UpdateBleFault::Identity);
        vTaskDelete(nullptr);
        return;
    }
    bond_store_failed = false;
    ble_npl_event_init(&barrier_, barrier, this);
    ready_ = true;
    for (;;) {
        UpdateTransportCommand cmd{};
        bool received = false;
        if (xSemaphoreTake(command_lock_, 0) == pdTRUE) {
            if (command_pending_) {
                cmd = *pending_command_;
                erase(pending_command_, sizeof(*pending_command_));
                command_pending_ = false;
                received = true;
            }
            xSemaphoreGive(command_lock_);
        }
        if (received) command(cmd);
        erase(&cmd, sizeof(cmd));
        Note n{};
        for (unsigned i = 0; i < 12 && xQueueReceive(notes_, &n, 0) == pdTRUE; ++i) handle(n);
        if (step_ == Step::Closing || step_ == Step::Quiet) closing();
        else if (step_ != Step::Idle) {
            const uint64_t now = now_ms();
            if (fault() != UpdateBleFault::None) fail(UpdateTransportFailure::Unavailable);
            else if (now >= session_deadline_ || now >= idle_deadline_ ||
                     ((step_ != Step::Ready && step_ != Step::Connected) && now >= deadline_))
                fail(UpdateTransportFailure::Transient);
            else if (step_ == Step::Writing && !write_pending_) write_fragment();
        }
        trace_publish();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Worker::trace_publish() {
    portENTER_CRITICAL(&trace_lock_);
    published_trace_->current = trace_;
    published_trace_->security_seen = security_seen_;
    published_trace_->security_rc = security_rc_;
    published_trace_->security_flags = security_flags_;
    if (trace_.ended_ms && std::strcmp(trace_.outcome, "reply") != 0 &&
        !published_trace_->first_failure.operation)
        published_trace_->first_failure = trace_;
    portEXIT_CRITICAL(&trace_lock_);
}

void Worker::trace_end(const char *outcome, uint32_t status) {
    if (!trace_.operation || trace_.ended_ms) return;
    trace_.stage = static_cast<uint8_t>(step_);
    trace_.ended_ms = now_ms();
    trace_.outcome = outcome;
    trace_.status = status;
    trace_publish();
}

bool Worker::load_identity() {
    uint8_t board[6]{}, fresh[6]{};
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS]{};
    int count = 0;
    if (esp_read_mac(board, ESP_MAC_WIFI_STA) != ESP_OK ||
        ble_hs_id_copy_addr(BLE_ADDR_RANDOM, fresh, nullptr) != 0 ||
        ble_store_util_bonded_peers(peers, &count, CONFIG_BT_NIMBLE_MAX_BONDS) != 0 ||
        count < 0 || count > CONFIG_BT_NIMBLE_MAX_BONDS) return false;
    UpdateIdentityRecovery recovery{};
    if (binrange::ble_identity_bootstrap_present) {
        std::memcpy(recovery.board_mac, binrange::ble_identity_bootstrap_mac, 6);
        std::memcpy(recovery.address, binrange::ble_identity_bootstrap_address, 6);
    }
    if (!update_identity_load(board, fresh, has_associations_ || count != 0,
                              binrange::ble_identity_bootstrap_present ? &recovery : nullptr,
                              local_identity_) || !apply_identity()) return false;
    std::snprintf(local_address_, sizeof(local_address_), "%02X:%02X:%02X:%02X:%02X:%02X",
                  local_identity_[5], local_identity_[4], local_identity_[3],
                  local_identity_[2], local_identity_[1], local_identity_[0]);
    return true;
}

bool Worker::apply_identity() {
    // A host resync can generate another random address. Reapply the durable
    // identity before each scan/connect, with no NVS access in this path.
    uint8_t actual[6]{};
    return NimBLEDevice::setOwnAddr(local_identity_) &&
        NimBLEDevice::setOwnAddrType(own_type_) &&
        ble_hs_id_copy_addr(BLE_ADDR_RANDOM, actual, nullptr) == 0 &&
        !std::memcmp(actual, local_identity_, 6);
}

bool Worker::peer(ble_gap_conn_desc &desc) const {
    return conn_ != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(conn_, &desc) == 0 &&
        desc.role == BLE_GAP_ROLE_MASTER && same_address(desc.peer_id_addr, gap_.address) &&
        same_address(desc.peer_ota_addr, gap_.address);
}

bool Worker::bond(ble_store_value_sec &value) const {
    ble_store_key_sec key{};
    key.peer_addr = gap_.address;
    return !bond_store_failed.load() && ble_store_read_peer_sec(&key, &value) == 0 &&
        same_address(value.peer_addr, gap_.address) && value.ltk_present &&
        value.authenticated && value.sc && value.key_size == 16;
}

bool Worker::secure() const {
    ble_gap_conn_desc desc{};
    return !bond_store_failed.load() && peer(desc) && desc.sec_state.encrypted && desc.sec_state.authenticated &&
        desc.sec_state.bonded && desc.sec_state.key_size == 16;
}

void Worker::security() {
    security_seen_ = false;
    security_rc_ = 0;
    security_flags_ = 0;
    ble_gap_conn_desc desc{};
    if (!peer(desc)) { fail(UpdateTransportFailure::Identity); return; }
    step_ = Step::Security;
    pin_available_ = current_.commissioning;
    int rc;
    if (current_.commissioning) {
        rc = ble_gap_security_initiate(conn_);
    } else {
        ble_store_value_sec key{};
        if (!bond(key)) { erase(&key, sizeof(key)); fail(UpdateTransportFailure::Security); return; }
        // Deliberately use the pinned stack's explicit encryption primitive.
        // security_initiate() can fall back to pairing if its own NVS reread
        // fails. This path supplies the validated LTK and can never do so.
        rc = ble_gap_encryption_initiate(conn_, key.key_size, key.ltk, key.ediv,
                                         key.rand_num, key.authenticated);
        erase(&key, sizeof(key));
    }
    if (rc != 0) {
        security_seen_ = true;
        security_rc_ = rc;
        fail(UpdateTransportFailure::Security);
    }
}

void Worker::command(UpdateTransportCommand &cmd) {
    const uint64_t now = now_ms();
    if (cmd.kind == UpdateTransportCommandKind::Disconnect && step_ == Step::Idle) {
        // Connect may have been rejected before queueing. Idle itself proves
        // quiescence, so the owner's closing token still deserves an ACK.
        current_ = cmd;
        want_disconnect_ack_ = true;
        step_ = Step::Quiet;
        return;
    }
    if (cmd.kind == UpdateTransportCommandKind::Connect) {
        if (step_ != Step::Idle || fault() != UpdateBleFault::None) return;
        current_ = cmd;
        callback_operation_ = cmd.operation;
        gap_ = {this, cmd.session, {}};
        gap_.address.type = cmd.association.address_type;
        bool zero = true, ff = true;
        for (size_t i = 0; i < 6; ++i) {
            gap_.address.val[5 - i] = cmd.association.address[i];
            zero &= cmd.association.address[i] == 0;
            ff &= cmd.association.address[i] == 255;
        }
        failure_sent_ = false;
        want_disconnect_ack_ = false;
        found_ = false;
        secured_ = false;
        conn_ = BLE_HS_CONN_HANDLE_NONE;
        svc_start_ = svc_end_ = value_ = value_end_ = cccd_ = 0;
        session_deadline_ = now + 540000;
        idle_deadline_ = now + 45000;
        deadline_ = now + kScanMs;
        step_ = Step::Scanning;
        if (zero || ff || gap_.address.type > 1 ||
            (gap_.address.type == 1 && (cmd.association.address[0] & 0xc0) != 0xc0)) {
            fail(UpdateTransportFailure::Identity); return;
        }
        if (!apply_identity()) {
            set_fault(UpdateBleFault::Identity);
            fail(UpdateTransportFailure::Identity); return;
        }
        ble_gap_disc_params params{};
        params.passive = 1; params.filter_duplicates = 1; params.itvl = 160; params.window = 80;
        if (ble_gap_disc(own_type_, kScanMs, &params, gap, &gap_) != 0)
            fail(UpdateTransportFailure::Transient);
        return;
    }
    if (cmd.session != current_.session || cmd.operation <= current_.operation) return;
    if (cmd.kind == UpdateTransportCommandKind::Disconnect) {
        erase(&current_.passkey, sizeof(current_.passkey));
        current_.operation = cmd.operation;
        callback_operation_ = cmd.operation;
        want_disconnect_ack_ = true;
        close();
        return;
    }
    if (fault() != UpdateBleFault::None ||
        (cmd.kind == UpdateTransportCommandKind::Security && step_ != Step::Connected) ||
        (cmd.kind == UpdateTransportCommandKind::Exchange && step_ != Step::Ready)) return;
    if (std::memcmp(&cmd.association.address, &current_.association.address, 6) ||
        cmd.association.address_type != current_.association.address_type) return;
    erase(&current_.passkey, sizeof(current_.passkey));
    current_ = cmd;
    callback_operation_ = cmd.operation;
    failure_sent_ = false;
    deadline_ = now + 10000;
    idle_deadline_ = now + 45000;
    if (cmd.kind == UpdateTransportCommandKind::Security) { security(); return; }
    trace_ = {};
    trace_.operation = cmd.operation;
    trace_.session = cmd.session;
    trace_.command = static_cast<uint8_t>(cmd.smp);
    trace_.started_ms = now;
    trace_.outcome = "pending";
    if (!secured_ || !secure()) { fail(UpdateTransportFailure::Security); return; }
    if (cmd.frame_size < 8 || (size_t(cmd.frame[2]) * 256 + cmd.frame[3] + 8) != cmd.frame_size ||
        !assembler_.begin(cmd.smp, cmd.frame[6])) { fail(UpdateTransportFailure::Protocol); return; }
    sent_ = 0;
    write_pending_ = reply_complete_ = false;
    step_ = Step::Writing;
    write_fragment();
}

void Worker::fail(UpdateTransportFailure failure) {
    trace_end("transport_failure", static_cast<uint32_t>(failure));
    if (!failure_sent_) { emit(UpdateTransportEventKind::Failed, failure); failure_sent_ = true; }
    close();
}

void Worker::close() {
    // Owner deadlines/cancellation can close before the worker deadline fires.
    trace_end("owner_close");
    secured_ = false;
    pin_available_ = false;
    erase(&current_.passkey, sizeof(current_.passkey));
    current_.commissioning = false;
    assembler_.abort();
    if (step_ == Step::Closing || step_ == Step::Quiet) return;
    step_ = Step::Closing;
    deadline_ = now_ms() + 8000;
    next_close_try_ = 0;
}

bool Worker::physically_closed() {
    ble_gap_conn_desc desc{};
    return !ble_gap_disc_active() && !ble_gap_conn_active() &&
        (conn_ == BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_find(conn_, &desc) == BLE_HS_ENOTCONN) &&
        ble_gap_conn_find_by_addr(&gap_.address, &desc) == BLE_HS_ENOTCONN;
}

void Worker::closing() {
    if (step_ == Step::Closing && barrier_pending_ && barrier_complete_.load()) {
        barrier_pending_ = false;
        if (physically_closed()) step_ = Step::Quiet;
    }
    if (step_ == Step::Quiet) {
        if (want_disconnect_ack_ && emit(UpdateTransportEventKind::Disconnected)) {
            step_ = Step::Idle;
            want_disconnect_ack_ = false;
            conn_ = BLE_HS_CONN_HANDLE_NONE;
            erase(&current_, sizeof(current_));
        }
        return;
    }
    const uint64_t now = now_ms();
    if (now >= deadline_) set_fault(UpdateBleFault::DisconnectTimeout);
    if (now < next_close_try_) return;
    next_close_try_ = now + 100;
    if (ble_gap_disc_active()) ble_gap_disc_cancel();
    if (ble_gap_conn_active()) ble_gap_conn_cancel();
    ble_gap_conn_desc desc{};
    if (ble_gap_conn_find_by_addr(&gap_.address, &desc) == 0)
        ble_gap_terminate(desc.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (conn_ != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(conn_, &desc) == 0)
        ble_gap_terminate(conn_, BLE_ERR_REM_USER_CONN_TERM);
    if (!barrier_pending_ && physically_closed()) {
        // The pinned host cancels GATT procedures before its disconnect event.
        // Cross its event queue once more before reusing callback context or
        // acknowledging quiescence to the owner of the staged image.
        barrier_pending_ = true;
        barrier_complete_ = false;
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &barrier_);
    }
}

void Worker::barrier(ble_npl_event *event) {
    auto *w = static_cast<Worker *>(ble_npl_event_get_arg(event));
    // This acknowledgment must survive a full notification queue. Nothing in
    // this callback touches session data after publishing completion.
    w->barrier_complete_ = true;
}

void Worker::discover(Step step) {
    if (!secure()) { fail(UpdateTransportFailure::Security); return; }
    context(step);
    matches_ = 0;
    int rc = BLE_HS_EINVAL;
    if (step == Step::Mtu) rc = ble_gattc_exchange_mtu(conn_, mtu, &gatt_);
    else if (step == Step::Service)
        rc = ble_gattc_disc_svc_by_uuid(conn_, service_uuid_.getBase(), service, &gatt_);
    else if (step == Step::Characteristic)
        rc = ble_gattc_disc_all_chrs(conn_, svc_start_, svc_end_, characteristic, &gatt_);
    else if (step == Step::Descriptor)
        rc = ble_gattc_disc_all_dscs(conn_, value_, value_end_, descriptor, &gatt_);
    else if (step == Step::Subscribe) {
        const uint8_t enabled[] = {1, 0};
        rc = ble_gattc_write_flat(conn_, cccd_, enabled, sizeof(enabled), written, &gatt_);
    }
    if (rc != 0) fail(UpdateTransportFailure::Transient);
}

void Worker::write_fragment() {
    if (!secure()) { fail(UpdateTransportFailure::Security); return; }
    const uint16_t mtu_size = ble_att_mtu(conn_);
    if (mtu_size < 23 || mtu_size > 512 || sent_ >= current_.frame_size) {
        fail(UpdateTransportFailure::Protocol); return;
    }
    writing_size_ = current_.frame_size - sent_;
    if (writing_size_ > mtu_size - 3U) writing_size_ = mtu_size - 3U;
    context(Step::Writing);
    int rc;
    if (without_response_)
        rc = ble_gattc_write_no_rsp_flat(conn_, value_, current_.frame + sent_, writing_size_);
    else
        rc = ble_gattc_write_flat(conn_, value_, current_.frame + sent_, writing_size_, written, &gatt_);
    if (rc == BLE_HS_ENOMEM || rc == BLE_HS_EAGAIN) return; // bounded by request deadline
    if (rc != 0) { fail(UpdateTransportFailure::Transient); return; }
    if (without_response_) {
        sent_ += writing_size_;
        if (sent_ == current_.frame_size) step_ = Step::Reply;
    } else write_pending_ = true;
    trace_.sent = sent_;
    trace_.stage = static_cast<uint8_t>(step_);
    if (sent_ == current_.frame_size) trace_.tx_done_ms = now_ms();
}

void Worker::reply_if_complete() {
    if (reply_complete_ && !write_pending_ && sent_ == current_.frame_size) {
        if (!emit(UpdateTransportEventKind::Reply)) { close(); return; }
        trace_end("reply");
        step_ = Step::Ready;
        idle_deadline_ = now_ms() + 45000;
    }
}

void Worker::handle(const Note &n) {
    if (n.session != current_.session) return;
    if (n.kind == NoteKind::Connected && n.status == 0) conn_ = n.connection;
    if (step_ == Step::Closing || step_ == Step::Quiet || step_ == Step::Idle) return;
    if (fault() != UpdateBleFault::None) { fail(UpdateTransportFailure::Unavailable); return; }
    if (n.kind == NoteKind::Disconnected) {
        trace_end("disconnected", static_cast<uint32_t>(n.status));
        fail(UpdateTransportFailure::Transient); return;
    }
    if (n.kind == NoteKind::Found && step_ == Step::Scanning) {
        ble_gap_disc_cancel();
        if (!apply_identity()) {
            set_fault(UpdateBleFault::Identity);
            fail(UpdateTransportFailure::Identity); return;
        }
        step_ = Step::Connecting;
        deadline_ = now_ms() + 15000;
        ble_gap_conn_params params{};
        params.scan_itvl = 160; params.scan_window = 80;
        params.itvl_min = 12; params.itvl_max = 24; params.supervision_timeout = 400;
        if (ble_gap_connect(own_type_, &gap_.address, 15000, &params, gap, &gap_) != 0)
            fail(UpdateTransportFailure::Transient);
        return;
    }
    if (n.kind == NoteKind::ScanDone && step_ == Step::Scanning) { fail(UpdateTransportFailure::Transient); return; }
    if (n.kind == NoteKind::Connected && step_ == Step::Connecting) {
        ble_gap_conn_desc desc{};
        if (n.status != 0) fail(UpdateTransportFailure::Transient);
        else if (!peer(desc)) fail(UpdateTransportFailure::Identity);
        else { step_ = Step::Connected; emit(UpdateTransportEventKind::Connected); }
        return;
    }
    if (n.connection != conn_) return;
    if (n.kind == NoteKind::Passkey) {
        if (step_ != Step::Security || !current_.commissioning || !pin_available_ || n.a != BLE_SM_IOACT_INPUT) {
            fail(UpdateTransportFailure::Security); return;
        }
        pin_available_ = false;
        ble_sm_io io{}; io.action = BLE_SM_IOACT_INPUT; io.passkey = current_.passkey;
        const int rc = ble_sm_inject_io(conn_, &io);
        erase(&io, sizeof(io)); erase(&current_.passkey, sizeof(current_.passkey));
        if (rc != 0) fail(UpdateTransportFailure::Security);
        return;
    }
    if (n.kind == NoteKind::Security) {
        ble_store_value_sec key{};
        const bool valid = n.status == 0 && secure() && bond(key);
        ble_gap_conn_desc desc{};
        const bool matches = peer(desc);
        security_seen_ = true;
        security_rc_ = n.status;
        // Bits: peer match, encrypted, authenticated, bonded, 16-byte key,
        // full policy accepted. These are checks, never security material.
        security_flags_ = (matches ? 1 : 0) | (desc.sec_state.encrypted ? 2 : 0) |
            (desc.sec_state.authenticated ? 4 : 0) | (desc.sec_state.bonded ? 8 : 0) |
            (desc.sec_state.key_size == 16 ? 16 : 0) | (valid ? 32 : 0);
        pin_available_ = false;
        erase(&key, sizeof(key)); erase(&current_.passkey, sizeof(current_.passkey));
        if (!valid) { fail(UpdateTransportFailure::Security); return; }
        if (step_ == Step::Security) discover(Step::Mtu);
        return;
    }
    if (n.kind == NoteKind::Notify) {
        if (n.operation == trace_.operation && !trace_.ended_ms) {
            trace_.notify_ms = n.arrived_ms;
            trace_.handled_ms = now_ms();
            trace_.received += n.size;
        }
        if (!secured_ || n.a != value_ || n.operation != current_.operation ||
            (step_ != Step::Writing && step_ != Step::Reply)) return;
        if (reply_complete_ || (step_ == Step::Writing && !write_pending_)) {
            fail(UpdateTransportFailure::Protocol); return;
        }
        const SmpFeed result = assembler_.feed(n.bytes, n.size);
        if (result == SmpFeed::Error) { fail(UpdateTransportFailure::Protocol); return; }
        reply_complete_ = result == SmpFeed::Complete;
        reply_if_complete();
        return;
    }
    if (n.operation != current_.operation || n.step != step_) return;
    if (n.kind == NoteKind::Mtu) {
        if (n.status || n.a < 23) fail(UpdateTransportFailure::Transient);
        else discover(Step::Service);
    } else if (n.kind == NoteKind::Service) {
        if (n.status == 0) { ++matches_; svc_start_ = n.a; svc_end_ = n.b; }
        else if (n.status == BLE_HS_EDONE && matches_ == 1 && svc_start_ < svc_end_)
            discover(Step::Characteristic);
        else fail(UpdateTransportFailure::Protocol);
    } else if (n.kind == NoteKind::Characteristic) {
        if (n.status == 0) {
            if (value_ && n.a > value_ && value_end_ == svc_end_) value_end_ = n.a - 1;
            if (n.match) {
                ++matches_; value_ = n.b; value_end_ = svc_end_;
                without_response_ = (n.c & BLE_GATT_CHR_F_WRITE_NO_RSP) != 0;
                if (!(n.c & BLE_GATT_CHR_F_NOTIFY) || !(n.c & (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)))
                    fail(UpdateTransportFailure::Protocol);
            }
        } else if (n.status == BLE_HS_EDONE && matches_ == 1 && value_ < value_end_)
            discover(Step::Descriptor);
        else fail(UpdateTransportFailure::Protocol);
    } else if (n.kind == NoteKind::Descriptor) {
        if (n.status == 0) { if (n.match && n.b == value_) { ++matches_; cccd_ = n.a; } }
        else if (n.status == BLE_HS_EDONE && matches_ == 1) discover(Step::Subscribe);
        else fail(UpdateTransportFailure::Protocol);
    } else if (n.kind == NoteKind::Written) {
        if (n.status != 0) { fail(UpdateTransportFailure::Transient); return; }
        if (step_ == Step::Subscribe) {
            secured_ = secure();
            if (!secured_) { fail(UpdateTransportFailure::Security); return; }
            step_ = Step::Ready; emit(UpdateTransportEventKind::Secured);
        } else if (step_ == Step::Writing) {
            write_pending_ = false; sent_ += writing_size_;
            if (sent_ == current_.frame_size) step_ = Step::Reply;
            trace_.sent = sent_;
            trace_.stage = static_cast<uint8_t>(step_);
            if (sent_ == current_.frame_size) trace_.tx_done_ms = now_ms();
            reply_if_complete();
        }
    }
}

Note Worker::from_gatt(NoteKind kind, uint16_t conn, const ble_gatt_error *error, void *arg) {
    const auto ctx = *static_cast<GattContext *>(arg);
    Note n{}; n.kind = kind; n.connection = conn; n.operation = ctx.operation;
    n.session = ctx.session; n.step = ctx.step; n.status = error ? error->status : BLE_HS_EUNKNOWN;
    return n;
}
int Worker::mtu(uint16_t conn, const ble_gatt_error *error, uint16_t value, void *arg) {
    auto *owner = static_cast<GattContext *>(arg)->owner;
    Note n = from_gatt(NoteKind::Mtu, conn, error, arg); n.a = value; owner->note(n); return 0;
}
int Worker::service(uint16_t conn, const ble_gatt_error *error, const ble_gatt_svc *value, void *arg) {
    auto *owner = static_cast<GattContext *>(arg)->owner;
    Note n = from_gatt(NoteKind::Service, conn, error, arg);
    if (value) { n.a = value->start_handle; n.b = value->end_handle; }
    owner->note(n); return 0;
}
int Worker::characteristic(uint16_t conn, const ble_gatt_error *error, const ble_gatt_chr *value, void *arg) {
    auto *owner = static_cast<GattContext *>(arg)->owner;
    Note n = from_gatt(NoteKind::Characteristic, conn, error, arg);
    if (value) {
        n.a = value->def_handle; n.b = value->val_handle; n.c = value->properties;
        n.match = ble_uuid_cmp(&value->uuid.u, owner->characteristic_uuid_.getBase()) == 0;
    }
    owner->note(n); return 0;
}
int Worker::descriptor(uint16_t conn, const ble_gatt_error *error, uint16_t value,
                       const ble_gatt_dsc *dsc, void *arg) {
    auto *owner = static_cast<GattContext *>(arg)->owner;
    Note n = from_gatt(NoteKind::Descriptor, conn, error, arg); n.b = value;
    if (dsc) { n.a = dsc->handle; n.match = ble_uuid_u16(&dsc->uuid.u) == 0x2902; }
    owner->note(n); return 0;
}
int Worker::written(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *, void *arg) {
    auto *owner = static_cast<GattContext *>(arg)->owner;
    owner->note(from_gatt(NoteKind::Written, conn, error, arg)); return 0;
}

int Worker::gap(ble_gap_event *event, void *arg) {
    const auto ctx = *static_cast<GapContext *>(arg);
    Worker *w = ctx.owner;
    Note n{}; n.session = ctx.session; n.operation = w->callback_operation_.load();
    n.arrived_ms = now_ms();
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            if (!same_address(event->disc.addr, ctx.address) || w->found_.exchange(true)) return 0;
            n.kind = NoteKind::Found; break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            n.kind = NoteKind::ScanDone; break;
        case BLE_GAP_EVENT_CONNECT:
            n.kind = NoteKind::Connected; n.status = event->connect.status;
            n.connection = event->connect.conn_handle; break;
        case BLE_GAP_EVENT_DISCONNECT:
            n.kind = NoteKind::Disconnected; n.connection = event->disconnect.conn.conn_handle;
            n.status = event->disconnect.reason; break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            n.kind = NoteKind::Security; n.connection = event->enc_change.conn_handle;
            n.status = event->enc_change.status; break;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            n.kind = NoteKind::Passkey; n.connection = event->passkey.conn_handle;
            n.a = event->passkey.params.action; break;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            n.kind = NoteKind::Security; n.connection = event->repeat_pairing.conn_handle;
            n.status = BLE_HS_EAUTHEN; w->note(n);
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        case BLE_GAP_EVENT_NOTIFY_RX:
            n.kind = NoteKind::Notify; n.connection = event->notify_rx.conn_handle;
            n.a = event->notify_rx.attr_handle;
            n.size = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (n.size > sizeof(n.bytes) || os_mbuf_copydata(event->notify_rx.om, 0, n.size, n.bytes)) {
                ++w->queue_drops_; w->set_fault(UpdateBleFault::QueueOverflow); return 0;
            }
            break;
        default: return 0;
    }
    w->note(n); return 0;
}

Worker worker;
}  // namespace

bool update_ble_begin(bool has_associations) { return worker.begin(has_associations); }
bool update_ble_ready() { return worker.ready(); }
const char *update_ble_local_address() { return worker.local_address(); }
UpdateBleFault update_ble_fault() { return worker.fault(); }
UpdateControllerTransport &update_ble_transport() { return worker; }
void update_ble_diagnostics(UpdateBleDiagnostics &out) { worker.diagnostics(out); }
