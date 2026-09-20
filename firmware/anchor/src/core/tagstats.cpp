#include "core/tagstats.h"
#include <cmath>

void TagStats::reset() {
    head_ = 0;
    count_ = 0;
}

void TagStats::add(const RangeSample &s) {
    win_[head_] = s;
    head_ = (head_ + 1) % TAGSTATS_WINDOW;
    if (count_ < TAGSTATS_WINDOW) count_++;
}

void TagStats::summarise(TagSummary *o) const {
    o->n = count_;
    if (count_ == 0) {
        o->mean = o->sd = o->dmin = o->dmax = NAN;
        o->rssi = o->fp = o->gap = NAN;
        return;
    }

    float sum = 0, mn = INFINITY, mx = -INFINITY, srssi = 0, sfp = 0;
    uint32_t nd = 0;
    for (uint32_t i = 0; i < count_; i++) {
        const RangeSample &s = win_[i];
        // The anchor records no distance of its own for some exchange types,
        // so distance stats must skip non-finite entries while power stats
        // still use every sample.
        if (std::isfinite(s.dist)) {
            sum += s.dist;
            if (s.dist < mn) mn = s.dist;
            if (s.dist > mx) mx = s.dist;
            nd++;
        }
        srssi += s.rssi;
        sfp += s.fp;
    }

    if (nd) {
        o->mean = sum / nd;
        float acc = 0;
        for (uint32_t i = 0; i < count_; i++) {
            if (!std::isfinite(win_[i].dist)) continue;
            float d = win_[i].dist - o->mean;
            acc += d * d;
        }
        // Sample standard deviation; undefined for a single sample, reported 0.
        o->sd = nd > 1 ? sqrtf(acc / (nd - 1)) : 0.0f;
        o->dmin = mn;
        o->dmax = mx;
    } else {
        o->mean = o->sd = o->dmin = o->dmax = NAN;
    }

    o->rssi = srssi / count_;
    o->fp = sfp / count_;
    o->gap = o->rssi - o->fp;
}
