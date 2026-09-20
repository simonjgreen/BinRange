#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BR_FINAL_LEN 24
#define BR_FINAL_TELEMETRY_LEN 33
#define BR_FINAL_FLAG_MOVING       0x01
#define BR_FINAL_FLAG_SENSOR_FAULT 0x02
void br_poll(uint8_t out[12], uint16_t tag, uint8_t sequence);
void br_final(uint8_t out[BR_FINAL_LEN], uint16_t tag, uint8_t sequence,
              uint32_t poll_tx, uint32_t response_rx, uint32_t final_tx);
void br_final_telemetry(uint8_t out[BR_FINAL_TELEMETRY_LEN], uint16_t tag,
                        uint8_t sequence, uint32_t poll_tx,
                        uint32_t response_rx, uint32_t final_tx,
                        uint16_t batt_mv, uint8_t flags, uint16_t misses,
                        uint32_t wake_count);
bool br_response_valid(const uint8_t *frame, size_t length, uint16_t tag);
uint32_t br_final_schedule(uint64_t response_rx);
uint64_t br_final_timestamp(uint32_t scheduled, uint16_t antenna_delay);
#ifdef __cplusplus
}
#endif
