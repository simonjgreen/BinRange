#include "tip_policy.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static int64_t norm(const int32_t v[3]) {
    int64_t n = 0;
    for (unsigned i=0; i<3; ++i) {
        if (v[i] < -2000 || v[i] > 2000) return 0;
        n += (int64_t)v[i] * v[i];
    }
    return n;
}
static bool gravity(const int32_t v[3]) {
    const int64_t n = norm(v);
    return n >= 750LL*750 && n <= 1250LL*1250;
}
bool br_tip_record_valid(const br_tip_record *r) { return r && gravity(r->upright); }
void br_tip_init(br_tip_state *s, const br_tip_record *r) {
    memset(s, 0, sizeof(*s));
    if (br_tip_record_valid(r)) { s->record = *r; s->calibrated = true; }
    /* A restarted, already-inverted bin must first return upright to rearm. */
}
void br_tip_invalid(br_tip_state *s) {
    s->calibration_samples = s->inverted_samples = s->upright_samples = 0;
}
bool br_tip_sample(br_tip_state *s, int32_t x, int32_t y, int32_t z,
                   uint64_t now, bool quiet) {
    const int32_t v[3] = {x,y,z};
    if (s->sampled && now <= s->last_sample) { br_tip_invalid(s); return false; }
    if (s->sampled && now - s->last_sample < 40) return false;
    if (s->sampled && now - s->last_sample > 120) br_tip_invalid(s);
    s->sampled = true;
    s->last_sample = now;
    if (!gravity(v)) { br_tip_invalid(s); return false; }
    if (!s->calibrated) {
        if (!quiet) { br_tip_invalid(s); return false; }
        if (s->calibration_samples) {
            for (unsigned i=0; i<3; ++i) {
                int32_t d = v[i] - s->reference[i];
                if (d < -80 || d > 80) { s->calibration_samples = 0; break; }
            }
        }
        if (!s->calibration_samples) {
            memcpy(s->reference, v, sizeof(v));
            memset(s->sum, 0, sizeof(s->sum));
        }
        for (unsigned i=0; i<3; ++i) s->sum[i] += v[i];
        if (++s->calibration_samples < 50) return false;
        for (unsigned i=0; i<3; ++i) s->record.upright[i] = s->sum[i] / 50;
        s->calibrated = s->armed = true;
        return true;
    }
    int64_t dot = 0;
    for (unsigned i=0; i<3; ++i) dot += (int64_t)v[i] * s->record.upright[i];
    const bool upright = dot > 0 && 2*dot*dot >= norm(v)*norm(s->record.upright);
    /* An idle low-rate sample can promote sampling before a slow tip crosses
     * 90 degrees. Once tipped, low-rate samples save power until returning. */
    s->fast = s->armed ? !upright : dot > 0;
    if (s->armed) {
        s->upright_samples = 0;
        if (dot >= 0) { s->inverted_samples = 0; return false; }
        if (++s->inverted_samples < 3) return false;
        s->armed = false;
        s->inverted_samples = 0;
        s->fast = false;
        if (s->record.count == UINT32_MAX) return false;
        ++s->record.count;
        s->recent = true;
        s->last_tip = now;
        return true;
    }
    s->inverted_samples = 0;
    if (!upright) { s->upright_samples = 0; return false; }
    if (++s->upright_samples >= 5) {
        s->armed = true;
        s->upright_samples = 0;
        s->fast = false;
    }
    return false;
}
uint32_t br_tip_interval(const br_tip_state *s, bool moving) {
    return moving || !s->calibrated || s->fast ? 40 : 1000;
}
uint32_t br_tip_age_s(const br_tip_state *s, uint64_t now) {
    if (!s->recent || now < s->last_tip) return UINT32_MAX;
    uint64_t seconds = (now - s->last_tip) / 1000;
    return seconds >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)seconds;
}
