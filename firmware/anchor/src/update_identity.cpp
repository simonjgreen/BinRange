#include "update_identity.h"
#include <nvs.h>
#include <cstring>

namespace {
constexpr uint8_t marker[] = {'B', 'L', 'I', 1};
constexpr size_t record_size = 16;
bool random_identity(const uint8_t *address) {
    if (!address || (address[5] & 0xc0) != 0xc0) return false;
    bool zero = (address[5] & 0x3f) == 0, ones = (address[5] & 0x3f) == 0x3f;
    for (unsigned i = 0; i < 5; ++i) { zero &= address[i] == 0; ones &= address[i] == 255; }
    return !zero && !ones;
}
bool valid(const uint8_t *record, const uint8_t *board) {
    return !std::memcmp(record, marker, 4) && !std::memcmp(record + 4, board, 6) &&
           random_identity(record + 10);
}
struct Handle {
    nvs_handle_t value = 0;
    bool opened = false;
    ~Handle() { if (opened) nvs_close(value); }
};
}

bool update_identity_load(const uint8_t board[6], const uint8_t fresh[6], bool known_peers,
                          const UpdateIdentityRecovery *recovery, uint8_t out[6]) {
    if (!out) return false;
    std::memset(out, 0, 6);
    if (!board || !fresh || (recovery &&
        (std::memcmp(recovery->board_mac, board, 6) || !random_identity(recovery->address)))) return false;
    Handle h;
    if (nvs_open("binble", NVS_READWRITE, &h.value) != ESP_OK) return false;
    h.opened = true;
    size_t size = 0;
    const esp_err_t result = nvs_get_blob(h.value, "identity", nullptr, &size);
    uint8_t record[record_size]{};
    if (result == ESP_OK) {
        if (size != sizeof(record) || nvs_get_blob(h.value, "identity", record, &size) != ESP_OK ||
            size != sizeof(record) || !valid(record, board)) return false;
        if (recovery && std::memcmp(record + 10, recovery->address, 6)) return false;
    } else if (result == ESP_ERR_NVS_NOT_FOUND) {
        if (known_peers && !recovery) return false;
        const uint8_t *address = recovery ? recovery->address : fresh;
        if (!random_identity(address)) return false;
        std::memcpy(record, marker, 4);
        std::memcpy(record + 4, board, 6);
        std::memcpy(record + 10, address, 6);
        // NVS supplies record integrity and atomicity. Verify committed readback
        // before this identity can authorize a connection; never erase/format.
        if (nvs_set_blob(h.value, "identity", record, sizeof(record)) != ESP_OK ||
            nvs_commit(h.value) != ESP_OK) return false;
        nvs_close(h.value); h.opened = false;
        if (nvs_open("binble", NVS_READWRITE, &h.value) != ESP_OK) return false;
        h.opened = true;
        uint8_t checked[record_size]{}; size = sizeof(checked);
        if (nvs_get_blob(h.value, "identity", checked, &size) != ESP_OK ||
            size != sizeof(checked) || std::memcmp(checked, record, sizeof(record))) return false;
    } else return false;
    std::memcpy(out, record + 10, 6);
    return true;
}
