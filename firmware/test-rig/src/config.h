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

// Defaults; antenna delay is runtime-tunable and persisted in NVS.
#define DEFAULT_ANT_DLY      16385
#define DEFAULT_INTERVAL_MS  100

// Rolling window used for the live statistics and the chart.
#define STATS_WINDOW 256
#define CHART_POINTS 120
