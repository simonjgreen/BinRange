#pragma once

#include "esp_idf_version.h"
#include "esp_console.h"

// Upstream uses the IDF 6 console helper. IDF 5.4 exposes the generic API.
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
#define esp_console_deregister_help_command() esp_console_cmd_deregister("help")
#endif
