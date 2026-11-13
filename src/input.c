/*
 * Touch keys (outputs of the capacitive controller, active low).
 * A GPIO interrupt on every edge starts a work item that scans the keys; while a key is down it
 * reschedules itself every 10 ms (long press, auto repeat), otherwise nothing runs.
 *   UP / DOWN   event on release (>= 40 ms), auto repeat after 0.6 s every 40 ms
 *   MENU / ENTER short on release (>= 40 ms), long after 6 s
 *   UP + DOWN   held 1 s: MSG_COMBO (dark mode), no UP/DOWN events for that touch
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "app.h"
#include "ui.h"

#define U DT_PATH(zephyr_user)
#define GPIO_KEY(name) GPIO_DT_SPEC_GET(U, name)

enum { KB_UP, KB_DOWN, KB_MENU, KB_ENTER, KB_COUNT };
static const struct gpio_dt_spec keys[KB_COUNT] = {
	GPIO_KEY(up_gpios), GPIO_KEY(down_gpios), GPIO_KEY(menu_gpios), GPIO_KEY(enter_gpios),
};

#define SCAN_MS 10
#define SHORT_MS 40
#define REPEAT_START_MS 600
#define REPEAT_EVERY_MS 40
#define LONG_MS 6000
#define COMBO_MS 1000

static struct k_work_delayable scan_work;
static struct gpio_callback key_cb;

static int64_t down_at[KB_COUNT] = { -1, -1, -1, -1 };
static int64_t next_repeat[KB_COUNT];
static bool fired[KB_COUNT];
static bool suppress[KB_COUNT];
static int64_t combo_at = -1;
static bool combo_done;

static void scan(struct k_work *work)
{
	ARG_UNUSED(work);

	int64_t now = k_uptime_get();
	bool pressed[KB_COUNT];
	bool busy = false;

	for (int k = 0; k < KB_COUNT; k++) {
		pressed[k] = gpio_pin_get_dt(&keys[k]) > 0;
	}

	if (pressed[KB_UP] && pressed[KB_DOWN]) {
		suppress[KB_UP] = suppress[KB_DOWN] = true;
		if (combo_at < 0) {
			combo_at = now;
		} else if (!combo_done && now - combo_at >= COMBO_MS) {
			combo_done = true;
			post(MSG_COMBO, 0, 0);
		}
	} else {
		combo_at = -1;
		combo_done = false;
	}

	for (int k = 0; k < KB_COUNT; k++) {
		bool updown = (k == KB_UP || k == KB_DOWN);
		enum ui_event e_short = k == KB_UP ? EV_UP : k == KB_DOWN ? EV_DOWN :
					k == KB_MENU ? EV_MENU : EV_ENTER;

		if (pressed[k]) {
			busy = true;
			if (down_at[k] < 0) {
				down_at[k] = now;
				next_repeat[k] = now + REPEAT_START_MS;
			}
			if (suppress[k]) {
				continue;
			}
			if (updown && now >= next_repeat[k]) {
				fired[k] = true;
				next_repeat[k] = now + REPEAT_EVERY_MS;
				post(MSG_KEY, e_short, 0);
			} else if (!updown && !fired[k] && now - down_at[k] >= LONG_MS) {
				fired[k] = true;
				post(MSG_KEY, k == KB_MENU ? EV_MENU_LONG : EV_ENTER_LONG, 0);
			}
		} else {
			if (down_at[k] >= 0 && now - down_at[k] >= SHORT_MS && !fired[k] &&
			    !suppress[k]) {
				post(MSG_KEY, e_short, 0);
			}
			down_at[k] = -1;
			fired[k] = false;
			suppress[k] = false;
		}
	}

	if (busy) {
		k_work_reschedule(&scan_work, K_MSEC(SCAN_MS));
	}
}

static void key_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_reschedule(&scan_work, K_NO_WAIT);
}

int input_init(void)
{
	uint32_t mask = 0;

	k_work_init_delayable(&scan_work, scan);
	for (int k = 0; k < KB_COUNT; k++) {
		if (!gpio_is_ready_dt(&keys[k])) {
			return -ENODEV;
		}
		gpio_pin_configure_dt(&keys[k], GPIO_INPUT);
		mask |= BIT(keys[k].pin);
	}
	/* all four keys are on the same GPIO port */
	gpio_init_callback(&key_cb, key_isr, mask);
	gpio_add_callback(keys[0].port, &key_cb);
	for (int k = 0; k < KB_COUNT; k++) {
		gpio_pin_interrupt_configure_dt(&keys[k], GPIO_INT_EDGE_BOTH);
	}
	return 0;
}
