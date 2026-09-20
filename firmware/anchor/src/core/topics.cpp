#include "core/topics.h"
#include <cstdio>
#include <cstring>

// snprintf always NUL-terminates within n, so truncation is safe by
// construction. Buffers are sized by callers.

void topic_anchor_status(char *out, size_t n, const char *anchor) {
    snprintf(out, n, BINRANGE_TOPIC_BASE "/anchor/%s/status", anchor);
}

void topic_anchor_state(char *out, size_t n, const char *anchor) {
    snprintf(out, n, BINRANGE_TOPIC_BASE "/anchor/%s/state", anchor);
}

void topic_anchor_cmd_wildcard(char *out, size_t n, const char *anchor) {
    snprintf(out, n, BINRANGE_TOPIC_BASE "/anchor/%s/cmd/+", anchor);
}

void topic_tag_state(char *out, size_t n, const char *tag, const char *anchor) {
    snprintf(out, n, BINRANGE_TOPIC_BASE "/tag/%s/anchor/%s/state", tag, anchor);
}

void topic_tag_config(char *out, size_t n, const char *tag) {
    snprintf(out, n, BINRANGE_TOPIC_BASE "/tag/%s/config", tag);
}

void topic_tag_config_wildcard(char *out, size_t n) {
    snprintf(out, n, BINRANGE_TOPIC_BASE "/tag/+/config");
}

void topic_discovery(char *out, size_t n, const char *component,
                     const char *object_id) {
    snprintf(out, n, BINRANGE_DISCOVERY_PREFIX "/%s/%s/config", component,
             object_id);
}

void tag_id_to_hex(char *out, size_t n, uint16_t addr) {
    snprintf(out, n, "%04x", addr);
}

static bool hex4(const char *p, uint16_t *out) {
    uint16_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        uint16_t d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

bool parse_tag_config_topic(const char *topic, uint16_t *addr) {
    static const char PRE[] = BINRANGE_TOPIC_BASE "/tag/";
    static const char SUF[] = "/config";
    size_t pre = sizeof(PRE) - 1;
    if (strncmp(topic, PRE, pre) != 0) return false;
    // Exactly four hex digits then the suffix, and nothing after it.
    if (strlen(topic) < pre + 4) return false;
    if (strcmp(topic + pre + 4, SUF) != 0) return false;
    return hex4(topic + pre, addr);
}
