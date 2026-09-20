#include <assert.h>
#include "update_commands.h"
int main(void) {
    assert(br_ota_command_allowed(1, 0, 0, true)); /* image list */
    assert(br_ota_command_allowed(1, 1, 2, true)); /* upload */
    assert(!br_ota_command_allowed(1, 1, 2, false)); /* preserve rollback image */
    assert(!br_ota_command_allowed(1, 0, 2, true)); /* remote confirmation */
    assert(!br_ota_command_allowed(1, 5, 2, true)); /* arbitrary erase */
    assert(br_ota_command_allowed(0, 5, 2, true)); /* reset */
    assert(br_ota_command_allowed(64, 0, 0, false)); /* diagnostics in trial */
    assert(br_ota_command_allowed(64, 1, 2, true)); /* test, never permanent */
    assert(!br_ota_command_allowed(64, 1, 2, false));
    assert(!br_ota_command_allowed(64, 1, 0, true));
    assert(br_ota_command_allowed(64, 2, 0, false)); /* inspect motion configuration */
    assert(br_ota_command_allowed(64, 2, 2, true)); /* authenticated, confirmed only */
    assert(!br_ota_command_allowed(64, 2, 2, false)); /* trial cannot persist tuning */
    assert(!br_ota_command_allowed(64, 3, 2, true));
    assert(!br_ota_command_allowed(3, 0, 2, true)); /* settings/shell forbidden */
    assert(!br_ota_command_allowed(65535, 255, 255, true));
    return 0;
}
