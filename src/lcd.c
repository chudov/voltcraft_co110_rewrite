/*
 * Bit-banged HT1621/HT1625-style LCD controller (VK1625), as used by the
 * original CO-110 firmware:
 *   command frame: CS low, bits 1 0 0, 8 command bits MSB first, 1 dummy bit
 *   write frame:   CS low, bits 1 0 1, 7 address bits MSB first,
 *                  then data bytes, each LSB first
 * Data is latched on the rising edge of WR.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "lcd.h"

#define U DT_PATH(zephyr_user)

static const struct gpio_dt_spec cs = GPIO_DT_SPEC_GET(U, lcd_cs_gpios);
static const struct gpio_dt_spec wr = GPIO_DT_SPEC_GET(U, lcd_wr_gpios);
static const struct gpio_dt_spec data = GPIO_DT_SPEC_GET(U, lcd_data_gpios);
static const struct gpio_dt_spec rd = GPIO_DT_SPEC_GET(U, lcd_rd_gpios);

#define HALF_PERIOD_US 4

uint8_t lcd_fb[LCD_FB_SIZE];

static inline void put(const struct gpio_dt_spec *p, int v)
{
	gpio_pin_set_raw(p->port, p->pin, v);
}

static void send_bit(int b)
{
	put(&data, b);
	put(&wr, 0);
	k_busy_wait(HALF_PERIOD_US);
	put(&wr, 1);
	k_busy_wait(HALF_PERIOD_US);
}

int lcd_init(void)
{
	const struct gpio_dt_spec *pins[] = { &cs, &wr, &data, &rd };

	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		if (!gpio_is_ready_dt(pins[i])) {
			return -ENODEV;
		}
		/* idle level of all four lines is high */
		gpio_pin_configure_dt(pins[i], GPIO_OUTPUT_HIGH);
		put(pins[i], 1);
	}

	k_msleep(10);

	/* same sequence as the original firmware */
	static const uint8_t init[] = {
		0x01, /* SYS EN */
		0x03, /* LCD ON */
		0x08, /* TONE OFF */
		0x80, /* IRQ DIS */
		0x04, /* TIMER DIS */
		0x05, /* WDT DIS */
	};

	for (size_t i = 0; i < ARRAY_SIZE(init); i++) {
		lcd_command(init[i]);
	}
	return 0;
}

void lcd_command(uint8_t cmd)
{
	put(&cs, 0);
	send_bit(1);
	send_bit(0);
	send_bit(0);
	for (int i = 7; i >= 0; i--) {
		send_bit((cmd >> i) & 1);
	}
	send_bit(0);
	put(&cs, 1);
}

void lcd_write(uint8_t addr, const uint8_t *buf, size_t len)
{
	put(&cs, 0);
	send_bit(1);
	send_bit(0);
	send_bit(1);
	for (int i = 6; i >= 0; i--) {
		send_bit((addr >> i) & 1);
	}
	for (size_t n = 0; n < len; n++) {
		for (int i = 0; i < 8; i++) {
			send_bit((buf[n] >> i) & 1);
		}
	}
	put(&cs, 1);
}

void lcd_flush(void)
{
	lcd_write(0, lcd_fb, LCD_FB_SIZE);
}

void lcd_fill(uint8_t value)
{
	for (size_t i = 0; i < LCD_FB_SIZE; i++) {
		lcd_fb[i] = value;
	}
}

void lcd_set(uint8_t byte, uint8_t bit, bool on)
{
	if (byte >= LCD_FB_SIZE || bit > 7) {
		return;
	}
	if (on) {
		lcd_fb[byte] |= BIT(bit);
	} else {
		lcd_fb[byte] &= ~BIT(bit);
	}
}
