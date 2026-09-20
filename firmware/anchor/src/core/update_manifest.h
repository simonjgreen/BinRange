#pragma once

#include <cstddef>

#include "core/update_job.h"

// Decodes the fixed release manifest published for a signed K4W image.
// Malformed input always leaves out zeroed.
bool parse_update_manifest(const char *json, size_t length, UpdateRelease &out);
