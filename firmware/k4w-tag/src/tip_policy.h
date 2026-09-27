#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Milli-g reference, learned only during first stable upright installation.
 * Persist as one record; never replace a valid reference merely on reboot. */
typedef struct br_tip_record {
    int32_t upright[3];
    uint32_t count;
} br_tip_record;
typedef struct br_tip_state {
    br_tip_record record;
    bool calibrated, armed, recent, sampled, fast;
    uint64_t last_sample, last_tip;
    int32_t reference[3], sum[3];
    uint8_t calibration_samples, inverted_samples, upright_samples;
} br_tip_state;
bool br_tip_record_valid(const br_tip_record *record);
void br_tip_init(br_tip_state *state, const br_tip_record *record);
/* True means a changed persistent record (calibration or new event). */
bool br_tip_sample(br_tip_state *state, int32_t x, int32_t y, int32_t z,
                   uint64_t now_ms, bool quiet);
void br_tip_invalid(br_tip_state *state);
uint32_t br_tip_interval(const br_tip_state *state, bool moving);
/* UINT32_MAX: no event since boot. No wall clock is inferred on the tag. */
uint32_t br_tip_age_s(const br_tip_state *state, uint64_t now_ms);
#ifdef __cplusplus
}
#endif
