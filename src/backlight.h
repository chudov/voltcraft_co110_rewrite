#ifndef BACKLIGHT_H_
#define BACKLIGHT_H_

/* LCD backlight through the standard Zephyr LED API (gpio-leds node "backlight_leds" in the board dts) */
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>

#define BL_DEV DEVICE_DT_GET(DT_NODELABEL(backlight_leds))
#define BL_IDX DT_NODE_CHILD_IDX(DT_NODELABEL(led_backlight))

static inline void backlight_set(bool on)
{
	if (on) {
		(void)led_on(BL_DEV, BL_IDX);
	} else {
		(void)led_off(BL_DEV, BL_IDX);
	}
}

#endif
