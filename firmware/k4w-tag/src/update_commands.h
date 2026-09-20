#ifndef BR_UPDATE_COMMANDS_H
#define BR_UPDATE_COMMANDS_H
#include <stdbool.h>
#include <stdint.h>
/* SMP read=0, write=2. Authentication/window gating is a separate prerequisite. */
static inline bool br_ota_command_allowed(uint16_t group, uint8_t id, uint8_t op,
                                          bool confirmed) {
    if (group == 1) return (id == 0 && op == 0) || (id == 1 && op == 2 && confirmed);
    if (group == 0) return id == 5 && op == 2;
    if (group == 64) return (id == 0 && op == 0) || (id == 1 && op == 2 && confirmed) ||
        (id == 2 && (op == 0 || (op == 2 && confirmed)));
    return false;
}
#endif
