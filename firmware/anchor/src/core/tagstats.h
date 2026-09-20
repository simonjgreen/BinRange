#pragma once
#include <cstdint>

// Per-tag window. Sized for a burst plus margin; a tag reports a handful of
// exchanges per event, not a continuous stream.
#define TAGSTATS_WINDOW 32

struct RangeSample {
    float dist;   // metres
    float rssi;   // dBm, relative indicator only (DGC term unavailable)
    float fp;     // dBm, first path
    float ppm;    // clock offset
};

struct TagSummary {
    uint32_t n;
    float mean, sd, dmin, dmax;
    float rssi, fp, gap;   // gap = rssi - fp, the NLOS discriminator
};

class TagStats {
  public:
    TagStats() { reset(); }
    void add(const RangeSample &s);
    void reset();
    void summarise(TagSummary *out) const;
    uint32_t count() const { return count_; }

  private:
    RangeSample win_[TAGSTATS_WINDOW];
    uint32_t head_, count_;
};
