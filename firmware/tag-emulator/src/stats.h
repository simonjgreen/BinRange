#pragma once
#include <Arduino.h>
#include "config.h"

enum FailReason : uint8_t { FAIL_TIMEOUT = 0, FAIL_RX_ERROR, FAIL_BAD_FRAME, FAIL_COUNT };

struct Snapshot {
  uint32_t ok, fail[FAIL_COUNT];
  uint32_t win_n;
  float mean, sd, dmin, dmax;
  float rate, success_pct;
  // Averaged over the rolling window. Single-exchange power readings are far
  // too noisy to calibrate against, so the averages are what callers want.
  float mean_rssi, mean_fp, mean_ppm;
  // Short average over the most recent exchanges. The window average lags far
  // too much to aim an antenna by; this one responds in a couple of seconds.
  float fast_rssi, fast_fp;
  uint32_t fast_n;
  float last_dist, last_rssi, last_fp, last_ppm;
  uint32_t last_age_ms;
  uint16_t chart_n;
  float chart[CHART_POINTS];
};

void stats_init();
void stats_add_ok(float dist, float rssi, float fp, float ppm);
void stats_add_fail(FailReason r);
void stats_reset();
void stats_snapshot(Snapshot *out);
