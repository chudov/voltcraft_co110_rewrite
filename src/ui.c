/*
 * UI state machine of the original firmware (see description.md, section 8):
 * main screen, menu (ALARM, PLANT, LOG, CALI, unit, ADV), offset calibration (MENU long).
 */
#include <zephyr/kernel.h>
#include "ui.h"
#include "settings.h"
#include "alarm.h"
#include "lcd.h"

#define MENU_TIMEOUT_MS 60000
#define CALI_SECONDS 250
#define ITEM_COUNT 6

enum ui_state { UI_MAIN, UI_MENU, UI_ALARM, UI_PLANT, UI_CALI, UI_UNIT, UI_ADV, UI_OFFSET };

struct ui_data ud;

static enum ui_state st = UI_MAIN;
static int item = 1;
static int edit_val;
static int edit_phase;
static int adv_low;
static int quantity = DISP_Q_CO2;
static int64_t last_input;
static bool cali_running;
static bool cali_due;
static int64_t cali_end;

static int c_to_f(int c) { return c * 9 / 5 + 320; }
static int f_to_c(int f) { return ((f - 320) * 5 + (f >= 320 ? 4 : -4)) / 9; }
static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int t_corrected(void) { return ud.t_raw + g_set.temp_offset; }
static int rh_corrected(void) { return clamp(ud.rh_raw + g_set.rh_offset, 0, 99); }

bool ui_calibrating(void) { return cali_running; }

bool ui_calibration_due(void)
{
	bool d = cali_due;

	cali_due = false;
	return d;
}

static bool open_item(void)
{
	switch (item) {
	case 1:
		st = UI_ALARM;
		return true;
	case 2:
		st = UI_PLANT;
		edit_val = g_set.preset;
		return true;
	case 4:
		st = UI_CALI;
		cali_running = false;
		return true;
	case 5:
		st = UI_UNIT;
		edit_val = g_set.unit_f;
		return true;
	case 6:
		if (g_set.preset < 2) { /* only the two user presets are editable */
			st = UI_ADV;
			edit_phase = 0;
			edit_val = g_set.low[g_set.preset];
			return true;
		}
		return false;
	default: /* 3 = LOG, not implemented yet */
		return false;
	}
}

bool ui_event(enum ui_event ev, int64_t now)
{
	last_input = now;

	switch (st) {
	case UI_MAIN:
		if (ev == EV_DOWN) {
			quantity = (quantity + 1) % 3;
			return true;
		}
		if (ev == EV_MENU) {
			st = UI_MENU;
			item = 1;
			return true;
		}
		if (ev == EV_MENU_LONG && ud.th_valid) {
			st = UI_OFFSET;
			edit_phase = 0;
			edit_val = g_set.unit_f ? c_to_f(t_corrected()) : t_corrected();
			return true;
		}
		if (ev == EV_ENTER_LONG) {
			settings_factory_reset();
			return true;
		}
		return false; /* UP is reserved for the chart zoom */

	case UI_MENU:
		if (ev == EV_UP) {
			item = item == 1 ? ITEM_COUNT : item - 1;
			return true;
		}
		if (ev == EV_DOWN) {
			item = item == ITEM_COUNT ? 1 : item + 1;
			return true;
		}
		if (ev == EV_MENU) {
			if (item == ITEM_COUNT) {
				st = UI_MAIN;
			} else {
				item++;
			}
			return true;
		}
		if (ev == EV_ENTER) {
			return open_item();
		}
		return false;

	case UI_ALARM:
		if (ev == EV_UP || ev == EV_DOWN) {
			g_set.alarm_on = !g_set.alarm_on;
			settings_save();
			return true;
		}
		if (ev == EV_MENU || ev == EV_ENTER) {
			st = UI_MAIN;
			return true;
		}
		return false;

	case UI_PLANT:
		if (ev == EV_UP) {
			edit_val = (edit_val + 1) % PRESET_COUNT;
			return true;
		}
		if (ev == EV_DOWN) {
			edit_val = (edit_val + PRESET_COUNT - 1) % PRESET_COUNT;
			return true;
		}
		if (ev == EV_ENTER) {
			g_set.preset = edit_val;
			settings_save();
			st = UI_MAIN;
			return true;
		}
		if (ev == EV_MENU) {
			st = UI_MAIN;
			return true;
		}
		return false;

	case UI_UNIT:
		if (ev == EV_UP || ev == EV_DOWN) {
			edit_val ^= 1;
			return true;
		}
		if (ev == EV_ENTER) {
			g_set.unit_f = edit_val;
			settings_save();
			st = UI_MAIN;
			return true;
		}
		if (ev == EV_MENU) {
			st = UI_MAIN;
			return true;
		}
		return false;

	case UI_ADV:
		if (ev == EV_UP) {
			edit_val = edit_val >= 5000 ? 0 : edit_val + 10;
			return true;
		}
		if (ev == EV_DOWN) {
			edit_val = edit_val <= 0 ? 5000 : edit_val - 10;
			return true;
		}
		if (ev == EV_ENTER) {
			int p = g_set.preset;

			if (edit_phase == 0) {
				adv_low = edit_val;
				edit_phase = 1;
				edit_val = g_set.high[p];
				return true;
			}
			if (edit_val <= adv_low) { /* high must stay above low */
				return false;
			}
			g_set.low[p] = adv_low;
			g_set.high[p] = edit_val;
			settings_save();
			alarm_set_user_thresholds(p, adv_low, edit_val);
			st = UI_MAIN;
			return true;
		}
		if (ev == EV_MENU) {
			st = UI_MAIN;
			return true;
		}
		return false;

	case UI_CALI:
		if (ev == EV_ENTER_LONG && !cali_running) {
			cali_running = true;
			cali_end = now + CALI_SECONDS * 1000LL;
			return true;
		}
		if (ev == EV_ENTER || ev == EV_MENU) {
			cali_running = false;
			st = UI_MAIN;
			return true;
		}
		return false;

	case UI_OFFSET:
		if (ev == EV_UP || ev == EV_DOWN) {
			int d = ev == EV_UP ? 1 : -1;

			if (edit_phase == 0) {
				int base = g_set.unit_f ? c_to_f(ud.t_raw) : ud.t_raw;

				edit_val = clamp(edit_val + d, base - 100, base + 100);
			} else {
				edit_val = clamp(edit_val + d, clamp(ud.rh_raw - 20, 0, 99),
						 clamp(ud.rh_raw + 20, 0, 99));
			}
			return true;
		}
		if (ev == EV_ENTER) {
			if (edit_phase == 0) {
				int c = g_set.unit_f ? f_to_c(edit_val) : edit_val;

				g_set.temp_offset = c - ud.t_raw;
				edit_phase = 1;
				edit_val = rh_corrected();
			} else {
				g_set.rh_offset = edit_val - ud.rh_raw;
				settings_save();
				st = UI_MAIN;
			}
			return true;
		}
		if (ev == EV_MENU) {
			st = UI_MAIN;
			return true;
		}
		return false;
	}
	return false;
}

void ui_tick(int64_t now)
{
	if (cali_running && now >= cali_end) {
		cali_running = false;
		cali_due = true;
		st = UI_MAIN;
	}
	if (st != UI_MAIN && !cali_running && now - last_input > MENU_TIMEOUT_MS) {
		st = UI_MAIN;
	}
}

void ui_render(int64_t now)
{
	bool blink = (now / 500) % 2;
	int unit = (st == UI_UNIT) ? edit_val : g_set.unit_f;
	int preset = (st == UI_PLANT) ? edit_val : g_set.preset;
	int chart[DISP_CHART_COLS];
	int menu_line = 0;

	disp_clear();

	/* CO2 digits */
	if (st == UI_CALI) {
		int rem = cali_running ? (int)((cali_end - now + 999) / 1000) : 400;

		disp_co2(rem, DISP_CO2_VALUE);
	} else if (st == UI_PLANT) {
		disp_co2_countdown(edit_val);
	} else if (st == UI_ADV) {
		if (blink) {
			disp_co2(edit_val, DISP_CO2_VALUE);
		} else {
			disp_blank_co2();
		}
	} else if (ud.co2_error) {
		disp_co2(0, DISP_CO2_ERROR);
	} else if (!ud.co2_valid) {
		disp_co2(0, DISP_CO2_DASHES);
	} else {
		disp_co2(ud.co2, DISP_CO2_VALUE);
	}

	/* temperature */
	if (st == UI_OFFSET && edit_phase == 0) {
		if (blink) {
			disp_temp(edit_val, g_set.unit_f);
		}
	} else if (ud.th_valid) {
		int t = t_corrected();

		disp_temp(unit ? c_to_f(t) : t, unit);
	}

	/* humidity */
	if (st == UI_ADV) {
		if (edit_phase == 0) {
			disp_rh_text('L', '0');
		} else {
			disp_rh_text('H', 'I');
		}
	} else if (st == UI_OFFSET && edit_phase == 1) {
		if (blink) {
			disp_rh(edit_val);
		}
	} else if (ud.th_valid) {
		disp_rh(rh_corrected());
	}

	/* chart */
	for (int c = 0; c < ud.hist_count; c++) {
		int v = ud.hist[quantity][c];

		chart[c] = (quantity == DISP_Q_TEMP && unit) ? c_to_f(v) : v;
	}
	disp_chart(chart, ud.hist_count, quantity, unit, -1, false);
	disp_zoom(0);

	/* menu underline, alarm bell, mode pictograms */
	switch (st) {
	case UI_MENU:  menu_line = item; break;
	case UI_ALARM: menu_line = 1; break;
	case UI_PLANT: menu_line = 2; break;
	case UI_CALI:  menu_line = 4; break;
	case UI_UNIT:  menu_line = 5; break;
	case UI_ADV:   menu_line = 6; break;
	default:       break;
	}
	disp_menu(menu_line);
	disp_alarm(st == UI_ALARM ? blink : true, !g_set.alarm_on);
	disp_preset(preset);
}
