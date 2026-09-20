/* Start before application entry; MCUboot feeds this watchdog during swaps. */
#include <zephyr/init.h>
#include <hal/nrf_wdt.h>

static int recovery_watchdog_start(void) {
    if (!NRF_WDT0->RUNSTATUS) {
        NRF_WDT0->CRV = 20 * 32768 - 1;
        /* Run during sleep, pause only while a debugger explicitly halts CPU. */
        NRF_WDT0->CONFIG = WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos;
        NRF_WDT0->RREN = 1;
        NRF_WDT0->TASKS_START = 1;
    }
    NRF_WDT0->RR[0] = 0x6e524635;
    return 0;
}
SYS_INIT(recovery_watchdog_start, PRE_KERNEL_1, 0);
