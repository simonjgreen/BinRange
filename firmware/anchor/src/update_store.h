#pragma once

#include <cstdint>

#include "core/update_storage.h"

// ESP32-backed singleton for the anchor main loop. Call begin() once from the
// controller; all other methods are owned by that same loop. No worker should
// retain acquired bytes after release().
UpdateStorage &update_store();

// Diagnostics for the NVS adapter. available entries are an estimate of flash
// space, not a guarantee that a future snapshot write will fit.
int32_t update_store_last_nvs_error();
uint32_t update_store_nvs_free_entries();
