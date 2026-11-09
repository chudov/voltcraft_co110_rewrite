/* Application side of the LEDs: standard Zephyr LED API (driver: led_swpwm.c). */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include "leds.h"

#define LEDS_NODE DT_NODELABEL(leds)
#define IDX(label) DT_NODE_CHILD_IDX(DT_NODELABEL(label))

static const struct device *const led_dev = DEVICE_DT_GET(LEDS_NODE);

/* LED API index of LD4, LD3, LD2, LD1 (bit order of leds_set) */
static const uint8_t idx[4] = {
	IDX(led_green), IDX(led_yellow), IDX(led_red), IDX(led_power),
};

/* brightness in percent of a lit LED in dark mode (4/32 = 12 %) */
static const uint8_t DARK_PCT[4] = { 12, 12, 12, 12 };

static unsigned int wanted;
static bool dark;

static void apply(void)
{
	for (int i = 0; i < 4; i++) {
		uint8_t pct = (wanted & BIT(i)) ? (dark ? DARK_PCT[i] : 100) : 0;

		(void)led_set_brightness(led_dev, idx[i], pct);
	}
}

void leds_init(void)
{
	wanted = 0;
	apply();
}

void leds_set(unsigned int mask)
{
	mask &= LED_ALL;
	if (mask != wanted) {
		wanted = mask;
		apply();
	}
}

void leds_set_dark(bool d)
{
	if (d != dark) {
		dark = d;
		apply();
	}
}

bool leds_is_dark(void)
{
	return dark;
}
