#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/task_wdt/task_wdt.h>
#include "watchdog.h"

static const struct device *const hw_wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg));
static bool ready;

/* runs before the threads and main() start */
static int wdt_sysinit(void)
{
	if (device_is_ready(hw_wdt) && task_wdt_init(hw_wdt) == 0) {
		ready = true;
	}
	return 0;
}
SYS_INIT(wdt_sysinit, APPLICATION, 0);

int co110_wdt_channel(int period_ms)
{
	if (!ready) {
		return -ENODEV;
	}
	return task_wdt_add(period_ms, NULL, NULL); /* NULL: no callback, reset on expiry */
}

void co110_wdt_feed(int channel)
{
	if (channel >= 0) {
		(void)task_wdt_feed(channel);
	}
}
