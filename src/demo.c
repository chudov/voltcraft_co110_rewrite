/*
 * Display demo: draws a typical CO-110 main screen with synthetic data.
 *   UP      next chart zoom (5 s, 5 min, 2 h, 0.5 d per division)
 *   DOWN    next chart quantity (CO2, RH, temperature)
 *   MENU    next menu underline (none, ALARM, PLANT, LOG, CALI, unit, ADV)
 *   ENTER   short: alarm muted on/off; long (1 s): human <-> plant preset
 * The CO2 value counts up by itself so the digits change; the chart column
 * and the bell blink at 0.5 s.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "lcd.h"
#include "backlight.h"
#include "display.h"

#define U DT_PATH(zephyr_user)
#define GPIO_OUT(name) GPIO_DT_SPEC_GET(U, name)

static const struct gpio_dt_spec aux_pc13 = GPIO_OUT(aux_pc13_gpios);
static const struct gpio_dt_spec aux_pa2 = GPIO_OUT(aux_pa2_gpios);
static const struct gpio_dt_spec buzzer = GPIO_OUT(buzzer_gpios);
static const struct gpio_dt_spec sensor_power = GPIO_OUT(sensor_power_gpios);

enum { KB_UP, KB_DOWN, KB_MENU, KB_ENTER, KB_COUNT };
static const struct gpio_dt_spec keys[KB_COUNT] = {
	GPIO_OUT(up_gpios), GPIO_OUT(down_gpios), GPIO_OUT(menu_gpios), GPIO_OUT(enter_gpios),
};

#define TICK_MS 10
#define LONG_TICKS 100

int main(void)
{
	gpio_pin_configure_dt(&buzzer, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&sensor_power, GPIO_OUTPUT_INACTIVE);
	backlight_set(true);
	gpio_pin_configure_dt(&aux_pc13, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&aux_pa2, GPIO_OUTPUT_INACTIVE);
	for (int i = 0; i < KB_COUNT; i++) {
		gpio_pin_configure_dt(&keys[i], GPIO_INPUT);
	}
	if (lcd_init() != 0) {
		return 0;
	}

	int zoom = 0, quantity = DISP_Q_CO2, menu = 0, preset = 0;
	bool muted = false, fahrenheit = false;
	int co2 = 842;
	int held[KB_COUNT] = { 0 };
	bool long_done[KB_COUNT] = { false };
	uint32_t tick = 0;

	while (1) {
		for (int k = 0; k < KB_COUNT; k++) {
			bool pressed = gpio_pin_get_dt(&keys[k]) > 0;

			if (pressed) {
				held[k]++;
				if (k == KB_ENTER && held[k] == LONG_TICKS) {
					preset = preset ? 0 : 1;
					long_done[k] = true;
				}
			} else {
				if (held[k] > 2 && !long_done[k]) {
					switch (k) {
					case KB_UP:    zoom = (zoom + 1) % 4; break;
					case KB_DOWN:  quantity = (quantity + 1) % 3; break;
					case KB_MENU:  menu = (menu + 1) % 7; break;
					default:      muted = !muted; break;
					}
				}
				held[k] = 0;
				long_done[k] = false;
			}
		}

		if (tick % 100 == 0) { /* once per second */
			co2 = (co2 + 7) % 2000;
		}
		bool blink = (tick / 50) % 2;

		/* synthetic 17-sample history, column 0 = newest */
		int hist[DISP_CHART_COLS];
		for (int c = 0; c < DISP_CHART_COLS; c++) {
			int tri = (c * 3 + zoom * 5) % 24;
			tri = tri > 12 ? 24 - tri : tri;
			switch (quantity) {
			case DISP_Q_CO2:  hist[c] = 420 + tri * 40 + (c == 0 ? co2 % 40 : 0); break;
			case DISP_Q_RH:   hist[c] = 30 + tri; break;
			default:          hist[c] = 195 + tri * 4; break;
			}
		}

		disp_clear();
		disp_co2(co2, DISP_CO2_VALUE);
		disp_temp(235, fahrenheit);
		disp_rh(45);
		disp_chart(hist, DISP_CHART_COLS, quantity, fahrenheit, 5, blink);
		disp_zoom(zoom);
		disp_menu(menu);
		disp_alarm(true, muted);
		disp_preset(preset);
		lcd_flush();

		tick++;
		k_msleep(TICK_MS);
	}
	return 0;
}
