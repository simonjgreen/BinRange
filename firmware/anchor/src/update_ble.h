#pragma once

#include "core/update_controller.h"

enum class UpdateBleFault : uint8_t { None, Startup, QueueOverflow, DisconnectTimeout, BondStore, Identity };

// Worker owns BLE identity/bond persistence; main loop owns jobs and MQTT.
// Initialization runs off the main loop because the library waits for host sync.
bool update_ble_begin(bool has_associations);
bool update_ble_ready();
// Immutable persisted identity, published only after successful initialization.
const char *update_ble_local_address();
UpdateBleFault update_ble_fault();
UpdateControllerTransport &update_ble_transport();

// Volatile, metadata-only diagnostics for the protected status endpoint. Never
// include frame bytes, addresses, pairing PINs or security material here.
struct UpdateBleTrace {
    uint32_t operation = 0, session = 0;
    uint8_t command = 0, stage = 0;
    uint64_t started_ms = 0, tx_done_ms = 0, notify_ms = 0, handled_ms = 0, ended_ms = 0;
    uint32_t sent = 0, received = 0;
    const char *outcome = "idle";
    uint32_t status = 0;
};
struct UpdateBleDiagnostics {
    UpdateBleTrace current, first_failure;
    uint8_t fault = 0, first_fault = 0, security_flags = 0;
    uint32_t queue_drops = 0; // queue-full OR invalid/oversized notification
    int store_type = 0, store_stage = 0, store_rc = 0, security_rc = 0;
    bool security_seen = false;
};
void update_ble_diagnostics(UpdateBleDiagnostics &out);
