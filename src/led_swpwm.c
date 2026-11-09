/*
 * LED driver with software PWM, implements the Zephyr LED API (led_on, led_off,
 * led_set_brightness 0..100) for GPIO LEDs described by "voltcraft,swpwm-leds".
 * A 100 us timer steps through 32 phases (about 310 Hz). Temporary solution, it can be
 * replaced by a hardware PWM driver without changes in the application.
 */
#define DT_DRV_COMPAT voltcraft_swpwm_leds

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>

#define PWM_STEPS 32
#define PWM_STEP_US 100

#define LED_SPEC(node_id) GPIO_DT_SPEC_GET(node_id, gpios),

static const struct gpio_dt_spec specs[] = {
	DT_INST_FOREACH_CHILD_STATUS_OKAY(0, LED_SPEC)
};
#define NUM_LEDS ARRAY_SIZE(specs)

static volatile uint8_t duty[NUM_LEDS]; /* on-time in steps, 0..PWM_STEPS */
static int8_t last[NUM_LEDS];
static uint8_t step;

static void pwm_tick(struct k_timer *t)
{
	ARG_UNUSED(t);

	for (size_t i = 0; i < NUM_LEDS; i++) {
		uint8_t d = duty[i];
		int8_t on = (d >= PWM_STEPS) ? 1 : (step < d ? 1 : 0);

		if (last[i] != on) {
			gpio_pin_set_dt(&specs[i], on);
			last[i] = on;
		}
	}
	step = (step + 1) % PWM_STEPS;
}

K_TIMER_DEFINE(pwm_timer, pwm_tick, NULL);

static int swpwm_set_brightness(const struct device *dev, uint32_t led, uint8_t value)
{
	ARG_UNUSED(dev);

	if (led >= NUM_LEDS || value > LED_BRIGHTNESS_MAX) {
		return -EINVAL;
	}
	uint32_t d = ((uint32_t)value * PWM_STEPS + 50) / 100;

	if (value != 0 && d == 0) {
		d = 1; /* any non-zero brightness stays visible */
	}
	duty[led] = (uint8_t)d;
	return 0;
}

static int swpwm_on(const struct device *dev, uint32_t led)
{
	return swpwm_set_brightness(dev, led, LED_BRIGHTNESS_MAX);
}

static int swpwm_off(const struct device *dev, uint32_t led)
{
	return swpwm_set_brightness(dev, led, 0);
}

static int swpwm_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	for (size_t i = 0; i < NUM_LEDS; i++) {
		if (!gpio_is_ready_dt(&specs[i])) {
			return -ENODEV;
		}
		gpio_pin_configure_dt(&specs[i], GPIO_OUTPUT_INACTIVE);
		last[i] = 0;
		duty[i] = 0;
	}
	k_timer_start(&pwm_timer, K_USEC(PWM_STEP_US), K_USEC(PWM_STEP_US));
	return 0;
}

static DEVICE_API(led, swpwm_api) = {
	.on = swpwm_on,
	.off = swpwm_off,
	.set_brightness = swpwm_set_brightness,
};

DEVICE_DT_INST_DEFINE(0, swpwm_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &swpwm_api);
