#include "core/tagpayload.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Tracks whether a separator is needed, which is what the hand-rolled version
// kept getting wrong.
struct Obj {
    char *out;
    size_t n, len;
    bool first;
    bool ok;

    explicit Obj(char *o, size_t cap) : out(o), n(cap), len(0), first(true), ok(true) {
        put("{");
    }
    void put(const char *s) {
        size_t l = strlen(s);
        if (!ok || len + l >= n) { ok = false; return; }
        memcpy(out + len, s, l);
        len += l;
        out[len] = '\0';
    }
    void sep() {
        if (!first) put(",");
        first = false;
    }
    void key(const char *k) {
        sep();
        char b[48];
        snprintf(b, sizeof(b), "\"%s\":", k);
        put(b);
    }
    void num(const char *k, float v, int dp) {
        key(k);
        if (std::isfinite(v)) {
            char b[32];
            snprintf(b, sizeof(b), "%.*f", dp, v);
            put(b);
        } else {
            put("null");    // JSON has no NaN literal
        }
    }
    void uint(const char *k, uint32_t v) {
        key(k);
        char b[16];
        snprintf(b, sizeof(b), "%u", (unsigned)v);
        put(b);
    }
    void null(const char *k) { key(k); put("null"); }
    void boolean(const char *k, bool v) { key(k); put(v ? "true" : "false"); }
    void str(const char *k, const char *v) {
        key(k);
        put("\"");
        put(v ? v : "");
        put("\"");
    }
    size_t close() {
        put("}");
        return ok ? len : 0;
    }
};

}  // namespace

size_t tag_state_json(char *out, size_t n, const TagState &s) {
    const TagSummary &t = *s.summary;
    Obj o(out, n);
    o.num("d", t.mean + s.offset, 3);
    o.num("sd", t.sd, 3);
    o.num("rssi", t.rssi, 1);
    o.num("fp", t.fp, 1);
    o.num("gap", t.gap, 1);
    o.uint("n", t.n);

    // Received FINALs cannot establish an end-to-end success denominator.
    o.null("ok");
    if (s.misses_known) o.uint("misses", s.misses); else o.null("misses");
    if (s.motion_known) o.boolean("moving", s.moving); else o.null("moving");
    if (s.sensor_fault_known) o.boolean("sensor_fault", s.sensor_fault);
    else o.null("sensor_fault");
    if (s.wake_count_known) o.uint("wake_count", s.wake_count);
    else o.null("wake_count");
    o.boolean("stale", s.stale);
    if (s.absence_known) o.boolean("missing", s.absent); else o.null("missing");
    if (s.received) o.uint("age", s.age_s); else o.null("age");
    if (s.received && s.iso_time && s.iso_time[0]) o.str("ts", s.iso_time);
    else o.null("ts");
    if (s.batt_known && s.batt_mv) o.num("bat", s.batt_mv / 1000.0f, 3);
    else o.null("bat");
    return o.close();
}
