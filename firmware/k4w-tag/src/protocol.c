#include "protocol.h"
#include <string.h>

uint32_t br_final_schedule(uint64_t response_rx) {
    /* Zephyr's SPI port needs more margin than the original ESP32 port.
     * FINAL remains inside the unchanged anchor's 8000-UUS receive window. */
    return (uint32_t)((response_rx + 4500ULL * 65536) >> 8);
}
uint64_t br_final_timestamp(uint32_t scheduled, uint16_t antenna_delay) {
    return ((uint64_t)(scheduled & 0xfffffffeUL) << 8) + antenna_delay;
}

static void header(uint8_t *out, uint16_t tag, uint8_t sequence, uint8_t function) {
    const uint8_t prefix[10] = {0x41,0x88,sequence,0xca,0xde,0x57,0x41,
                                (uint8_t)tag,(uint8_t)(tag>>8),function};
    memcpy(out, prefix, sizeof(prefix));
}
void br_poll(uint8_t out[12], uint16_t tag, uint8_t sequence) {
    memset(out, 0, 12);
    header(out, tag, sequence, 0x21);
}
static void le32(uint8_t *out, uint32_t value) {
    for (unsigned i=0; i<4; i++) out[i] = (uint8_t)(value >> (8*i));
}
void br_final(uint8_t out[BR_FINAL_LEN], uint16_t tag, uint8_t sequence,
              uint32_t poll_tx, uint32_t response_rx, uint32_t final_tx) {
    memset(out, 0, BR_FINAL_LEN);
    header(out, tag, sequence, 0x23);
    le32(out+10, poll_tx);
    le32(out+14, response_rx);
    le32(out+18, final_tx);
}
void br_final_telemetry(uint8_t out[BR_FINAL_TELEMETRY_LEN], uint16_t tag,
                        uint8_t sequence, uint32_t poll_tx,
                        uint32_t response_rx, uint32_t final_tx,
                        uint16_t batt_mv, uint8_t flags, uint16_t misses,
                        uint32_t wake_count) {
    memset(out, 0, BR_FINAL_TELEMETRY_LEN);
    header(out, tag, sequence, 0x23);
    le32(out + 10, poll_tx);
    le32(out + 14, response_rx);
    le32(out + 18, final_tx);
    out[22] = (uint8_t)batt_mv;
    out[23] = (uint8_t)(batt_mv >> 8);
    out[24] = flags;
    out[25] = (uint8_t)misses;
    out[26] = (uint8_t)(misses >> 8);
    le32(out + 27, wake_count);
}
bool br_response_valid(const uint8_t *frame, size_t length, uint16_t tag) {
    if (!frame || length != 14) return false;
    const uint8_t expected[10] = {0x41,0x88,frame[2],0xca,0xde,
                                  (uint8_t)tag,(uint8_t)(tag>>8),0x57,0x41,0x10};
    return memcmp(frame, expected, sizeof(expected)) == 0;
}
