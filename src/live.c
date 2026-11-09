/*
 * CO-110 application: SHT30 + CM1106S on the LCD, alarm, menu and stored settings.
 *
 * Start-up like the original firmware (about 37 s):
 *   0-2 s    all LCD segments and all LEDs on, one 200 ms beep
 *   2-3 s    LD1
 *   3-5 s    LD2 / LD3, "Lo" and the low CO2 threshold
 *   5-7 s    LD4 / all LEDs, "Hi" and the high CO2 threshold
 *   7-37 s   LEDs off, 30 s countdown in the CO2 digits (sensors already run)
 *
 * Keys (see ui.c): UP, DOWN, MENU, ENTER; UP + DOWN held for 1 s toggles dark mode
 * (LEDs dimmed by software PWM). MENU/ENTER long = 6 s like the original.
 * Schedule like the original firmware: SHT30 every 4 s, CM1106S every 5 s (which is
 * also the chart sample period); the CM1106S is power cycled after 40 failed polls.
 * Every accepted key gives a 50 ms click and silences the alarm until the next CO2 reading;
 * the backlight stays on 63 s after a key (the first key on a dark screen only wakes it).
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/buzzer.h>
#include "lcd.h"
#include "display.h"
#include "sensors.h"
#include "leds.h"
#include "alarm.h"
#include "settings.h"
#include "ui.h"
#include "history.h"

#define U DT_PATH(zephyr_user)
#define GPIO_OUT(name) GPIO_DT_SPEC_GET(U, name)

static const struct gpio_dt_spec backlight = GPIO_OUT(backlight_gpios);
static const struct gpio_dt_spec aux_pc13 = GPIO_OUT(aux_pc13_gpios);
static const struct gpio_dt_spec aux_pa2 = GPIO_OUT(aux_pa2_gpios);
static const struct device *const buzzer = DEVICE_DT_GET(DT_NODELABEL(buzzer));
static const struct gpio_dt_spec sensor_power = GPIO_OUT(sensor_power_gpios);

enum { KB_UP, KB_DOWN, KB_MENU, KB_ENTER, KB_COUNT };
static const struct gpio_dt_spec keys[KB_COUNT] = {
	GPIO_OUT(up_gpios), GPIO_OUT(down_gpios), GPIO_OUT(menu_gpios), GPIO_OUT(enter_gpios),
};

#define TICK_MS 10
#define CO2_PERIOD_MS 5000
#define SHT_PERIOD_MS 4000
#define CO2_REPLY_WAIT_MS 300
#define CO2_MAX_FAILS 40
#define POWER_OFF_MS 500
#define STARTUP_MS 37000

#define SHORT_MS 40          /* shorter touches are ignored */
#define REPEAT_START_MS 600  /* UP/DOWN auto repeat after 0.6 s ... */
#define REPEAT_EVERY_MS 40   /* ... every 40 ms */
#define LONG_MS 6000         /* MENU/ENTER long press: 6 s as measured on the original */
#define COMBO_MS 1000        /* UP + DOWN for dark mode */

#define BACKLIGHT_MS 63000
#define CLICK_MS 50
#define CLICK_LOCK_MS 200
#define ALARM_BEEP_MS 100
#define ALARM_PERIOD_MS 1100 /* 100 ms on, 1 s off */

static int clamp_rh(int v)
{
	return v < 0 ? 0 : (v > 99 ? 99 : v);
}

/* start-up screen, returns true while the start-up sequence is running */
static bool startup_screen(int64_t elapsed_ms, int preset)
{
	int th_low, th_high;

	alarm_thresholds(preset, &th_low, &th_high);
	disp_clear();

	if (elapsed_ms < 2000) {
		lcd_fill(0xFF);
		leds_set(LED_ALL);
	} else if (elapsed_ms < 3000) {
		leds_set(LED_LD1);
	} else if (elapsed_ms < 7000) {
		bool low = elapsed_ms < 5000;

		leds_set(elapsed_ms < 4000 ? LED_LD2 : (elapsed_ms < 5000 ? LED_LD3 :
			 (elapsed_ms < 6000 ? LED_LD4 : LED_ALL)));
		disp_co2(low ? th_low : th_high, DISP_CO2_VALUE);
		if (low) {
			disp_rh_text('L', '0');
		} else {
			disp_rh_text('H', 'I');
		}
	} else if (elapsed_ms < STARTUP_MS) {
		leds_set(0);
		disp_co2_countdown(30 - (int)((elapsed_ms - 7000) / 1000));
	} else {
		return false;
	}
	lcd_flush_if_changed();
	return true;
}

int main(void)
{
	gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&sensor_power, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&aux_pc13, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&aux_pa2, GPIO_OUTPUT_INACTIVE);
	for (int i = 0; i < KB_COUNT; i++) {
		gpio_pin_configure_dt(&keys[i], GPIO_INPUT);
	}
	leds_init();
	if (lcd_init() != 0) {
		leds_set(LED_ALL);
		return 0;
	}
	settings_init();
	history_load();
	cm1106_init();
	buzzer_beep(buzzer, 200); /* start-up beep */

	int sht_fails = 0, co2_fails = 0;
	int64_t down_at[KB_COUNT] = { -1, -1, -1, -1 };
	int64_t next_repeat[KB_COUNT] = { 0 };
	bool fired[KB_COUNT] = { false }; /* long press or auto repeat already used */
	bool suppress[KB_COUNT] = { false };
	int64_t combo_at = -1;
	bool combo_done = false;

	int64_t t0 = k_uptime_get();
	int64_t next_co2 = t0 + 2000; /* give the sensor time to start */
	int64_t next_sht = t0 + 500;
	int64_t co2_reply_at = 0;
	int64_t power_on_at = 0;
	bool co2_pending = false;

	bool alarm_req = false;
	int64_t wake_until = t0 + BACKLIGHT_MS;
	int64_t lock_end = 0, alarm_next = 0;
	bool bl_on = true;

	while (1) {
		int64_t now = k_uptime_get();

		/* ---- keys (all timings measured with the clock, not with loop counts) ---- */
		bool pressed[KB_COUNT];
		enum ui_event ev[2 * KB_COUNT];
		int nev = 0;

		for (int k = 0; k < KB_COUNT; k++) {
			pressed[k] = gpio_pin_get_dt(&keys[k]) > 0;
		}
		if (pressed[KB_UP] && pressed[KB_DOWN]) {
			suppress[KB_UP] = suppress[KB_DOWN] = true;
			if (combo_at < 0) {
				combo_at = now;
			} else if (!combo_done && now - combo_at >= COMBO_MS) {
				combo_done = true;
				leds_set_dark(!leds_is_dark());
				wake_until = now + BACKLIGHT_MS;
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
					ev[nev++] = e_short;
				} else if (!updown && !fired[k] && now - down_at[k] >= LONG_MS) {
					fired[k] = true;
					ev[nev++] = k == KB_MENU ? EV_MENU_LONG : EV_ENTER_LONG;
				}
			} else {
				if (down_at[k] >= 0 && now - down_at[k] >= SHORT_MS && !fired[k] &&
				    !suppress[k]) {
					ev[nev++] = e_short;
				}
				down_at[k] = -1;
				fired[k] = false;
				suppress[k] = false;
			}
		}
		for (int i = 0; i < nev; i++) {
			wake_until = now + BACKLIGHT_MS;
			if (!bl_on) {
				continue; /* the first key on a dark screen only wakes it */
			}
			if (ui_event(ev[i], now)) {
				buzzer_beep(buzzer, CLICK_MS);
				lock_end = now + CLICK_LOCK_MS;
				alarm_req = false; /* silence until the next reading */
			}
		}
		ui_tick(now);

		/* ---- sensors ---- */
		if (power_on_at != 0 && now >= power_on_at) {
			gpio_pin_set_dt(&sensor_power, 1);
			power_on_at = 0;
			next_co2 = now + 2000;
		}

		if (now >= next_sht) {
			next_sht = now + SHT_PERIOD_MS;
			int t, h;

			if (sht30_measure(&t, &h) == 0) {
				ud.t_raw = t;
				ud.rh_raw = h;
				ud.th_valid = true;
				sht_fails = 0;
			} else if (++sht_fails >= 5) {
				sht30_recover_bus();
				sht_fails = 0;
			}
		}

		if (ui_calibration_due()) {
			(void)cm1106_calibrate(400);
			next_co2 = k_uptime_get() + 2000;
			co2_pending = false;
		}

		if (power_on_at == 0 && !co2_pending && !ui_calibrating() && now >= next_co2) {
			next_co2 = now + CO2_PERIOD_MS;
			cm1106_request();
			co2_reply_at = now + CO2_REPLY_WAIT_MS;
			co2_pending = true;
		}
		if (co2_pending && now >= co2_reply_at) {
			int ppm;

			co2_pending = false;
			if (cm1106_get_result(&ppm)) {
				ud.co2 = ppm;
				ud.co2_valid = true;
				ud.co2_error = false;
				co2_fails = 0;
				bool req;

				alarm_evaluate(ppm, g_set.preset, &req);
				alarm_req = req;
				/* the chart only collects samples after the start-up sequence */
				if (now - t0 >= STARTUP_MS && ud.th_valid) {
					history_push(ppm, clamp_rh(ud.rh_raw + g_set.rh_offset),
						     ud.t_raw + g_set.temp_offset);
				}
			} else if (++co2_fails >= CO2_MAX_FAILS) {
				ud.co2_valid = false;
				ud.co2_error = true;
				alarm_req = false;
				co2_fails = CO2_MAX_FAILS;
				gpio_pin_set_dt(&sensor_power, 0); /* power cycle the sensor */
				power_on_at = now + POWER_OFF_MS;
			}
		}

		/* ---- repeating alarm beep (the buzzer API stops each beep by itself) ---- */
		bool alarm_active = alarm_req && g_set.alarm_on;

		if (alarm_active && now >= lock_end && now >= alarm_next) {
			buzzer_beep(buzzer, ALARM_BEEP_MS);
			alarm_next = now + ALARM_PERIOD_MS;
		}

		/* ---- backlight ---- */
		bl_on = now < wake_until || alarm_active;
		gpio_pin_set_dt(&backlight, bl_on);

		/* ---- display ---- */
		if (startup_screen(now - t0, g_set.preset)) {
			k_msleep(TICK_MS);
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

		k_msleep(TICK_MS);
	}
	return 0;
}
