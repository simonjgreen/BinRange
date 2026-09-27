#pragma once
#include "tip_policy.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct br_tip_monitor {
    br_tip_state policy;
    uint64_t next_sample, next_save, no_data_deadline;
    uint32_t committed_count;
    int error, load_error;
    bool healthy, dirty;
} br_tip_monitor;
typedef int (*br_tip_read_fn)(void *context, int32_t sample[3]);
typedef int (*br_tip_save_fn)(void *context, const br_tip_record *record);
void br_tip_monitor_init(br_tip_monitor *m, const br_tip_record *record, int load_error);
/* Main-loop owner; true requests an immediate report after a status/event change. */
bool br_tip_monitor_update(br_tip_monitor *m, uint64_t now, bool moving,
                            br_tip_read_fn read, br_tip_save_fn save, void *context);
bool br_tip_monitor_ready(const br_tip_monitor *m);
uint64_t br_tip_monitor_deadline(const br_tip_monitor *m);
#ifdef __cplusplus
}
#endif
