#pragma once
#include <cstddef>
#include <cstdint>

// Every MQTT topic in the system is built here. Arduino-free so it can be
// unit tested on the host.
#define BINRANGE_TOPIC_BASE "binrange"
#define BINRANGE_DISCOVERY_PREFIX "homeassistant"

void topic_anchor_status(char *out, size_t n, const char *anchor);
void topic_anchor_state(char *out, size_t n, const char *anchor);
void topic_anchor_cmd_wildcard(char *out, size_t n, const char *anchor);
void topic_tag_state(char *out, size_t n, const char *tag, const char *anchor);
void topic_tag_config(char *out, size_t n, const char *tag);
void topic_tag_config_wildcard(char *out, size_t n);
void topic_discovery(char *out, size_t n, const char *component,
                     const char *object_id);
void tag_id_to_hex(char *out, size_t n, uint16_t addr);

// Extracts the tag address from "binrange/tag/<4 hex>/config".
// Returns false for any other topic shape, including malformed hex.
bool parse_tag_config_topic(const char *topic, uint16_t *addr);
