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
#define FN_POLL  0x21
#define FN_RESP  0x10
#define FN_FINAL 0x23
#define FINAL_MSG_POLL_TX_TS_IDX  10
#define FINAL_MSG_RESP_RX_TS_IDX  14
#define FINAL_MSG_FINAL_TX_TS_IDX 18
#define FINAL_MSG_BATT_MV_IDX     22
#define FINAL_MSG_FLAGS_IDX       24
#define FINAL_MSG_MISSES_IDX      25
#define FINAL_MSG_WAKE_COUNT_IDX  27
#define FINAL_FLAG_MOVING         0x01
#define FINAL_FLAG_SENSOR_FAULT   0x02
#define FINAL_LEN_LEGACY          24
#define FINAL_LEN_WITH_BATT       26
#define FINAL_LEN_WITH_TELEMETRY  29
#define FINAL_LEN_WITH_WAKE_COUNT 33

// A tag that reports itself moving is expected to report often, so silence
// means a lost link rather than a bin that has not moved. Without this, a
// dropout mid-collection is indistinguishable from a parked bin.
#define MOVING_STALE_S 20

// Open the FINAL receive window early: its preamble starts ~1016 uus before
// the programmed RMARKER on the Long profile.
#define RESP_TX_TO_FINAL_RX_DLY_UUS 100
#define FINAL_RX_TIMEOUT_UUS        8000

// ---- Anchor identity ----
#define ANCHOR_ADDR   0x4157        // 'W','A'
#define ANCHOR_ID     "a"
// Default liveness threshold. A motion-woken tag is silent because it has not
// moved, not because it is broken, so this must be generous - several times
// the tag's idle tick. Overridable per tag in the adoption record.
#define DEFAULT_STALE_AFTER_S 21600   // 6 hours
#define BURST_IDLE_MS 1500          // a burst has ended after this much quiet

// Defaults; antenna delay is runtime-tunable and persisted in NVS.
#define DEFAULT_ANT_DLY      16356
#define DEFAULT_INTERVAL_MS  100

// Rolling window used for the live statistics and the chart.
#define STATS_WINDOW 256
#define CHART_POINTS 120

#include "core/tagstats.h"
// One completed exchange, handed from the radio task to core 0.
struct RangeEvent {
    uint16_t addr;
    RangeSample sample;
    uint32_t at_ms;
    uint16_t batt_mv;   // 0 explicitly means unknown
    uint16_t misses;    // cumulative failed local exchanges since tag boot
    uint32_t wake_count; // diagnostic only; resets when the tag reboots
    bool moving;        // the tag's own motion state
    bool sensor_fault;
    bool has_battery;   // includes the older battery-only 26-byte layout
    bool has_telemetry; // 29-byte telemetry (or newer) was present
    bool has_wake_count;
};
