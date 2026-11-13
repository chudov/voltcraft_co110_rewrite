/*
 * UI state machine of the original firmware (see description.md, section 8):
 * main screen, menu (ALARM, PLANT, LOG, CALI, unit, ADV), offset calibration (MENU long).
 */
#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include "ui.h"
#include "settings.h"
#include "alarm.h"
#include "lcd.h"
#include "history.h"

#define MENU_TIMEOUT_MS 60000
#define CALI_SECONDS 250
#define ITEM_COUNT 6
#define CALI_MSG_MS 3000
#define CYCLE_MS 10000      /* chart change in the automatic cycle state */

struct ui_data co110_ud;

static enum ui_screen st = UI_MAIN;
static int item = 1;
static int edit_val;
static int edit_phase;
static int adv_low;
static int quantity = DISP_Q_CO2;
static bool auto_cycle;   /* 4th DOWN state: CO2, RH and temperature charts follow each other */
static int64_t cycle_next;
static int zoom;   /* chart level 0..3: 5 s, 5 min, 2 h, 12 h per division */
static int sel;    /* selected column in the LOG screen, 0 = newest */
static int64_t last_input;
static bool cali_running;
static bool cali_due;
static int64_t cali_end;
static enum ui_cali_msg cali_msg;
static int64_t cali_msg_end;

static int c_to_f(int c) { return c * 9 / 5 + 320; }
static int f_to_c(int f) { return ((f - 320) * 5 + (f >= 320 ? 4 : -4)) / 9; }

static int t_corrected(void) { return co110_ud.t_raw + g_set.temp_offset; }
static int rh_corrected(void) { return CLAMP(co110_ud.rh_raw + g_set.rh_offset, 0, 99); }

bool ui_calibrating(void) { return cali_running; }

void ui_show_cali_result(bool ok, int64_t now)
{
	cali_msg = ok ? UI_CALI_MSG_DONE : UI_CALI_MSG_FAIL;
	cali_msg_end = now + CALI_MSG_MS;
}

enum ui_cali_msg ui_cali_message(int64_t now)
{
	return (cali_msg != UI_CALI_MSG_NONE && now < cali_msg_end) ? cali_msg : UI_CALI_MSG_NONE;
}

bool ui_calibration_due(void)
{
	bool d = cali_due;

	cali_due = false;
	return d;
}

/* ---------------------------------------------------------------------------------------
 * State machine (Zephyr SMF, hierarchical). S_SCREEN is the parent of all screens except the
 * main screen: it handles what they share (MENU leaves, time-out returns to the main screen).
 * Events that a screen does not handle are propagated to the parent.
 * The variables above hold the data of the screens; "acted" tells the application that the
 * event did something (key click).
 * ------------------------------------------------------------------------------------- */

struct ui_obj {
	struct smf_ctx ctx;   /* must be first */
	enum ui_event ev;
	int64_t now;
	bool acted;
};

static struct ui_obj uo;

enum {
	S_MAIN, S_SCREEN, S_MENU, S_ALARM, S_PLANT, S_LOG, S_CALI, S_UNIT,
	S_ADV_LOW, S_ADV_HIGH, S_OFFSET_TEMP, S_OFFSET_RH,
};

static const struct smf_state states[];

static enum smf_state_result acted(struct ui_obj *o)
{
	o->acted = true;
	return SMF_EVENT_HANDLED;
}

static enum smf_state_result go(struct ui_obj *o, int state)
{
	smf_set_state(SMF_CTX(o), &states[state]);
	return acted(o);
}

/* --- main screen --- */
static void main_entry(void *x)
{
	struct ui_obj *o = x;

	st = UI_MAIN;
	cycle_next = o->now + CYCLE_MS;
}

static enum smf_state_result main_run(void *x)
{
	struct ui_obj *o = x;

	switch (o->ev) {
	case EV_DOWN:
		/* CO2 -> RH -> temperature -> automatic cycle -> CO2 */
		if (auto_cycle) {
			auto_cycle = false;
			quantity = DISP_Q_CO2;
		} else if (quantity == DISP_Q_TEMP) {
			auto_cycle = true; /* starts with the temperature chart */
			cycle_next = o->now + CYCLE_MS;
		} else {
			quantity++;
		}
		return acted(o);
	case EV_UP:
		zoom = (zoom + 1) % 4;
		return acted(o);
	case EV_MENU:
		return go(o, S_MENU);
	case EV_MENU_LONG:
		if (co110_ud.th_valid) {
			return go(o, S_OFFSET_TEMP);
		}
		break;
	case EV_ENTER_LONG:
		co110_settings_factory_reset();
		history_clear();
		return acted(o);
	default:
		break;
	}
	return SMF_EVENT_HANDLED;
}

/* --- parent of all other screens --- */
static enum smf_state_result screen_run(void *x)
{
	struct ui_obj *o = x;

	if (o->ev == EV_MENU) {         /* leave / cancel */
		return go(o, S_MAIN);
	}
	if (o->ev == EV_TIMEOUT) {      /* no key for 60 s */
		smf_set_state(SMF_CTX(o), &states[S_MAIN]);
	}
	return SMF_EVENT_HANDLED;       /* other events are ignored */
}

/* --- menu --- */
static void menu_entry(void *x)
{
	ARG_UNUSED(x);
	st = UI_MENU;
	item = 1;
}

static enum smf_state_result menu_run(void *x)
{
	struct ui_obj *o = x;

	switch (o->ev) {
	case EV_UP:
		item = item == 1 ? ITEM_COUNT : item - 1;
		return acted(o);
	case EV_DOWN:
		item = item == ITEM_COUNT ? 1 : item + 1;
		return acted(o);
	case EV_MENU:
		if (item == ITEM_COUNT) {
			return go(o, S_MAIN);
		}
		item++;
		return acted(o);
	case EV_ENTER:
		switch (item) {
		case 1: return go(o, S_ALARM);
		case 2: return go(o, S_PLANT);
		case 3: return go(o, S_LOG);
		case 4: return go(o, S_CALI);
		case 5: return go(o, S_UNIT);
		default:
			if (g_set.preset < 2) { /* only the two user presets are editable */
				return go(o, S_ADV_LOW);
			}
			return SMF_EVENT_HANDLED;
		}
	default:
		return SMF_EVENT_PROPAGATE;
	}
}

/* --- ALARM: on / off, saved immediately --- */
static void alarm_entry(void *x)
{
	ARG_UNUSED(x);
	st = UI_ALARM;
}

static enum smf_state_result alarm_run(void *x)
{
	struct ui_obj *o = x;

	if (o->ev == EV_UP || o->ev == EV_DOWN) {
		g_set.alarm_on = !g_set.alarm_on;
		co110_settings_save();
		return acted(o);
	}
	if (o->ev == EV_ENTER) {
		return go(o, S_MAIN);
	}
	return SMF_EVENT_PROPAGATE;
}

/* --- PLANT: preset 0..9 --- */
static void plant_entry(void *x)
{
	ARG_UNUSED(x);
	st = UI_PLANT;
	edit_val = g_set.preset;
}

static enum smf_state_result plant_run(void *x)
{
	struct ui_obj *o = x;

	switch (o->ev) {
	case EV_UP:
		edit_val = (edit_val + 1) % PRESET_COUNT;
		return acted(o);
	case EV_DOWN:
		edit_val = (edit_val + PRESET_COUNT - 1) % PRESET_COUNT;
		return acted(o);
	case EV_ENTER:
		g_set.preset = edit_val;
		co110_settings_save();
		return go(o, S_MAIN);
	default:
		return SMF_EVENT_PROPAGATE;
	}
}

/* --- LOG: browse the chart samples --- */
static void log_entry(void *x)
{
	ARG_UNUSED(x);
	st = UI_LOG;
	sel = 0;
}

static enum smf_state_result log_run(void *x)
{
	struct ui_obj *o = x;
	int n = MAX(co110_ud.hist_count[zoom], 1);

	switch (o->ev) {
	case EV_UP:      /* towards older samples (left) */
		sel = (sel + 1) % n;
		return acted(o);
	case EV_DOWN:    /* towards newer samples (right) */
		sel = (sel + n - 1) % n;
		return acted(o);
	case EV_ENTER:   /* next quantity */
		quantity = (quantity + 1) % 3;
		return acted(o);
	default:
		return SMF_EVENT_PROPAGATE;
	}
}

/* --- CALI: 250 s countdown, then the application sends the 400 ppm calibration --- */
static void cali_entry(void *x)
{
	ARG_UNUSED(x);
	st = UI_CALI;
	cali_running = false;
}

static void cali_exit(void *x)
{
	ARG_UNUSED(x);
	cali_running = false; /* leaving the screen aborts a running countdown */
}

static enum smf_state_result cali_run(void *x)
{
	struct ui_obj *o = x;

	switch (o->ev) {
	case EV_ENTER_LONG:
		if (!cali_running) {
			cali_running = true;
			cali_end = o->now + CALI_SECONDS * 1000LL;
			return acted(o);
		}
		return SMF_EVENT_HANDLED;
	case EV_ENTER:
		return go(o, S_MAIN);
	case EV_CALI_END:
		cali_due = true;
		smf_set_state(SMF_CTX(o), &states[S_MAIN]);
		return SMF_EVENT_HANDLED;
	case EV_TIMEOUT:
		if (cali_running) {
			return SMF_EVENT_HANDLED; /* the countdown keeps the screen */
		}
		return SMF_EVENT_PROPAGATE;
	default:
		return SMF_EVENT_PROPAGATE;
	}
}

/* --- unit: degC / degF --- */
static void unit_entry(void *x)
{
	ARG_UNUSED(x);
	st = UI_UNIT;
	edit_val = g_set.unit_f;
}

static enum smf_state_result unit_run(void *x)
{
	struct ui_obj *o = x;

	if (o->ev == EV_UP || o->ev == EV_DOWN) {
		edit_val ^= 1;
		return acted(o);
	}
	if (o->ev == EV_ENTER) {
		g_set.unit_f = edit_val;
		co110_settings_save();
		return go(o, S_MAIN);
	}
	return SMF_EVENT_PROPAGATE;
}

/* --- ADV: low, then high CO2 threshold of the editable presets --- */
static enum smf_state_result adv_step(struct ui_obj *o)
{
	if (o->ev == EV_UP) {
		edit_val = edit_val >= 5000 ? 0 : edit_val + 10;
		return acted(o);
	}
	if (o->ev == EV_DOWN) {
		edit_val = edit_val <= 0 ? 5000 : edit_val - 10;
		return acted(o);
	}
	return SMF_EVENT_PROPAGATE;
}

static void adv_low_entry(void *x)
{
	ARG_UNUSED(x);
	st = UI_ADV;
	edit_phase = 0;
	edit_val = g_set.low[g_set.preset];
}

static enum smf_state_result adv_low_run(void *x)
{
	struct ui_obj *o = x;

	if (o->ev == EV_ENTER) {
		adv_low = edit_val;
		return go(o, S_ADV_HIGH);
	}
	return adv_step(o);
}

static void adv_high_entry(void *x)
{
	ARG_UNUSED(x);
	edit_phase = 1;
	edit_val = g_set.high[g_set.preset];
}

static enum smf_state_result adv_high_run(void *x)
{
	struct ui_obj *o = x;

	if (o->ev == EV_ENTER) {
		int p = g_set.preset;

		if (edit_val <= adv_low) { /* high must stay above low */
			return SMF_EVENT_HANDLED;
		}
		g_set.low[p] = adv_low;
		g_set.high[p] = edit_val;
		co110_settings_save();
		alarm_set_user_thresholds(p, adv_low, edit_val);
		return go(o, S_MAIN);
	}
	return adv_step(o);
}

/* --- offsets: temperature, then humidity --- */
static void offset_temp_entry(void *x)
{
	ARG_UNUSED(x);
	st = UI_OFFSET;
	edit_phase = 0;
	edit_val = g_set.unit_f ? c_to_f(t_corrected()) : t_corrected();
}

static enum smf_state_result offset_temp_run(void *x)
{
	struct ui_obj *o = x;

	if (o->ev == EV_UP || o->ev == EV_DOWN) {
		int base = g_set.unit_f ? c_to_f(co110_ud.t_raw) : co110_ud.t_raw;

		edit_val = CLAMP(edit_val + (o->ev == EV_UP ? 1 : -1), base - 100, base + 100);
		return acted(o);
	}
	if (o->ev == EV_ENTER) {
		int c = g_set.unit_f ? f_to_c(edit_val) : edit_val;

		g_set.temp_offset = c - co110_ud.t_raw;
		return go(o, S_OFFSET_RH);
	}
	return SMF_EVENT_PROPAGATE;
}

static void offset_rh_entry(void *x)
{
	ARG_UNUSED(x);
	edit_phase = 1;
	edit_val = rh_corrected();
}

static enum smf_state_result offset_rh_run(void *x)
{
	struct ui_obj *o = x;

	if (o->ev == EV_UP || o->ev == EV_DOWN) {
		edit_val = CLAMP(edit_val + (o->ev == EV_UP ? 1 : -1),
				 CLAMP(co110_ud.rh_raw - 20, 0, 99), CLAMP(co110_ud.rh_raw + 20, 0, 99));
		return acted(o);
	}
	if (o->ev == EV_ENTER) {
		g_set.rh_offset = edit_val - co110_ud.rh_raw;
		co110_settings_save();
		return go(o, S_MAIN);
	}
	return SMF_EVENT_PROPAGATE;
}

/* --- state table --- */
static const struct smf_state states[] = {
	[S_MAIN] = SMF_CREATE_STATE(main_entry, main_run, NULL, NULL, NULL),
	[S_SCREEN] = SMF_CREATE_STATE(NULL, screen_run, NULL, NULL, NULL),
	[S_MENU] = SMF_CREATE_STATE(menu_entry, menu_run, NULL, &states[S_SCREEN], NULL),
	[S_ALARM] = SMF_CREATE_STATE(alarm_entry, alarm_run, NULL, &states[S_SCREEN], NULL),
	[S_PLANT] = SMF_CREATE_STATE(plant_entry, plant_run, NULL, &states[S_SCREEN], NULL),
	[S_LOG] = SMF_CREATE_STATE(log_entry, log_run, NULL, &states[S_SCREEN], NULL),
	[S_CALI] = SMF_CREATE_STATE(cali_entry, cali_run, cali_exit, &states[S_SCREEN], NULL),
	[S_UNIT] = SMF_CREATE_STATE(unit_entry, unit_run, NULL, &states[S_SCREEN], NULL),
	[S_ADV_LOW] = SMF_CREATE_STATE(adv_low_entry, adv_low_run, NULL, &states[S_SCREEN], NULL),
	[S_ADV_HIGH] = SMF_CREATE_STATE(adv_high_entry, adv_high_run, NULL, &states[S_SCREEN], NULL),
	[S_OFFSET_TEMP] = SMF_CREATE_STATE(offset_temp_entry, offset_temp_run, NULL,
					   &states[S_SCREEN], NULL),
	[S_OFFSET_RH] = SMF_CREATE_STATE(offset_rh_entry, offset_rh_run, NULL,
					 &states[S_SCREEN], NULL),
};

static void feed(enum ui_event ev, int64_t now)
{
	uo.ev = ev;
	uo.now = now;
	uo.acted = false;
	(void)smf_run_state(SMF_CTX(&uo));
}

enum ui_screen ui_current_screen(void)
{
	return st;
}

void ui_init(void)
{
	quantity = DISP_Q_CO2;
	auto_cycle = false;
	zoom = 0;
	smf_set_initial(SMF_CTX(&uo), &states[S_MAIN]);
}

bool ui_event(enum ui_event ev, int64_t now)
{
	last_input = now;
	feed(ev, now);
	return uo.acted;
}

bool ui_auto_cycle(void) { return auto_cycle; }
int ui_quantity(void) { return quantity; }

void ui_tick(int64_t now)
{
	if (auto_cycle && st == UI_MAIN && now >= cycle_next) {
		quantity = (quantity + 1) % 3;
		cycle_next = now + CYCLE_MS;
	}
	if (cali_running && now >= cali_end) {
		feed(EV_CALI_END, now);
	} else if (st != UI_MAIN && now - last_input > MENU_TIMEOUT_MS) {
		feed(EV_TIMEOUT, now);
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
	} else if (st == UI_MAIN && ui_cali_message(now) != UI_CALI_MSG_NONE) {
		disp_co2_text(ui_cali_message(now) == UI_CALI_MSG_DONE ? "donE" : "FAIL");
	} else if (co110_ud.co2_error) {
		disp_co2(0, DISP_CO2_ERROR);
	} else if (!co110_ud.co2_valid) {
		disp_co2(0, DISP_CO2_DASHES);
	} else {
		disp_co2(co110_ud.co2, DISP_CO2_VALUE);
	}

	/* temperature */
	if (st == UI_OFFSET && edit_phase == 0) {
		if (blink) {
			disp_temp(edit_val, g_set.unit_f);
		}
	} else if (co110_ud.th_valid) {
		int t = t_corrected();

		disp_temp(unit ? c_to_f(t) : t, unit);
	}

	/* humidity */
	if (st == UI_ADV) {
		if (edit_phase == 0) {
			disp_rh_text('L', '0', false);
		} else {
			disp_rh_text('H', 'I', false);
		}
	} else if (st == UI_OFFSET && edit_phase == 1) {
		if (blink) {
			disp_rh(edit_val);
		}
	} else if (co110_ud.th_valid) {
		disp_rh(rh_corrected());
	}

	/* chart of the selected zoom level */
	for (int c = 0; c < co110_ud.hist_count[zoom]; c++) {
		int v = co110_ud.hist[zoom][quantity][c];

		chart[c] = (quantity == DISP_Q_TEMP && unit) ? c_to_f(v) : v;
	}
	disp_chart(chart, co110_ud.hist_count[zoom], quantity, unit, st == UI_LOG ? sel : -1,
		   st == UI_LOG && blink);
	disp_zoom(zoom);

	/* LOG: show the selected sample in the field of the chosen quantity */
	if (st == UI_LOG) {
		disp_log_cursor(true);
		if (sel < co110_ud.hist_count[zoom]) {
			int v = chart[sel];

			if (quantity == DISP_Q_CO2) {
				disp_co2(v, DISP_CO2_VALUE);
			} else if (quantity == DISP_Q_RH) {
				disp_rh(v);
			} else {
				disp_temp(v, unit);
			}
		}
	}

	/* menu underline, alarm bell, mode pictograms */
	switch (st) {
	case UI_MENU:  menu_line = item; break;
	case UI_ALARM: menu_line = 1; break;
	case UI_PLANT: menu_line = 2; break;
	case UI_LOG:   menu_line = 3; break;
	case UI_CALI:  menu_line = 4; break;
	case UI_UNIT:  menu_line = 5; break;
	case UI_ADV:   menu_line = 6; break;
	default:       break;
	}
	disp_menu(menu_line);
	disp_alarm(st == UI_ALARM ? blink : true, !g_set.alarm_on);
	disp_preset(preset);
}
