/*
 * Live display: SHT30 (temperature, humidity) and CM1106S (CO2) on the LCD.
 *
 * Start-up like the original firmware (about 37 s):
 *   0-2 s    all LCD segments and all LEDs on, one 200 ms beep
 *   2-3 s    LD1
 *   3-5 s    LD2 / LD3, "Lo" and the low CO2 threshold
 *   5-7 s    LD4 / all LEDs, "Hi" and the high CO2 threshold
 *   7-37 s   LEDs off, 30 s countdown in the CO2 digits (sensors already run)
 * then the main screen.
 *
 * Keys on the main screen:
 *   DOWN          next chart quantity (CO2, RH, temperature)
 *   MENU          switch degC / degF
 *   UP + DOWN     hold 1 s: toggle dark mode (LEDs dimmed by software PWM)
 * LEDs and alarm: zone of the selected preset (default 0 = human: < 800 green, < 1200 yellow,
 * otherwise red + repeating alarm beep 100 ms / 1 s). Every accepted key gives a 50 ms click and
 * silences the alarm until the next CO2 reading. The backlight stays on for 63 s after a key
 * press (the first press on a dark screen only wakes it) and while an alarm is active.
 * Schedule like the original firmware: SHT30 every 4 s, CM1106S every 5 s (which is
 * also the chart sample period); the CM1106S is power cycled after 40 failed polls.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "lcd.h"
#include "display.h"
#include "sensors.h"
#include "leds.h"
#include "alarm.h"

#define U DT_PATH(zephyr_user)
#define GPIO_OUT(name) GPIO_DT_SPEC_GET(U, name)

static const struct gpio_dt_spec backlight = GPIO_OUT(backlight_gpios);
static const struct gpio_dt_spec aux_pc13 = GPIO_OUT(aux_pc13_gpios);
static const struct gpio_dt_spec aux_pa2 = GPIO_OUT(aux_pa2_gpios);
static const struct gpio_dt_spec buzzer = GPIO_OUT(buzzer_gpios);
static const struct gpio_dt_spec sensor_power = GPIO_OUT(sensor_power_gpios);

enum { KB_UP, KB_DOWN, KB_MENU, KB_COUNT };
static const struct gpio_dt_spec keys[KB_COUNT] = {
	GPIO_OUT(up_gpios), GPIO_OUT(down_gpios), GPIO_OUT(menu_gpios),
};

#define TICK_MS 10
#define CO2_PERIOD_MS 5000
#define SHT_PERIOD_MS 4000
#define CO2_REPLY_WAIT_MS 300
#define CO2_MAX_FAILS 40
#define POWER_OFF_MS 500
#define COMBO_TICKS 100 /* UP + DOWN held for 1 s */

#define BACKLIGHT_MS 63000
#define CLICK_MS 50
#define CLICK_LOCK_MS 200
#define ALARM_ON_MS 100
#define ALARM_OFF_MS 1000

static int hist[3][DISP_CHART_COLS]; /* [quantity][0 = newest] */
static int hist_count;

static void push_history(int co2, int rh, int tenths)
{
	for (int q = 0; q < 3; q++) {
		for (int c = DISP_CHART_COLS - 1; c > 0; c--) {
			hist[q][c] = hist[q][c - 1];
		}
	}
	hist[DISP_Q_CO2][0] = co2;
	hist[DISP_Q_RH][0] = rh;
	hist[DISP_Q_TEMP][0] = tenths;
	if (hist_count < DISP_CHART_COLS) {
		hist_count++;
	}
}

static int c_to_f_tenths(int c_tenths)
{
	return c_tenths * 9 / 5 + 320;
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
	} else if (elapsed_ms < 37000) {
		leds_set(0);
		disp_co2_countdown(30 - (int)((elapsed_ms - 7000) / 1000));
	} else {
		return false;
	}
	lcd_flush();
	return true;
}

int main(void)
{
	gpio_pin_configure_dt(&buzzer, GPIO_OUTPUT_ACTIVE); /* start-up beep, off after 200 ms */
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
	cm1106_init();

	int co2 = 0, rh = 0, tenths = 0;
	bool co2_valid = false, th_valid = false;
	int co2_fails = 0, sht_fails = 0;
	int quantity = DISP_Q_CO2;
	bool fahrenheit = false;
	int held[KB_COUNT] = { 0 };
	bool suppress[KB_COUNT] = { false };
	int combo = 0;

	int64_t t0 = k_uptime_get();
	int64_t next_co2 = t0 + 2000; /* give the sensor time to start */
	int64_t next_sht = t0 + 500;
	int64_t co2_reply_at = 0;
	int64_t power_on_at = 0;
	bool co2_pending = false;
	int preset = 0;
	bool alarm_enabled = true;
	bool alarm_req = false;
	enum zone zone = ZONE_NONE;
	int64_t wake_until = t0 + BACKLIGHT_MS;
	int64_t click_end = 0, lock_end = 0, alarm_next = 0, alarm_beep_end = 0;
	bool alarm_beeping = false;
	bool bl_on = true;

	while (1) {
		int64_t now = k_uptime_get();

		/* keys: act on release of a short press; UP + DOWN held = dark mode */
		bool pressed[KB_COUNT];

		for (int k = 0; k < KB_COUNT; k++) {
			pressed[k] = gpio_pin_get_dt(&keys[k]) > 0;
		}
		if (pressed[KB_UP] && pressed[KB_DOWN]) {
			suppress[KB_UP] = suppress[KB_DOWN] = true;
			if (++combo == COMBO_TICKS) {
				leds_set_dark(!leds_is_dark());
				wake_until = now + BACKLIGHT_MS;
			}
		} else {
			combo = 0;
		}
		for (int k = 0; k < KB_COUNT; k++) {
			if (pressed[k]) {
				held[k]++;
			} else {
				if (held[k] > 2 && !suppress[k]) {
					wake_until = now + BACKLIGHT_MS;
					if (bl_on) { /* first key on a dark screen only wakes it */
						if (k == KB_DOWN) {
							quantity = (quantity + 1) % 3;
						} else if (k == KB_MENU) {
							fahrenheit = !fahrenheit;
						}
						click_end = now + CLICK_MS;
						lock_end = now + CLICK_LOCK_MS;
						alarm_req = false; /* silence until the next reading */
					}
				}
				held[k] = 0;
				suppress[k] = false;
			}
		}

		/* sensor power cycle in progress */
		if (power_on_at != 0 && now >= power_on_at) {
			gpio_pin_set_dt(&sensor_power, 1);
			power_on_at = 0;
			next_co2 = now + 2000;
		}

		/* SHT30 */
		if (now >= next_sht) {
			next_sht = now + SHT_PERIOD_MS;
			int t, h;

			if (sht30_measure(&t, &h) == 0) {
				tenths = t;
				rh = h;
				th_valid = true;
				sht_fails = 0;
			} else if (++sht_fails >= 5) {
				sht30_recover_bus();
				sht_fails = 0;
			}
		}

		/* CM1106S request / reply, and one chart sample per request */
		if (power_on_at == 0 && !co2_pending && now >= next_co2) {
			next_co2 = now + CO2_PERIOD_MS;
			cm1106_request();
			co2_reply_at = now + CO2_REPLY_WAIT_MS;
			co2_pending = true;
		}
		if (co2_pending && now >= co2_reply_at) {
			int ppm;

			co2_pending = false;
			if (cm1106_get_result(&ppm)) {
				co2 = ppm;
				co2_valid = true;
				co2_fails = 0;
				zone = alarm_evaluate(co2, preset, &alarm_req);
			} else if (++co2_fails >= CO2_MAX_FAILS) {
				co2_valid = false;
				zone = ZONE_NONE;
				alarm_req = false;
				co2_fails = CO2_MAX_FAILS;
				gpio_pin_set_dt(&sensor_power, 0); /* power cycle the sensor */
				power_on_at = now + POWER_OFF_MS;
			}
			/* the chart only collects samples after the start-up sequence */
			if (now - t0 >= 37000 && co2_valid && th_valid) {
				push_history(co2, rh, tenths);
			}
		}

		/* buzzer: start-up beep, key click, repeating alarm */
		bool alarm_active = alarm_req && alarm_enabled;
		bool buz = (now - t0) < 200 || now < click_end;

		if (!alarm_active) {
			alarm_beeping = false;
		} else if (now >= lock_end) {
			if (!alarm_beeping && now >= alarm_next) {
				alarm_beeping = true;
				alarm_beep_end = now + ALARM_ON_MS;
			} else if (alarm_beeping && now >= alarm_beep_end) {
				alarm_beeping = false;
				alarm_next = now + ALARM_OFF_MS;
			}
			buz = buz || alarm_beeping;
		}
		gpio_pin_set_dt(&buzzer, buz);

		/* backlight */
		bl_on = now < wake_until || alarm_active;
		gpio_pin_set_dt(&backlight, bl_on);

		if (startup_screen(now - t0, preset)) {
			k_msleep(TICK_MS);
			continue;
		}

		/* zone LEDs */
		leds_set((zone == ZONE_OK ? LED_LD4 : 0) | (zone == ZONE_WARN ? LED_LD3 : 0) |
			 (zone == ZONE_ALERT ? LED_LD2 : 0) | LED_LD1);

		/* display */
		int shown_t = fahrenheit ? c_to_f_tenths(tenths) : tenths;
		int chart[DISP_CHART_COLS];

		for (int c = 0; c < hist_count; c++) {
			int v = hist[quantity][c];

			chart[c] = (quantity == DISP_Q_TEMP && fahrenheit) ? c_to_f_tenths(v) : v;
		}

		disp_clear();
		if (co2_fails >= CO2_MAX_FAILS) {
			disp_co2(0, DISP_CO2_ERROR);
		} else if (!co2_valid) {
			disp_co2(0, DISP_CO2_DASHES);
		} else {
			disp_co2(co2, DISP_CO2_VALUE);
		}
		if (th_valid) {
			disp_temp(shown_t, fahrenheit);
			disp_rh(rh);
		}
		disp_chart(chart, hist_count, quantity, fahrenheit, -1, false);
		disp_zoom(0);
		disp_alarm(true, !alarm_enabled);
		disp_preset(preset);
		lcd_flush();

		k_msleep(TICK_MS);
	}
	return 0;
}
