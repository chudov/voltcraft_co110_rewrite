/*
 * LEDs with a software PWM (dark mode). A 100 us timer steps through 32 phases
 * (about 310 Hz); in dark mode every lit LED is on for DARK_DUTY[i] of the 32 phases.
 * Temporary solution, a hardware PWM solution may replace it later.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "leds.h"

#define PWM_STEPS 32

/* on-time out of 32 in dark mode, index = bit number (LD4, LD3, LD2, LD1) */
static const uint8_t DARK_DUTY[4] = { 4, 4, 4, 4 };

static const struct gpio_dt_spec leds[4] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_green), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_yellow), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_power), gpios),
};

static volatile unsigned int wanted;
static volatile bool dark;
static uint8_t step;
static int8_t last[4] = { -1, -1, -1, -1 };

static void pwm_tick(struct k_timer *t)
{
	ARG_UNUSED(t);

	for (int i = 0; i < 4; i++) {
		bool on = false;

		if (wanted & BIT(i)) {
			on = dark ? (step < DARK_DUTY[i]) : true;
		}
		if (last[i] != (int8_t)on) {
			gpio_pin_set_dt(&leds[i], on);
			last[i] = (int8_t)on;
		}
	}
	step = (step + 1) % PWM_STEPS;
}

K_TIMER_DEFINE(pwm_timer, pwm_tick, NULL);

void leds_init(void)
{
	for (int i = 0; i < 4; i++) {
		gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
	}
	k_timer_start(&pwm_timer, K_USEC(100), K_USEC(100));
}

void leds_set(unsigned int mask)
{
	wanted = mask & LED_ALL;
}

void leds_set_dark(bool d)
{
	dark = d;
}

bool leds_is_dark(void)
{
	return dark;
}
