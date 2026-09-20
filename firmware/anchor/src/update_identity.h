#pragma once
#include <cstdint>

struct UpdateIdentityRecovery {
    uint8_t board_mac[6];  // ESP32 Wi-Fi MAC, display/MSB order.
    uint8_t address[6];    // Static-random BLE identity, NimBLE/LSB order.
};

// Startup only, before any connection. Stored identity wins; malformed storage
// is never treated as missing. A legacy bond requires explicit recovery input.
bool update_identity_load(const uint8_t board_mac[6], const uint8_t fresh[6],
                          bool known_peers, const UpdateIdentityRecovery *recovery,
                          uint8_t out[6]);
