/* Drawing layer of the CO-110 LCD, built on lcd_fb and the measured segment map. */
#include <stdlib.h>
#include "display.h"
#include "lcd.h"
#include "lcd_map.h"

static const uint8_t glyph_digit[10] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

/* bit 0..6 = segment a..g */
static uint8_t glyph(char c)
{
	if (c >= '0' && c <= '9') {
		return glyph_digit[c - '0'];
	}
	switch (c) {
	case '-': return 0x40;
	case 'E': return 0x79;
	case 'L': return 0x38;
	case 'H': return 0x76;
	case 'I': return 0x30;
	default:  return 0x00;
	}
}

static void put_char(const lcd_seg_t s[8], char c, bool dp)
{
	uint8_t g = glyph(c);

	for (int i = 0; i < 7; i++) {
		lcd_set(s[i].byte, s[i].bit, (g >> i) & 1);
	}
	if (s[7].byte != LCD_SEG_NONE) {
		lcd_set(s[7].byte, s[7].bit, dp);
	}
}

/* right aligned integer, at least min_digits digits, leading blanks */
static void put_uint(const lcd_seg_t d[][8], int n, unsigned int v, int min_digits,
		     int dp_digit /* digit index with decimal point, -1 none */)
{
	for (int i = n - 1; i >= 0; i--) {
		int from_right = n - 1 - i;
		char c = ' ';

		if (v > 0 || from_right < min_digits) {
			c = '0' + (v % 10);
			v /= 10;
		}
		put_char(d[i], c, i == dp_digit);
	}
}

void disp_clear(void)
{
	lcd_fill(0x00);
}

void disp_co2(int ppm, enum disp_co2_mode mode)
{
	if (mode == DISP_CO2_DASHES) {
		for (int i = 0; i < 4; i++) {
			put_char(lcd_digits_co2[i], '-', false);
		}
	} else if (mode == DISP_CO2_ERROR) {
		put_char(lcd_digits_co2[0], ' ', false);
		put_char(lcd_digits_co2[1], ' ', false);
		put_char(lcd_digits_co2[2], 'E', false);
		put_char(lcd_digits_co2[3], '1', false);
	} else {
		if (ppm < 0) {
			ppm = 0;
		}
		put_uint(lcd_digits_co2, 4, ppm > 9999 ? 9999 : ppm, 1, -1);
	}
	lcd_set(LCD_SYM_CO2, true);
	lcd_set(LCD_SYM_PP, true);
	lcd_set(LCD_SYM_M, true);
}

void disp_co2_countdown(int n)
{
	if (n < 0) {
		n = 0;
	}
	if (n > 99) {
		n = 99;
	}
	put_char(lcd_digits_co2[0], ' ', false);
	put_char(lcd_digits_co2[1], n >= 10 ? '0' + n / 10 : ' ', false);
	put_char(lcd_digits_co2[2], '0' + n % 10, false);
	put_char(lcd_digits_co2[3], ' ', false);
	lcd_set(LCD_SYM_CO2, true);
	lcd_set(LCD_SYM_PP, true);
	lcd_set(LCD_SYM_M, true);
}

void disp_blank_co2(void)
{
	for (int i = 0; i < 4; i++) {
		put_char(lcd_digits_co2[i], ' ', false);
	}
}

void disp_blank_temp(void)
{
	for (int i = 0; i < 3; i++) {
		put_char(lcd_digits_temp[i], ' ', false);
	}
}

void disp_blank_rh(void)
{
	for (int i = 0; i < 2; i++) {
		put_char(lcd_digits_rh[i], ' ', false);
	}
}

void disp_rh_text(char left, char right)
{
	put_char(lcd_digits_rh[0], left, false);
	put_char(lcd_digits_rh[1], right, false);
	lcd_set(LCD_SYM_RH_PCT, true);
}

/* xx.x in three digits, '-' in the left digit for negative values */
static void put_tenths(const lcd_seg_t d[][8], int tenths)
{
	bool neg = tenths < 0;
	unsigned int v = abs(tenths);

	if (v > 999) {
		v = 999;
	}
	put_uint(d, 3, v, 2, 1);
	if (neg) {
		put_char(d[0], '-', false);
	}
}

void disp_temp(int tenths, bool fahrenheit)
{
	put_tenths(lcd_digits_temp, tenths);
	lcd_set(LCD_UNIT_DEG, true);
	lcd_set(LCD_UNIT_C_D, !fahrenheit);
	lcd_set(LCD_UNIT_F_G, fahrenheit);
}

void disp_rh(int percent)
{
	if (percent < 0) {
		percent = 0;
	}
	put_uint(lcd_digits_rh, 2, percent > 99 ? 99 : percent, 1, -1);
	lcd_set(LCD_SYM_RH_PCT, true);
}

/* MIN / MAX number: 3 digits; for CO2 >= 1000 a leading "1", >= 1999 shown as x10 */
static void put_minmax(const lcd_seg_t d[][8], int v, enum disp_quantity q,
		       int x10_byte, int x10_bit, int one_byte, int one_bit)
{
	bool x10 = false, one = false;

	if (q == DISP_Q_TEMP) {
		put_tenths(d, v);
	} else {
		if (v < 0) {
			v = 0;
		}
		if (v >= 1999) {
			v /= 10;
			x10 = true;
		}
		if (v >= 1000) {
			one = true;
			v -= 1000;
		}
		put_uint(d, 3, v, one ? 3 : 1, -1);
	}
	lcd_set(x10_byte, x10_bit, x10);
	lcd_set(one_byte, one_bit, one);
}

void disp_chart(const int *values, int count, enum disp_quantity q,
		bool fahrenheit, int selected_col, bool hide_selected)
{
	int vmin = 0, vmax = 0;

	if (count > DISP_CHART_COLS) {
		count = DISP_CHART_COLS;
	}
	for (int c = 0; c < count; c++) {
		if (c == 0 || values[c] < vmin) {
			vmin = values[c];
		}
		if (c == 0 || values[c] > vmax) {
			vmax = values[c];
		}
	}

	/* clear the dot field */
	for (int x = 0; x < DISP_CHART_COLS; x++) {
		lcd_fb[LCD_DOT_BYTE(x)] = 0;
	}
	/* column 0 = newest sample at the right edge (x = 16) */
	for (int c = 0; c < count; c++) {
		int level = 1;

		if (vmax > vmin) {
			level = 1 + ((values[c] - vmin) * 7 + (vmax - vmin) / 2) / (vmax - vmin);
		}
		if (hide_selected && c == selected_col) {
			level = 0;
		}
		lcd_fb[LCD_DOT_BYTE(16 - c)] = (uint8_t)((1u << level) - 1u);
	}

	/* quantity marker on the top line */
	lcd_set(LCD_SYM_CO2_TOP, q == DISP_Q_CO2);
	lcd_set(LCD_SYM_RH_TOP, q == DISP_Q_RH);
	lcd_set(LCD_SYM_TEMP_TOP, q == DISP_Q_TEMP);

	/* MIN / MAX numbers and their unit markers */
	put_minmax(lcd_digits_min, vmin, q, LCD_MIN_X10, LCD_MIN_ONE);
	put_minmax(lcd_digits_max, vmax, q, LCD_MAX_X10, LCD_MAX_ONE);
	lcd_set(LCD_SYM_MINMAX_ARROWS, count > 0);
	lcd_set(LCD_SYM_PPM_TOP, q == DISP_Q_CO2);
	lcd_set(LCD_MM_PERCENT, q == DISP_Q_RH);
	lcd_set(LCD_MM_DEG, q == DISP_Q_TEMP);
	lcd_set(LCD_MM_C_D, q == DISP_Q_TEMP && !fahrenheit);
	lcd_set(LCD_MM_F_G, q == DISP_Q_TEMP && fahrenheit);
}

void disp_zoom(int z)
{
	lcd_set(LCD_SYM_PER_DIV, true);
	lcd_set(LCD_SYM_5S, z == 0);
	lcd_set(LCD_SYM_5M, z == 1);
	lcd_set(LCD_SYM_2H, z == 2);
	lcd_set(LCD_SYM_HALF_D, z == 3);
	/* span of the 17 columns */
	lcd_set(LCD_SYM_MINUS1M, z == 0);
	lcd_set(LCD_SYM_MINUS1, z == 1 || z == 2);
	lcd_set(LCD_SYM_H, z == 1);
	lcd_set(LCD_SYM_D, z == 2);
	lcd_set(LCD_SYM_MINUS1W, z == 3);
	lcd_set(LCD_SYM_TRIANGLE, z != 3);
}

void disp_menu(int item)
{
	lcd_set(LCD_MENU_LINE, item > 0);
	lcd_set(LCD_UL_ALARM, item == 1);
	lcd_set(LCD_UL_PLANT, item == 2);
	lcd_set(LCD_UL_LOG, item == 3);
	lcd_set(LCD_UL_CALI, item == 4);
	lcd_set(LCD_UL_UNIT, item == 5);
	lcd_set(LCD_UL_ADV, item == 6);
}

void disp_alarm(bool bell_visible, bool muted)
{
	lcd_set(LCD_SYM_BELL, bell_visible);
	lcd_set(LCD_SYM_BELL_CROSS, muted);
}

void disp_preset(int preset)
{
	/* the brackets mark the active mode */
	lcd_set(LCD_SYM_HUMAN, true);
	lcd_set(LCD_SYM_TREE, true);
	lcd_set(LCD_SYM_HUMAN_BRACKET, preset == 0);
	lcd_set(LCD_SYM_TREE_BRACKET, preset != 0);
}
