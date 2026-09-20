#pragma once

#include "core/update_controller.h"

// Bounded writer shared by the web and MQTT adapters. Overflow returns an
// empty result, never a truncated JSON document. Strings are always escaped.
class UpdateJson {
 public:
    UpdateJson(char *buffer, size_t capacity);
    void raw(const char *text);
    void str(const char *text);
    void number(uint64_t value);
    void boolean(bool value) { raw(value ? "true" : "false"); }
    void hex(const uint8_t *bytes, size_t size);
    size_t finish();
 private:
    char *buffer_;
    size_t capacity_, length_ = 0;
    bool ok_;
};

const char *update_phase_name(UpdatePhase phase);
const char *update_pairing_name(UpdatePairingPhase phase);
void update_job_json(UpdateJson &out, const UpdateControllerSnapshot &snapshot,
                     size_t index, const char *name, uint64_t now);
