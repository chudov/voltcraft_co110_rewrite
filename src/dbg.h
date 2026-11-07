#ifndef DBG_H_
#define DBG_H_

#include <soc.h>

/*
 * Keep the debug port alive in Sleep/Stop/Standby. Without this the core's
 * clock is gated every time Zephyr's idle thread executes WFI, and the ST-Link
 * can no longer connect ("Unable to get core ID") unless it can pull NRST.
 */
static inline void debug_in_low_power(void)
{
	DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
}

#endif
