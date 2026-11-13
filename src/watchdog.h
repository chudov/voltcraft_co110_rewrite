#ifndef WATCHDOG_H_
#define WATCHDOG_H_

/*
 * Task watchdog (Zephyr task_wdt) on top of the IWDG. Every thread registers a channel with its
 * maximum loop time and feeds it once per loop; if one thread stops feeding, or the whole
 * system hangs, the device resets. The IWDG is paused while a debugger halts the core.
 */

/* returns a channel id (>= 0) or a negative errno; wdt_feed() ignores negative ids */
int wdt_channel(int period_ms);
void wdt_feed(int channel);

#endif
