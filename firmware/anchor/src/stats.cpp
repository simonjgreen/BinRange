#include "stats.h"
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t mtx;

// Rolling window of successful exchanges for mean/sd/min/max and for the
// averaged link-quality figures.
struct Sample { float d, rssi, fp, ppm; };
static Sample win[STATS_WINDOW];
static uint32_t win_head, win_count;

// Rolling window of attempt outcomes, so success_pct reflects recent
// conditions rather than being diluted by lifetime history.
static uint8_t att[STATS_WINDOW];   // 1 = ok, 0 = fail
static uint32_t att_head, att_count;

static float chart[CHART_POINTS];
static uint16_t chart_head, chart_count;

static uint32_t c_ok, c_fail[FAIL_COUNT];
static float last_d, last_rssi, last_fp, last_ppm;
static uint32_t last_ms;

// Rate is measured over a sliding time base rather than assumed from the
// configured interval, so a struggling link reports its true throughput.
static uint32_t rate_window_start, rate_window_ok;
static float rate_hz;

void stats_init() {
  mtx = xSemaphoreCreateMutex();
  stats_reset();
}

void stats_reset() {
  if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
  win_head = win_count = att_head = att_count = 0;
  chart_head = chart_count = 0;
  c_ok = 0;
  for (int i = 0; i < FAIL_COUNT; i++) c_fail[i] = 0;
  last_d = last_rssi = last_fp = last_ppm = NAN;
  last_ms = 0;
  rate_window_start = millis();
  rate_window_ok = 0;
  rate_hz = 0;
  if (mtx) xSemaphoreGive(mtx);
}

static void push_chart(float v) {
  chart[chart_head] = v;
  chart_head = (chart_head + 1) % CHART_POINTS;
  if (chart_count < CHART_POINTS) chart_count++;
}

static void push_attempt(uint8_t ok) {
  att[att_head] = ok;
  att_head = (att_head + 1) % STATS_WINDOW;
  if (att_count < STATS_WINDOW) att_count++;
}

static void tick_rate() {
  uint32_t now = millis();
  uint32_t elapsed = now - rate_window_start;
  if (elapsed >= 2000) {
    rate_hz = (rate_window_ok * 1000.0f) / (float)elapsed;
    rate_window_start = now;
    rate_window_ok = 0;
  }
}

void stats_add_ok(float dist, float rssi, float fp, float ppm) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  win[win_head] = {dist, rssi, fp, ppm};
  win_head = (win_head + 1) % STATS_WINDOW;
  if (win_count < STATS_WINDOW) win_count++;
  push_attempt(1);
  push_chart(dist);
  c_ok++;
  rate_window_ok++;
  last_d = dist; last_rssi = rssi; last_fp = fp; last_ppm = ppm; last_ms = millis();
  tick_rate();
  xSemaphoreGive(mtx);
}

void stats_add_fail(FailReason r) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (r < FAIL_COUNT) c_fail[r]++;
  push_attempt(0);
  push_chart(NAN);
  tick_rate();
  xSemaphoreGive(mtx);
}

void stats_snapshot(Snapshot *o) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  o->ok = c_ok;
  for (int i = 0; i < FAIL_COUNT; i++) o->fail[i] = c_fail[i];

  o->win_n = win_count;
  {
    float s = 0, mn = INFINITY, mx = -INFINITY;
    uint32_t nd = 0;
    float sr = 0, sf = 0, sp = 0;
    uint32_t nr = 0, nf = 0, np = 0;
    for (uint32_t i = 0; i < win_count; i++) {
      float v = win[i].d;
      if (isfinite(v)) { s += v; nd++; if (v < mn) mn = v; if (v > mx) mx = v; }
      if (isfinite(win[i].rssi)) { sr += win[i].rssi; nr++; }
      if (isfinite(win[i].fp))   { sf += win[i].fp;   nf++; }
      if (isfinite(win[i].ppm))  { sp += win[i].ppm;  np++; }
    }
    if (nd) {
      o->mean = s / nd;
      float acc = 0;
      for (uint32_t i = 0; i < win_count; i++)
        if (isfinite(win[i].d)) { float d = win[i].d - o->mean; acc += d * d; }
      // Sample standard deviation (n-1); with one sample it is undefined.
      o->sd = nd > 1 ? sqrtf(acc / (nd - 1)) : 0.0f;
      o->dmin = mn; o->dmax = mx;
    } else {
      o->mean = o->sd = o->dmin = o->dmax = NAN;
    }
    // Most recent FAST_N successes, walking back from the write head.
    const uint32_t FAST_N = 16;
    uint32_t fn = win_count < FAST_N ? win_count : FAST_N;
    float fr = 0, ff = 0; uint32_t fnr = 0, fnf = 0;
    for (uint32_t i = 1; i <= fn; i++) {
      uint32_t idx = (win_head + STATS_WINDOW - i) % STATS_WINDOW;
      if (isfinite(win[idx].rssi)) { fr += win[idx].rssi; fnr++; }
      if (isfinite(win[idx].fp))   { ff += win[idx].fp;   fnf++; }
    }
    o->fast_rssi = fnr ? fr / fnr : NAN;
    o->fast_fp   = fnf ? ff / fnf : NAN;
    o->fast_n    = fn;

    o->mean_rssi = nr ? sr / nr : NAN;
    o->mean_fp   = nf ? sf / nf : NAN;
    o->mean_ppm  = np ? sp / np : NAN;
  }

  if (att_count) {
    uint32_t good = 0;
    for (uint32_t i = 0; i < att_count; i++) good += att[i];
    o->success_pct = (good * 100.0f) / att_count;
  } else {
    o->success_pct = NAN;
  }

  o->rate = rate_hz;
  o->last_dist = last_d;
  o->last_rssi = last_rssi;
  o->last_fp = last_fp;
  o->last_ppm = last_ppm;
  o->last_age_ms = last_ms ? (millis() - last_ms) : 0xFFFFFFFF;

  // Emit chart oldest-first so the UI can plot it directly.
  o->chart_n = chart_count;
  uint16_t start = (chart_count == CHART_POINTS) ? chart_head : 0;
  for (uint16_t i = 0; i < chart_count; i++)
    o->chart[i] = chart[(start + i) % CHART_POINTS];

  xSemaphoreGive(mtx);
}

uint32_t stats_counter_ok() { return c_ok; }

uint32_t stats_counter_fail(FailReason r) {
    return r < FAIL_COUNT ? c_fail[r] : 0;
}
