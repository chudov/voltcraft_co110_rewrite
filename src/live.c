/*
 * CO-110 application, UI thread (= main thread). It owns the display and all UI state and is
 * driven by messages (app.h):
 *   keys               input.c      (GPIO interrupts + work item)
 *   SHT30 / CM1106S    sensor_tasks.c (one thread each, 4 s / 5 s)
 *   backlight, alarm   k_timers started and stopped here
 * The thread sleeps in k_msgq_get() with a 100 ms time-out, which is the tick for blinking,
 * the start-up countdown, the calibration countdown and the menu time-out. The LCD is only
 * written when the frame buffer changed.
 *
 * Start-up like the original firmware (about 37 s):
 *   0-2 s    all LCD segments and all LEDs on, one 200 ms beep
 *   2-3 s    LD1
 *   3-5 s    LD2 / LD3, "Lo" and the low CO2 threshold
 *   5-7 s    LD4 / all LEDs, "Hi" and the high CO2 threshold
 *   7-37 s   LEDs off, 30 s countdown in the CO2 digits (sensors already run)
 * Keys: see ui.c; UP + DOWN held for 1 s toggles dark mode.
 * Every accepted key gives a 50 ms click (not in dark mode) and silences the alarm until the next CO2 reading;
 * the backlight stays on 63 s after a key (the first key on a dark screen only wakes it).
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/buzzer.h>
#include "lcd.h"
#include "backlight.h"
#include "display.h"
#include "leds.h"
#include "alarm.h"
#include "settings.h"
#include "history.h"
#include "ui.h"
#include "startup.h"
#include "app.h"
#include "watchdog.h"

static const struct device *const buzzer = DEVICE_DT_GET(DT_NODELABEL(buzzer));

#define UI_TICK_MS 100
#define BACKLIGHT_MS 63000
#define CLICK_MS 50
#define CLICK_LOCK_MS 200
#define ALARM_BEEP_MS 100
#define ALARM_PERIOD_MS 1100 /* 100 ms on, 1 s off */

K_MSGQ_DEFINE(app_q, sizeof(struct msg), 16, 4);

static void bl_timer_fn(struct k_timer *t)
{
	ARG_UNUSED(t);
	post(MSG_BACKLIGHT_OFF, 0, 0);
}
K_TIMER_DEFINE(bl_timer, bl_timer_fn, NULL);

static void alarm_timer_fn(struct k_timer *t)
{
	ARG_UNUSED(t);
	post(MSG_ALARM_BEEP, 0, 0);
}
K_TIMER_DEFINE(alarm_timer, alarm_timer_fn, NULL);

static int64_t t0;
static bool alarm_req;      /* the zone logic asks for the alarm beep */
static bool bl_timed;       /* backlight on because of recent activity */
static bool bl_on = true;   /* backlight pin state */
static int64_t lock_end;    /* no alarm beep before this time (key click lockout) */

static int clamp_rh(int v)
{
	return v < 0 ? 0 : (v > 99 ? 99 : v);
}

static void wake_backlight(void)
{
	bl_timed = true;
	k_timer_start(&bl_timer, K_MSEC(BACKLIGHT_MS), K_NO_WAIT);
}

static void handle(const struct msg *m, int64_t now)
{
	switch (m->type) {
	case MSG_KEY:
		wake_backlight();
		if (!bl_on) {
			break; /* the first key on a dark screen only wakes it */
		}
		if (ui_event((enum ui_event)m->a, now)) {
			if (!g_set.dark_mode) { /* dark mode: no key click */
				buzzer_beep(buzzer, CLICK_MS);
				lock_end = now + CLICK_LOCK_MS;
			}
			alarm_req = false; /* silence until the next reading */
		}
		break;

	case MSG_COMBO:
		g_set.dark_mode = !g_set.dark_mode; /* stored, survives a power cycle */
		co110_settings_save();
		wake_backlight();
		break;

	case MSG_TH:
		ud.t_raw = m->a;
		ud.rh_raw = m->b;
		ud.th_valid = true;
		break;

	case MSG_CO2: {
		bool req;

		ud.co2 = m->a;
		ud.co2_valid = true;
		ud.co2_error = false;
		alarm_evaluate(m->a, g_set.preset, &req);
		alarm_req = req;
		/* the chart only collects samples after the start-up sequence */
		if (!startup_active() && ud.th_valid) {
			history_push(m->a, clamp_rh(ud.rh_raw + g_set.rh_offset),
				     ud.t_raw + g_set.temp_offset);
		}
		break;
	}

	case MSG_CO2_LOST:
		ud.co2_valid = false;
		ud.co2_error = true;
		alarm_req = false;
		break;

	case MSG_BACKLIGHT_OFF:
		bl_timed = false;
		break;

	case MSG_ALARM_BEEP:
		if (alarm_req && g_set.alarm_on && now >= lock_end) {
			buzzer_beep(buzzer, ALARM_BEEP_MS);
		}
		break;
	}
}

int main(void)
{
	backlight_set(true);
	leds_init();
	if (lcd_init() != 0) {
		leds_set(LED_ALL);
		return 0;
	}
	co110_settings_init();
	history_load();
	leds_set_dark(g_set.dark_mode);
	input_init();
	ui_init();

	t0 = k_uptime_get();
	if (!g_set.dark_mode) { /* dark mode: no start-up beep */
		buzzer_beep(buzzer, 200);
	}
	wake_backlight();
	startup_init(t0);

	bool alarm_timer_on = false;
	int wdt = wdt_channel(3000);

	while (1) {
		wdt_feed(wdt);
		struct msg m;
		int rc = k_msgq_get(&app_q, &m, K_MSEC(UI_TICK_MS));
		int64_t now = k_uptime_get();

		while (rc == 0) {
			handle(&m, now);
			rc = k_msgq_get(&app_q, &m, K_NO_WAIT);
		}
		now = k_uptime_get();

		leds_set_dark(g_set.dark_mode); /* also follows a factory reset */
		ui_tick(now);
		if (ui_calibration_due()) {
			k_sem_give(&cali_sem); /* the CM1106S thread sends the 400 ppm calibration */
		}
		co2_suspended = ui_calibrating();

		/* periodic alarm beep while the alarm is active */
		bool alarm_active = alarm_req && g_set.alarm_on && !startup_active();

		if (alarm_active && !alarm_timer_on) {
			alarm_timer_on = true;
			k_timer_start(&alarm_timer, K_MSEC(ALARM_PERIOD_MS), K_MSEC(ALARM_PERIOD_MS));
			if (now >= lock_end) {
				buzzer_beep(buzzer, ALARM_BEEP_MS);
			}
		} else if (!alarm_active && alarm_timer_on) {
			k_timer_stop(&alarm_timer);
			alarm_timer_on = false;
		}

		/* backlight: recent activity or active alarm */
		bool want_bl = bl_timed || alarm_active;

		if (want_bl != bl_on) {
			backlight_set(want_bl);
			bl_on = want_bl;
		}

		/* display and zone LEDs */
		startup_tick(now);
		if (startup_active()) {
			startup_render(g_set.preset);
			lcd_flush_if_changed();
			continue;
		}

		enum zone zone = ZONE_NONE;

		if (ud.co2_valid) {
			bool dummy;

			zone = alarm_evaluate(ud.co2, g_set.preset, &dummy);
		}
		leds_set((zone == ZONE_OK ? LED_LD4 : 0) | (zone == ZONE_WARN ? LED_LD3 : 0) |
			 (zone == ZONE_ALERT ? LED_LD2 : 0) | LED_LD1);

		ui_render(now);
		lcd_flush_if_changed();
	}
	return 0;
}
