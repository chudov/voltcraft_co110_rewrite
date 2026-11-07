/*
 * Live display: SHT30 (temperature, humidity) and CM1106S (CO2) on the LCD.
 *   DOWN   next chart quantity (CO2, RH, temperature)
 *   MENU   switch degC / degF
 * LEDs: zone of the human preset (< 800 green, < 1200 yellow, otherwise red).
 * Schedule like the original firmware: SHT30 every 4 s, CM1106S every 5 s (which is
 * also the chart sample period); the CM1106S is power cycled after 40 failed polls.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "lcd.h"
#include "display.h"
#include "sensors.h"

#define U DT_PATH(zephyr_user)
#define GPIO_OUT(name) GPIO_DT_SPEC_GET(U, name)

static const struct gpio_dt_spec backlight = GPIO_OUT(backlight_gpios);
static const struct gpio_dt_spec aux_pc13 = GPIO_OUT(aux_pc13_gpios);
static const struct gpio_dt_spec aux_pa2 = GPIO_OUT(aux_pa2_gpios);
static const struct gpio_dt_spec buzzer = GPIO_OUT(buzzer_gpios);
static const struct gpio_dt_spec sensor_power = GPIO_OUT(sensor_power_gpios);

/* LD4 green, LD3 yellow, LD2 red, LD1 power */
static const struct gpio_dt_spec leds[4] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_green), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_yellow), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_power), gpios),
};

enum { KB_DOWN, KB_MENU, KB_COUNT };
static const struct gpio_dt_spec keys[KB_COUNT] = {
	GPIO_OUT(down_gpios), GPIO_OUT(menu_gpios),
};

#define TICK_MS 10
#define CO2_PERIOD_MS 5000
#define SHT_PERIOD_MS 4000
#define CO2_REPLY_WAIT_MS 300
#define CO2_MAX_FAILS 40
#define POWER_OFF_MS 500

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

int main(void)
{

	gpio_pin_configure_dt(&buzzer, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&sensor_power, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&aux_pc13, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&aux_pa2, GPIO_OUTPUT_INACTIVE);
	for (int i = 0; i < 4; i++) {
		gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
	}
	for (int i = 0; i < KB_COUNT; i++) {
		gpio_pin_configure_dt(&keys[i], GPIO_INPUT);
	}
	if (lcd_init() != 0) {
		return 0;
	}
	cm1106_init();
	gpio_pin_set_dt(&leds[3], 1);

	int co2 = 0, rh = 0, tenths = 0;
	bool co2_valid = false, th_valid = false;
	int co2_fails = 0, sht_fails = 0;
	int quantity = DISP_Q_CO2;
	bool fahrenheit = false;
	int held[KB_COUNT] = { 0 };

	int64_t next_co2 = k_uptime_get() + 2000; /* give the sensor time to start */
	int64_t next_sht = k_uptime_get() + 500;
	int64_t co2_reply_at = 0;
	int64_t power_on_at = 0;
	bool co2_pending = false;

	while (1) {
		int64_t now = k_uptime_get();

		/* keys: act on release of a short press */
		for (int k = 0; k < KB_COUNT; k++) {
			if (gpio_pin_get_dt(&keys[k]) > 0) {
				held[k]++;
			} else {
				if (held[k] > 2) {
					if (k == KB_DOWN) {
						quantity = (quantity + 1) % 3;
					} else {
						fahrenheit = !fahrenheit;
					}
				}
				held[k] = 0;
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
			} else if (++co2_fails >= CO2_MAX_FAILS) {
				co2_valid = false;
				co2_fails = CO2_MAX_FAILS;
				gpio_pin_set_dt(&sensor_power, 0); /* power cycle the sensor */
				power_on_at = now + POWER_OFF_MS;
			}
			if (co2_valid && th_valid) {
				push_history(co2, rh, tenths);
			}
		}

		/* zone LEDs (human preset 0 thresholds 800 / 1200) */
		bool known = co2_valid;

		gpio_pin_set_dt(&leds[0], known && co2 < 800);
		gpio_pin_set_dt(&leds[1], known && co2 >= 800 && co2 < 1200);
		gpio_pin_set_dt(&leds[2], known && co2 >= 1200);

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
		disp_alarm(true, false);
		disp_preset(0);
		lcd_flush();

		k_msleep(TICK_MS);
	}
	return 0;
}
