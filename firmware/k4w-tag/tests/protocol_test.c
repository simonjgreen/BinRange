#include <assert.h>
#include <string.h>
#include "protocol.h"

int main(void) {
    /* 4500 UUS = 0x11940000 device ticks; delayed register discards 8 bits. */
    assert(br_final_schedule(0) == 0x00119400);
    assert(br_final_schedule(0xffffff0000ULL) == 0x00119300);
    assert(br_final_timestamp(0x00119401, 16356) == 0x11943fe4ULL);
    uint8_t poll[12];
    br_poll(poll, 0xb100, 7);
    const uint8_t want_poll[] = {0x41,0x88,7,0xca,0xde,0x57,0x41,0,0xb1,0x21,0,0};
    assert(!memcmp(poll, want_poll, sizeof(poll)));
    uint8_t final[24];
    br_final(final, 0xb100, 8, 0x12345678, 0x9abcdef0, 0x01020304);
    const uint8_t want_final[] = {0x41,0x88,8,0xca,0xde,0x57,0x41,0,0xb1,0x23,
        0x78,0x56,0x34,0x12,0xf0,0xde,0xbc,0x9a,4,3,2,1,0,0};
    assert(!memcmp(final, want_final, sizeof(final)));

    uint8_t telemetry[33];
    br_final_telemetry(telemetry, 0xb100, 9, 0x12345678, 0x9abcdef0, 0x01020304,
                       2950, BR_FINAL_FLAG_MOVING | BR_FINAL_FLAG_SENSOR_FAULT,
                       0x1234, 0x89abcdef);
    const uint8_t want_telemetry[] = {0x41,0x88,9,0xca,0xde,0x57,0x41,0,0xb1,0x23,
        0x78,0x56,0x34,0x12,0xf0,0xde,0xbc,0x9a,4,3,2,1,
        0x86,0x0b,0x03,0x34,0x12,0xef,0xcd,0xab,0x89,0,0};
    assert(!memcmp(telemetry, want_telemetry, sizeof(telemetry)));
    uint8_t response[] = {0x41,0x88,99,0xca,0xde,0,0xb1,0x57,0x41,0x10,2,0,0,0};
    assert(br_response_valid(response, sizeof(response), 0xb100));
    assert(!br_response_valid(response, sizeof(response), 0xb001));
    for (unsigned n=0; n<sizeof(response); n++)
        assert(!br_response_valid(response, n, 0xb100));
    assert(!br_response_valid(response, 15, 0xb100));
    assert(!br_response_valid(NULL, 14, 0xb100));
    for (unsigned i=0; i<10; i++) {
        if (i==2) continue; /* responder has its own sequence counter */
        response[i] ^= 1;
        assert(!br_response_valid(response, sizeof(response), 0xb100));
        response[i] ^= 1;
    }
    return 0;
}
