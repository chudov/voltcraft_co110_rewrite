/*
 * CO-110 board init.
 *
 * RULE FOR EVERY FIRMWARE ON THIS BOARD: the debug port must stay alive in
 * Sleep/Stop/Standby. NRST is not reachable on the CO-110, so an image that
 * sleeps (Zephyr idle = WFI) without these bits makes the ST-Link fail with
 * "Unable to get core ID" and the chip can only be recovered via BOOT0.
 * Doing it here, before the kernel starts, covers every application.
 */
#include <zephyr/init.h>
#include <soc.h>

static int co110_debug_in_low_power(void)
{
	DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
	return 0;
}

SYS_INIT(co110_debug_in_low_power, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
