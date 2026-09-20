#pragma once
#include <Arduino.h>

// ---- Makerfabs ESP32 UWB DW3000 (WROVER) pin map ----
#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  4

// ---- Roles ----
enum Role : uint8_t { ROLE_INITIATOR = 0, ROLE_RESPONDER = 1 };

// ---- Frame layout (from the Makerfabs / Qorvo SS-TWR example) ----
#define ALL_MSG_COMMON_LEN      10
#define ALL_MSG_SN_IDX          2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN         4

// ---- PHY profiles ----
// SHORT is the vendor example: 128 symbol preamble at 6.8 Mbps. It is the
// shortest-range configuration the part offers.
// LONG trades data rate for link budget: 1024 symbol preamble at 850 kbps,
// worth roughly 15-18 dB, at the cost of a much longer frame.
// MAX pushes the preamble to 2048 symbols for another ~3 dB over LONG, at the
// cost of roughly 9 ms of air time per exchange.
enum Phy : uint8_t { PHY_SHORT = 0, PHY_LONG = 1, PHY_MAX = 2, PHY_COUNT };

// Timing must follow the profile. The radio schedules the preamble so the
// RMARKER lands at the programmed instant, so the responder's turnaround has
// to exceed the preamble duration (~1016 uus at PLEN_1024) or the delayed
// transmit is already in the past and fails.
struct PhyTiming {
  uint16_t poll_tx_to_resp_rx_dly;
  uint16_t resp_rx_timeout;
  uint16_t poll_rx_to_resp_tx_dly;
};
static const PhyTiming PHY_TIMING[PHY_COUNT] = {
  {600,   400,  900},   // SHORT: preamble ~127 uus
  {100,  6000, 3000},   // LONG:  preamble ~1016 uus, turnaround must exceed it
  {100, 12000, 4000},   // MAX:   preamble ~2032 uus
};
// Bounded RX window so the ranging task periodically yields to the idle task
// instead of spinning forever (the stock example spins with no timeout).
#define RESPONDER_RX_WINDOW_UUS    60000

// ---- DS-TWR ----
// Three messages: POLL (tag) -> RESP (anchor) -> FINAL (tag, carrying the
// tag's three timestamps so the anchor can compute the range).
#define FN_POLL  0x21
#define FN_RESP  0x10
#define FN_FINAL 0x23
#define FINAL_MSG_POLL_TX_TS_IDX  10
#define FINAL_MSG_RESP_RX_TS_IDX  14
#define FINAL_MSG_FINAL_TX_TS_IDX 18
// The tag reports its battery inside FINAL, so the anchor learns it as part of
// ranging rather than needing a channel of its own. Two bytes, millivolts,
// little endian. A 24 byte FINAL without them stays valid.
#define FINAL_MSG_BATT_MV_IDX     22
// Telemetry the anchor cannot observe for itself. Misses are failed polls
// since the last success: the anchor never hears those, so without this its
// success rate is measured over a denominator that excludes every failure.
#define FINAL_MSG_FLAGS_IDX       24   // bit 0: tag reports itself moving
#define FINAL_MSG_MISSES_IDX      25   // uint16 LE, saturating
#define FINAL_FLAG_MOVING         0x01

// Both turnarounds must exceed the preamble duration (~1016 uus on the Long
// profile) or the delayed transmit is already in the past and fails.
#define RESP_RX_AFTER_TX_DLY_UUS    100
#define RESP_RX_TIMEOUT_DS_UUS      6000
#define RESP_RX_TO_FINAL_TX_DLY_UUS 3000

// ---- Tag emulation ----
// A real bin tag wakes, ranges a few times and sleeps; it does not free-run.
// The anchor's queue and publication path must see that intermittency.
#define DEFAULT_TAG_ADDR   0x4556   // 'V','E'
#define DEFAULT_BURST_SIZE 8
#define DEFAULT_EVENT_MS   10000

// The emulator can impersonate several tags in turn, one burst per identity.
// This exercises the anchor's registry, per-tag statistics and per-tag burst
// detection. It does NOT test concurrency: there is still only one radio, so
// transmissions never overlap and the anchor's interleaved-FINAL guard is
// never reached.
#define MAX_TAG_IDS 8

// Motion model. A real tag sleeps until its accelerometer interrupt fires,
// ranges quickly while moving, then falls back to a slow liveness tick. Each
// identity has its own state, so one bin can be wheeled out while the rest sit
// still - which is what the anchor will actually see.
#define DEFAULT_MOTION_TICK_MS 5000     // while moving
#define DEFAULT_IDLE_TICK_MS   60000    // liveness only; 600000 in the field
#define DEFAULT_MOTION_HOLD_MS 60000    // how long motion persists after a trigger

// Simulated CR2477. Real discharge would take years, so a drain multiplier
// accelerates it for testing; 1 means real time.
#define BATT_CAPACITY_UAH   700000.0f   // 700 mAh usable
#define BATT_UAH_PER_EXCH   0.4f        // measured estimate for one DS-TWR exchange
// At the 60 s idle tick with 16 exchange bursts, real drain is ~9 mAh/day, so
// a real cell lasts months and shows nothing on a bench. 200x drains a
// simulated 700 mAh in roughly nine hours: visible across a working session
// without vanishing before you can watch it. 1 is real time.
#define DEFAULT_DRAIN_MULT  200

// Defaults; antenna delay is runtime-tunable and persisted in NVS.
#define DEFAULT_ANT_DLY      16356
#define DEFAULT_INTERVAL_MS  100

// Rolling window used for the live statistics and the chart.
#define STATS_WINDOW 256
#define CHART_POINTS 120
