#pragma once
#include <cstddef>
#include "core/topics.h"

// One Home Assistant entity. Null optional fields are omitted from the JSON
// rather than emitted as null, which Home Assistant treats differently.
struct EntitySpec {
    const char *component;       // "sensor" | "binary_sensor"
    const char *key;             // stable slug, part of the unique id
    const char *name;
    const char *unit;            // nullptr to omit
    const char *device_class;    // nullptr to omit
    const char *state_class;     // nullptr to omit
    const char *value_template;
    bool diagnostic;             // sets entity_category
};

extern const EntitySpec TAG_ENTITIES[];
extern const size_t TAG_ENTITY_COUNT;

// An entity belonging to the anchor itself: its diagnostics and its controls.
// A control sets cmd_key; a button additionally has no value_template.
struct AnchorEntity {
    const char *component;       // "sensor" | "binary_sensor" | "number" | "select" | "button"
    const char *key;
    const char *name;
    const char *unit;
    const char *device_class;
    const char *state_class;
    const char *value_template;  // nullptr for buttons
    bool diagnostic;
    const char *cmd_key;         // nullptr for read-only entities
    const char *options;         // select only: pre-quoted, comma separated
};

extern const AnchorEntity ANCHOR_ENTITIES[];
extern const size_t ANCHOR_ENTITY_COUNT;

size_t discovery_anchor_entity(char *out, size_t n, const AnchorEntity &e,
                               const char *anchor, const char *fw);

void discovery_object_id(char *out, size_t n, const char *tag,
                         const char *anchor, const char *key);

// Returns the payload length, or 0 if it did not fit.
size_t discovery_tag_entity(char *out, size_t n, const EntitySpec &e,
                            const char *tag, const char *anchor,
                            const char *tag_name, const char *area,
                            const char *fw);

// Minimal field extraction for the fixed-shape adoption payloads we publish
// ourselves. Not a general JSON parser and not to be used as one.
bool json_str(const char *json, const char *key, char *out, size_t n);
bool json_float(const char *json, const char *key, float *out);
