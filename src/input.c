/*
 * Touch keys with the Zephyr input subsystem.
 *   devicetree: gpio-keys node "keys" (outputs of the capacitive controller, active low, debounced)
 *               zephyr,input-longpress node "longpress" for MENU and ENTER (short / 6 s long)
 *
 *   MENU / ENTER  longpress device: short press (event on release) or long press (event after 6 s)
 *   UP / DOWN     raw key events: event on release, auto repeat after 0.6 s every 40 ms
 *   UP + DOWN     held 1 s: MSG_COMBO (dark mode), no UP/DOWN events for that touch
 *
 * The callbacks run in the input thread, the timers are delayable work items of the system
 * work queue; the shared state is protected by a mutex. Everything is posted to the UI thread.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include "app.h"
#include "ui.h"

#define REPEAT_START_MS 600
#define REPEAT_EVERY_MS 40
#define COMBO_MS 1000

static const struct device *const keys_dev = DEVICE_DT_GET(DT_NODELABEL(keys));
static const struct device *const longpress_dev = DEVICE_DT_GET(DT_NODELABEL(longpress));

enum { KB_UP, KB_DOWN, KB_COUNT };

static struct k_mutex in_lock;
static struct k_work_delayable repeat_work[KB_COUNT];
static struct k_work_delayable combo_work;
static bool pressed[KB_COUNT];
static bool fired[KB_COUNT];    /* auto repeat has produced events, no event on release */
static bool suppress[KB_COUNT]; /* part of the UP + DOWN chord */

static enum ui_event up_down_event(int k)
{
	return k == KB_UP ? EV_UP : EV_DOWN;
}

static void repeat_fn(struct k_work *work)
{
	struct k_work_delayable *dw = k_work_delayable_from_work(work);
	int k = dw - repeat_work;

	k_mutex_lock(&in_lock, K_FOREVER);
	if (pressed[k] && !suppress[k]) {
		fired[k] = true;
		co110_post(MSG_KEY, up_down_event(k), 0);
		k_work_reschedule(dw, K_MSEC(REPEAT_EVERY_MS));
	}
	k_mutex_unlock(&in_lock);
}

static void combo_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&in_lock, K_FOREVER);
	if (pressed[KB_UP] && pressed[KB_DOWN]) {
		co110_post(MSG_COMBO, 0, 0);
	}
	k_mutex_unlock(&in_lock);
}

static void up_down_change(int k, bool down)
{
	k_mutex_lock(&in_lock, K_FOREVER);
	pressed[k] = down;
	if (down) {
		if (pressed[KB_UP] && pressed[KB_DOWN]) {
			suppress[KB_UP] = suppress[KB_DOWN] = true;
			k_work_cancel_delayable(&repeat_work[KB_UP]);
			k_work_cancel_delayable(&repeat_work[KB_DOWN]);
			k_work_reschedule(&combo_work, K_MSEC(COMBO_MS));
		} else {
			k_work_reschedule(&repeat_work[k], K_MSEC(REPEAT_START_MS));
		}
	} else {
		k_work_cancel_delayable(&repeat_work[k]);
		k_work_cancel_delayable(&combo_work);
		if (!fired[k] && !suppress[k]) {
			co110_post(MSG_KEY, up_down_event(k), 0);
		}
		fired[k] = false;
		suppress[k] = false;
	}
	k_mutex_unlock(&in_lock);
}

/* raw key events of the gpio-keys device: only UP and DOWN are used here */
static void keys_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY) {
		return;
	}
	if (evt->code == INPUT_KEY_UP) {
		up_down_change(KB_UP, evt->value != 0);
	} else if (evt->code == INPUT_KEY_DOWN) {
		up_down_change(KB_DOWN, evt->value != 0);
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(keys)), keys_cb, NULL);

/* short and long press events of MENU and ENTER (longpress device) */
static void longpress_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->value == 0) {
		return; /* releases are of no interest */
	}
	switch (evt->code) {
	case INPUT_KEY_MENU:
		co110_post(MSG_KEY, EV_MENU, 0);
		break;
	case INPUT_KEY_ENTER:
		co110_post(MSG_KEY, EV_ENTER, 0);
		break;
	case INPUT_KEY_F1:
		co110_post(MSG_KEY, EV_MENU_LONG, 0);
		break;
	case INPUT_KEY_F2:
		co110_post(MSG_KEY, EV_ENTER_LONG, 0);
		break;
	default:
		break;
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(longpress)), longpress_cb, NULL);

int co110_input_init(void)
{
	k_mutex_init(&in_lock);
	for (int k = 0; k < KB_COUNT; k++) {
		k_work_init_delayable(&repeat_work[k], repeat_fn);
	}
	k_work_init_delayable(&combo_work, combo_fn);

	if (!device_is_ready(keys_dev) || !device_is_ready(longpress_dev)) {
		return -ENODEV;
	}
	return 0;
}
