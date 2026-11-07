/*
 * CO-110 LCD bit-map test.
 *
 * Start-up: LED chase, then all segments on for 2 s, then single-bit mode.
 *
 * Keys (touch pads):
 *   UP / DOWN  next / previous buffer bit (auto repeat); bit 7 -> next byte
 *   MENU       short: jump to bit 0 of the next byte
 *              long (1 s): cycle display mode  SINGLE -> CUMULATIVE -> INVERTED
 *   ENTER      short: cycle what the 4 LEDs show (beeps 1/2/3 times)
 *              long (1 s): toggle backlight
 *
 * LED field 1 (1 beep): bit index 0..7 on LD4 LD3 LD2 (LD4 = bit 0), LD1 off
 * LED field 2 (2 beeps): byte & 0x0F on LD4..LD1 (LD4 = bit 0)
 * LED field 3 (3 beeps): byte >> 4 (0..2) on LD4 LD3, LD2 always on as marker
 *
 * Write down the (byte, bit) of every segment you see light up.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "lcd.h"

#define U DT_PATH(zephyr_user)
#define GPIO_OUT(name) GPIO_DT_SPEC_GET(U, name)

static const struct gpio_dt_spec backlight = GPIO_OUT(backlight_gpios);
static const struct gpio_dt_spec buzzer = GPIO_OUT(buzzer_gpios);
static const struct gpio_dt_spec sensor_power = GPIO_OUT(sensor_power_gpios);
static const struct gpio_dt_spec aux_pc13 = GPIO_OUT(aux_pc13_gpios);
static const struct gpio_dt_spec aux_pa2 = GPIO_OUT(aux_pa2_gpios);

/* index 0..3 = LD4 (green), LD3 (yellow), LD2 (red), LD1 (power) */
static const struct gpio_dt_spec leds[4] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_green), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_yellow), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_power), gpios),
};

enum { KB_UP, KB_DOWN, KB_MENU, KB_ENTER, KB_COUNT };

static const struct gpio_dt_spec keys[KB_COUNT] = {
	GPIO_OUT(up_gpios), GPIO_OUT(down_gpios),
	GPIO_OUT(menu_gpios), GPIO_OUT(enter_gpios),
};

enum mode { M_SINGLE, M_CUMUL, M_INVERT, M_COUNT };

#define TICK_MS 10
#define LONG_TICKS 100       /* 1 s */
#define REPEAT_START 40      /* 0.4 s */
#define REPEAT_EVERY 8       /* 80 ms */
#define TOTAL_BITS (LCD_FB_SIZE * 8)

static void leds_show(unsigned int v)
{
	for (int i = 0; i < 4; i++) {
		gpio_pin_set_dt(&leds[i], (v >> i) & 1);
	}
}

static void beep(int n)
{
	for (int i = 0; i < n; i++) {
		gpio_pin_set_dt(&buzzer, 1);
		k_msleep(50);
		gpio_pin_set_dt(&buzzer, 0);
		k_msleep(150);
	}
}

static void out_init(const struct gpio_dt_spec *p, int level)
{
	gpio_pin_configure_dt(p, level ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
}

int main(void)
{
	/* board state like the original firmware after MX_GPIO_Init */
	out_init(&buzzer, 0);
	out_init(&sensor_power, 0);   /* sensor not needed for this test */
	out_init(&backlight, 1);
	out_init(&aux_pc13, 1);
	out_init(&aux_pa2, 0);
	for (int i = 0; i < 4; i++) {
		out_init(&leds[i], 0);
	}
	for (int i = 0; i < KB_COUNT; i++) {
		gpio_pin_configure_dt(&keys[i], GPIO_INPUT);
	}

	if (lcd_init() != 0) {
		leds_show(0xF); /* all LEDs on = LCD GPIO not ready */
		return 0;
	}

	/* LED chase, then all segments on */
	for (int i = 3; i >= 0; i--) {
		leds_show(BIT(i));
		k_msleep(250);
	}
	lcd_fill(0xFF);
	lcd_flush();
	leds_show(0xF);
	k_msleep(2000);

	unsigned int idx = 0;
	enum mode mode = M_SINGLE;
	int field = 0;
	bool bl = true;
	bool dirty = true;
	int held[KB_COUNT] = { 0 };
	bool long_done[KB_COUNT] = { false };

	while (1) {
		for (int k = 0; k < KB_COUNT; k++) {
			bool pressed = gpio_pin_get_dt(&keys[k]) > 0;

			if (pressed) {
				held[k]++;
				if (k == KB_UP || k == KB_DOWN) {
					bool fire = (held[k] == 1) ||
						    (held[k] > REPEAT_START &&
						     ((held[k] - REPEAT_START) % REPEAT_EVERY) == 0);
					if (fire) {
						if (k == KB_UP) {
							idx = (idx + 1) % TOTAL_BITS;
						} else {
							idx = (idx + TOTAL_BITS - 1) % TOTAL_BITS;
						}
						dirty = true;
					}
				} else if (held[k] == LONG_TICKS) {
					long_done[k] = true;
					if (k == KB_MENU) {
						mode = (mode + 1) % M_COUNT;
						dirty = true;
					} else {
						bl = !bl;
						gpio_pin_set_dt(&backlight, bl);
					}
				}
			} else {
				if (held[k] > 2 && !long_done[k]) {
					if (k == KB_MENU) {
						idx = ((idx / 8) + 1) * 8 % TOTAL_BITS;
						dirty = true;
					} else if (k == KB_ENTER) {
						field = (field + 1) % 3;
						dirty = true;
						beep(field + 1);
					}
				}
				held[k] = 0;
				long_done[k] = false;
			}
		}

		if (dirty) {
			unsigned int byte = idx / 8, bit = idx % 8;

			switch (mode) {
			case M_SINGLE:
				lcd_fill(0x00);
				lcd_set(byte, bit, true);
				break;
			case M_CUMUL:
				lcd_fill(0x00);
				for (unsigned int i = 0; i <= idx; i++) {
					lcd_set(i / 8, i % 8, true);
				}
				break;
			default:
				lcd_fill(0xFF);
				lcd_set(byte, bit, false);
				break;
			}
			lcd_flush();

			if (field == 0) {
				leds_show(bit & 7);
			} else if (field == 1) {
				leds_show(byte & 0xF);
			} else {
				leds_show((byte >> 4) | BIT(2));
			}
			dirty = false;
		}
		k_msleep(TICK_MS);
	}
	return 0;
}
