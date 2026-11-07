/*
 * Segment map of the CO-110 LCD, measured on the real device with the
 * LCD bit-map test (frame buffer byte, bit). Generated from LED_mapping.txt.
 *
 * Symbols expand to "byte, bit", so use them as lcd_set(LCD_SYM_BELL, on).
 * Digit tables: [digit, left to right][segment a b c d e f g dp],
 * dp byte 255 = digit has no decimal point.
 * Segment letters: a top, b upper right, c lower right, d bottom,
 * e lower left, f upper left, g middle.
 */
#ifndef LCD_MAP_H_
#define LCD_MAP_H_

#include <stdint.h>

typedef struct {
	uint8_t byte;
	uint8_t bit;
} lcd_seg_t;

#define LCD_SEG_NONE 255

#define LCD_BATT_BODY          0, 0
#define LCD_SYM_CO2_TOP        0, 1
#define LCD_SYM_CHART_TOP      0, 2
#define LCD_SYM_RH_TOP         0, 3
#define LCD_SYM_CYCLE          0, 4
#define LCD_SYM_TEMP_TOP       0, 5
#define LCD_BATT_RIGHT         0, 7
#define LCD_BATT_MID           12, 0
#define LCD_BATT_LEFT          12, 7
#define LCD_SYM_MINMAX_ARROWS  12, 5
#define LCD_MAX_ONE            12, 6
#define LCD_MIN_ONE            13, 0
#define LCD_MAX_X10            13, 4
#define LCD_MIN_X10            15, 0
#define LCD_SYM_PPM_TOP        15, 4
#define LCD_UNIT_C_D           19, 1
#define LCD_UNIT_F_G           19, 2
#define LCD_UNIT_DEG           19, 3
#define LCD_MM_PERCENT         19, 4
#define LCD_MM_DEG             19, 5
#define LCD_MM_F_G             19, 6
#define LCD_MM_C_D             19, 7
#define LCD_SYM_RH_PCT         28, 7
#define LCD_UL_ADV             30, 0
#define LCD_SYM_CO2            30, 7
#define LCD_UL_UNIT            32, 0
#define LCD_SYM_PER_DIV        32, 7
#define LCD_MENU_LINE          33, 1
#define LCD_SYM_M              33, 2
#define LCD_SYM_PP             33, 3
#define LCD_UL_CALI            34, 0
#define LCD_SYM_PERCENT_VOL    34, 1
#define LCD_SYM_BELL           34, 2
#define LCD_SYM_5S             34, 7
#define LCD_UL_LOG             35, 0
#define LCD_SYM_BELL_CROSS     35, 1
#define LCD_SYM_HUMAN_BRACKET  35, 2
#define LCD_SYM_HUMAN          35, 3
#define LCD_UL_PLANT           36, 0
#define LCD_UL_ALARM           36, 1
#define LCD_SYM_TREE_BRACKET   36, 2
#define LCD_SYM_TREE           36, 3
#define LCD_SYM_5M             36, 7
#define LCD_SYM_D              37, 0
#define LCD_SYM_H              37, 1
#define LCD_SYM_HALF_D         37, 2
#define LCD_SYM_2H             37, 3
#define LCD_SYM_TRIANGLE       38, 0
#define LCD_SYM_MINUS1         38, 1
#define LCD_SYM_MINUS1M        38, 2
#define LCD_SYM_MINUS1W        38, 3
#define LCD_SYM_ARROWS_LR      38, 7
#define LCD_ROW_ARROW(row)  26, (row) /* arrows left of the dot field, row 0 = bottom */

/* 17 x 8 dot field: column x = 0 (left) .. 16 (right), row y = 0 (bottom) .. 7.
 * Bytes 1..11 hold x = 6..16 (byte 1 -> x 6), bytes 20..25 hold x = 5..0
 * (byte 20 -> x 5); bit n = row n. */
#define LCD_DOT_BYTE(x) ((x) >= 6 ? ((x) - 5) : (25 - (x)))

/* CO2 section: 4 digits (the second from the left has the dp) */
static const lcd_seg_t lcd_digits_co2[4][8] = {
	{ { 37, 7 }, { 37, 6 }, { 37, 5 }, { 37, 4 }, { 38, 4 }, { 38, 6 }, { 38, 5 }, { 255, 0 } },
	{ { 35, 7 }, { 35, 6 }, { 35, 5 }, { 35, 4 }, { 36, 4 }, { 36, 6 }, { 36, 5 }, { 34, 3 } },
	{ { 33, 7 }, { 33, 6 }, { 33, 5 }, { 33, 4 }, { 34, 4 }, { 34, 6 }, { 34, 5 }, { 255, 0 } },
	{ { 31, 7 }, { 31, 6 }, { 31, 5 }, { 31, 4 }, { 32, 4 }, { 32, 6 }, { 32, 5 }, { 255, 0 } },
};

/* temperature section: 3 digits, xx.x (middle digit has the dp) */
static const lcd_seg_t lcd_digits_temp[3][8] = {
	{ { 31, 3 }, { 31, 2 }, { 31, 1 }, { 31, 0 }, { 32, 1 }, { 32, 3 }, { 32, 2 }, { 255, 0 } },
	{ { 29, 3 }, { 29, 2 }, { 29, 1 }, { 29, 0 }, { 30, 1 }, { 30, 3 }, { 30, 2 }, { 28, 0 } },
	{ { 27, 3 }, { 27, 2 }, { 27, 1 }, { 27, 0 }, { 28, 1 }, { 28, 3 }, { 28, 2 }, { 255, 0 } },
};

/* RH section: 2 digits */
static const lcd_seg_t lcd_digits_rh[2][8] = {
	{ { 29, 7 }, { 29, 6 }, { 29, 5 }, { 29, 4 }, { 30, 4 }, { 30, 6 }, { 30, 5 }, { 255, 0 } },
	{ { 27, 7 }, { 27, 6 }, { 27, 5 }, { 27, 4 }, { 28, 4 }, { 28, 6 }, { 28, 5 }, { 255, 0 } },
};

/* MAX number (top right): 3 digits, middle digit has the dp */
static const lcd_seg_t lcd_digits_max[3][8] = {
	{ { 14, 7 }, { 14, 6 }, { 14, 5 }, { 14, 4 }, { 13, 5 }, { 13, 7 }, { 13, 6 }, { 255, 0 } },
	{ { 16, 7 }, { 16, 6 }, { 16, 5 }, { 16, 4 }, { 15, 5 }, { 15, 7 }, { 15, 6 }, { 17, 4 } },
	{ { 18, 7 }, { 18, 6 }, { 18, 5 }, { 18, 4 }, { 17, 5 }, { 17, 7 }, { 17, 6 }, { 255, 0 } },
};

/* MIN number (second from the top): 3 digits, middle digit has the dp */
static const lcd_seg_t lcd_digits_min[3][8] = {
	{ { 14, 3 }, { 14, 2 }, { 14, 1 }, { 14, 0 }, { 13, 1 }, { 13, 3 }, { 13, 2 }, { 255, 0 } },
	{ { 16, 3 }, { 16, 2 }, { 16, 1 }, { 16, 0 }, { 15, 1 }, { 15, 3 }, { 15, 2 }, { 17, 0 } },
	{ { 18, 3 }, { 18, 2 }, { 18, 1 }, { 18, 0 }, { 17, 1 }, { 17, 3 }, { 17, 2 }, { 255, 0 } },
};

#endif
