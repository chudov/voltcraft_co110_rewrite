#include <zephyr/ztest.h>
#include "alarm.h"
#include "ui.h"
#include "startup.h"
#include "settings.h"
#include "leds.h"

extern int stub_save_count;
extern int stub_reset_count;
extern int stub_hist_clear_count;
extern unsigned int stub_leds;
extern int stub_menu_line;
void stub_defaults(void);

/* ---------------------------------------------------------------- alarm zones */

static void alarm_before(void *f)
{
	ARG_UNUSED(f);
	alarm_set_user_thresholds(0, 800, 1200);
	alarm_set_user_thresholds(1, 600, 900);
}

ZTEST_SUITE(alarm, NULL, NULL, alarm_before, NULL, NULL);

ZTEST(alarm, test_human_zones)
{
	bool buzz;

	zassert_equal(alarm_evaluate(500, 0, &buzz), ZONE_OK);
	zassert_false(buzz);
	zassert_equal(alarm_evaluate(799, 0, &buzz), ZONE_OK);
	zassert_equal(alarm_evaluate(800, 0, &buzz), ZONE_WARN);
	zassert_false(buzz);
	zassert_equal(alarm_evaluate(1199, 0, &buzz), ZONE_WARN);
	zassert_false(buzz);
	zassert_equal(alarm_evaluate(1200, 0, &buzz), ZONE_ALERT);
	zassert_true(buzz);
}

ZTEST(alarm, test_plant_zones)
{
	bool buzz;

	/* preset 3: low 800, high 1000 */
	zassert_equal(alarm_evaluate(700, 3, &buzz), ZONE_ALERT);
	zassert_true(buzz);
	zassert_equal(alarm_evaluate(800, 3, &buzz), ZONE_ALERT);
	zassert_true(buzz);
	zassert_equal(alarm_evaluate(801, 3, &buzz), ZONE_OK);
	zassert_false(buzz);
	zassert_equal(alarm_evaluate(999, 3, &buzz), ZONE_OK);
	zassert_equal(alarm_evaluate(1000, 3, &buzz), ZONE_WARN);
	zassert_false(buzz);
}

ZTEST(alarm, test_thresholds)
{
	int low, high;

	alarm_thresholds(0, &low, &high);
	zassert_equal(low, 800);
	zassert_equal(high, 1200);
	alarm_thresholds(7, &low, &high);
	zassert_equal(low, 1200);
	zassert_equal(high, 1800);

	alarm_set_user_thresholds(1, 500, 700);
	alarm_thresholds(1, &low, &high);
	zassert_equal(low, 500);
	zassert_equal(high, 700);

	alarm_set_user_thresholds(5, 1, 2); /* fixed presets cannot be changed */
	alarm_thresholds(5, &low, &high);
	zassert_equal(low, 800);
	zassert_equal(high, 1400);
}

/* ------------------------------------------------------------- UI state machine */

static int64_t T;

static bool ev(enum ui_event e)
{
	T += 100;
	return ui_event(e, T);
}

static void ui_before(void *f)
{
	ARG_UNUSED(f);
	stub_defaults();
	stub_save_count = 0;
	stub_reset_count = 0;
	stub_hist_clear_count = 0;
	alarm_set_user_thresholds(0, 800, 1200);
	alarm_set_user_thresholds(1, 600, 900);
	co110_ud.th_valid = false;
	T = 1000;
	ui_init();
}

ZTEST_SUITE(ui, NULL, NULL, ui_before, NULL, NULL);

ZTEST(ui, test_starts_in_main_and_ignores_enter)
{
	zassert_equal(ui_current_screen(), UI_MAIN);
	zassert_false(ev(EV_ENTER));
	zassert_equal(ui_current_screen(), UI_MAIN);
}

ZTEST(ui, test_plant_selection_is_saved)
{
	zassert_true(ev(EV_MENU));
	zassert_equal(ui_current_screen(), UI_MENU);
	zassert_true(ev(EV_DOWN));        /* item 2 = PLANT */
	zassert_true(ev(EV_ENTER));
	zassert_equal(ui_current_screen(), UI_PLANT);
	zassert_true(ev(EV_UP));
	zassert_true(ev(EV_ENTER));
	zassert_equal(ui_current_screen(), UI_MAIN);
	zassert_equal(g_set.preset, 1);
	zassert_equal(stub_save_count, 1);
}

ZTEST(ui, test_menu_cycles_and_leaves)
{
	zassert_true(ev(EV_MENU));
	for (int i = 0; i < 5; i++) {         /* items 2 .. 6 */
		zassert_true(ev(EV_MENU));
		zassert_equal(ui_current_screen(), UI_MENU);
	}
	zassert_equal(stub_menu_line, 0);     /* render not called yet */
	zassert_true(ev(EV_MENU));            /* after item 6: back to main */
	zassert_equal(ui_current_screen(), UI_MAIN);
}

ZTEST(ui, test_menu_cancel_in_screen)
{
	zassert_true(ev(EV_MENU));
	zassert_true(ev(EV_DOWN));
	zassert_true(ev(EV_DOWN));
	zassert_true(ev(EV_DOWN));
	zassert_true(ev(EV_DOWN));            /* item 5 = unit */
	zassert_true(ev(EV_ENTER));
	zassert_equal(ui_current_screen(), UI_UNIT);
	zassert_true(ev(EV_UP));              /* edit only */
	zassert_true(ev(EV_MENU));            /* cancel: handled by the parent state */
	zassert_equal(ui_current_screen(), UI_MAIN);
	zassert_equal(g_set.unit_f, 0);
	zassert_equal(stub_save_count, 0);
}

ZTEST(ui, test_menu_timeout)
{
	zassert_true(ev(EV_MENU));
	ui_tick(T + 59000);
	zassert_equal(ui_current_screen(), UI_MENU);
	ui_tick(T + 61000);
	zassert_equal(ui_current_screen(), UI_MAIN);
}

ZTEST(ui, test_alarm_toggle)
{
	zassert_true(ev(EV_MENU));
	zassert_true(ev(EV_ENTER));           /* item 1 = ALARM */
	zassert_equal(ui_current_screen(), UI_ALARM);
	zassert_true(ev(EV_UP));
	zassert_equal(g_set.alarm_on, 0);
	zassert_true(ev(EV_DOWN));
	zassert_equal(g_set.alarm_on, 1);
	zassert_equal(stub_save_count, 2);
	zassert_true(ev(EV_ENTER));
	zassert_equal(ui_current_screen(), UI_MAIN);
}

static void open_adv(void)
{
	zassert_true(ev(EV_MENU));
	zassert_true(ev(EV_UP));              /* wraps from item 1 to item 6 */
	zassert_true(ev(EV_ENTER));
}

ZTEST(ui, test_adv_high_must_stay_above_low)
{
	open_adv();
	zassert_equal(ui_current_screen(), UI_ADV);
	zassert_true(ev(EV_ENTER));           /* keep low = 800, now the high value */
	for (int i = 0; i < 40; i++) {        /* 1200 -> 800 */
		zassert_true(ev(EV_DOWN));
	}
	zassert_false(ev(EV_ENTER));          /* high == low is refused */
	zassert_equal(ui_current_screen(), UI_ADV);
	zassert_true(ev(EV_UP));              /* 810 */
	zassert_true(ev(EV_ENTER));
	zassert_equal(ui_current_screen(), UI_MAIN);
	zassert_equal(g_set.low[0], 800);
	zassert_equal(g_set.high[0], 810);

	int low, high;

	alarm_thresholds(0, &low, &high);
	zassert_equal(low, 800);
	zassert_equal(high, 810);
}

ZTEST(ui, test_adv_not_editable_for_fixed_presets)
{
	g_set.preset = 3;
	zassert_true(ev(EV_MENU));
	zassert_true(ev(EV_UP));
	zassert_false(ev(EV_ENTER));
	zassert_equal(ui_current_screen(), UI_MENU);
}

ZTEST(ui, test_calibration_countdown)
{
	zassert_true(ev(EV_MENU));
	zassert_true(ev(EV_DOWN));
	zassert_true(ev(EV_DOWN));
	zassert_true(ev(EV_DOWN));            /* item 4 = CALI */
	zassert_true(ev(EV_ENTER));
	zassert_equal(ui_current_screen(), UI_CALI);
	zassert_false(ui_calibrating());

	zassert_true(ui_event(EV_ENTER_LONG, T));
	zassert_true(ui_calibrating());
	ui_tick(T + 100000);                  /* the 60 s menu time-out must not interrupt it */
	zassert_equal(ui_current_screen(), UI_CALI);
	ui_tick(T + 249999);
	zassert_true(ui_calibrating());
	ui_tick(T + 250000);
	zassert_false(ui_calibrating());
	zassert_equal(ui_current_screen(), UI_MAIN);
	zassert_true(ui_calibration_due());
	zassert_false(ui_calibration_due());  /* reported only once */
}

ZTEST(ui, test_calibration_result_message)
{
	zassert_equal(ui_cali_message(0), UI_CALI_MSG_NONE);
	ui_show_cali_result(true, 1000);
	zassert_equal(ui_cali_message(1000), UI_CALI_MSG_DONE);
	zassert_equal(ui_cali_message(3999), UI_CALI_MSG_DONE);
	zassert_equal(ui_cali_message(4000), UI_CALI_MSG_NONE);
	ui_show_cali_result(false, 10000);
	zassert_equal(ui_cali_message(10500), UI_CALI_MSG_FAIL);
	zassert_equal(ui_cali_message(13000), UI_CALI_MSG_NONE);
}

ZTEST(ui, test_down_cycles_charts_and_auto_state)
{
	zassert_equal(ui_quantity(), DISP_Q_CO2);
	zassert_true(ev(EV_DOWN));
	zassert_equal(ui_quantity(), DISP_Q_RH);
	zassert_true(ev(EV_DOWN));
	zassert_equal(ui_quantity(), DISP_Q_TEMP);
	zassert_false(ui_auto_cycle());
	zassert_true(ev(EV_DOWN));                /* 4th state: automatic cycle, starts with temperature */
	zassert_true(ui_auto_cycle());
	zassert_equal(ui_quantity(), DISP_Q_TEMP);
	ui_tick(T + 9999);
	zassert_equal(ui_quantity(), DISP_Q_TEMP);
	ui_tick(T + 10000);
	zassert_equal(ui_quantity(), DISP_Q_CO2);
	ui_tick(T + 20000);
	zassert_equal(ui_quantity(), DISP_Q_RH);
	ui_tick(T + 30000);
	zassert_equal(ui_quantity(), DISP_Q_TEMP);
	zassert_true(ev(EV_DOWN));                /* back to the CO2 chart */
	zassert_false(ui_auto_cycle());
	zassert_equal(ui_quantity(), DISP_Q_CO2);
}

ZTEST(ui, test_calibration_abort)
{
	zassert_true(ev(EV_MENU));
	zassert_true(ev(EV_DOWN));
	zassert_true(ev(EV_DOWN));
	zassert_true(ev(EV_DOWN));
	zassert_true(ev(EV_ENTER));
	zassert_true(ui_event(EV_ENTER_LONG, T + 100));
	zassert_true(ui_calibrating());
	zassert_true(ui_event(EV_MENU, T + 200)); /* leaving the screen aborts */
	zassert_false(ui_calibrating());
	ui_tick(T + 300000);
	zassert_false(ui_calibration_due());
}

ZTEST(ui, test_factory_reset_from_main)
{
	g_set.preset = 4;
	zassert_true(ev(EV_ENTER_LONG));
	zassert_equal(stub_reset_count, 1);
	zassert_equal(stub_hist_clear_count, 1);
	zassert_equal(g_set.preset, 0);
}

ZTEST(ui, test_offsets_need_a_measurement)
{
	zassert_false(ev(EV_MENU_LONG));
	zassert_equal(ui_current_screen(), UI_MAIN);

	co110_ud.th_valid = true;
	co110_ud.t_raw = 200;
	co110_ud.rh_raw = 40;
	zassert_true(ev(EV_MENU_LONG));
	zassert_equal(ui_current_screen(), UI_OFFSET);
	zassert_true(ev(EV_UP));              /* 20.1 degC */
	zassert_true(ev(EV_ENTER));
	zassert_equal(g_set.temp_offset, 1);
	zassert_equal(ui_current_screen(), UI_OFFSET); /* now the humidity page */
	zassert_true(ev(EV_UP));              /* 41 % */
	zassert_true(ev(EV_ENTER));
	zassert_equal(g_set.rh_offset, 1);
	zassert_equal(ui_current_screen(), UI_MAIN);
	zassert_equal(stub_save_count, 1);
}

/* ----------------------------------------------------------- start-up sequence */

ZTEST_SUITE(startup, NULL, NULL, NULL, NULL, NULL);

ZTEST(startup, test_sequence)
{
	startup_init(0);
	zassert_true(startup_active());
	zassert_equal(stub_leds, LED_ALL);

	startup_tick(1999);
	zassert_equal(stub_leds, LED_ALL);
	startup_tick(2000);
	zassert_equal(stub_leds, LED_LD1);
	startup_tick(3000);
	zassert_equal(stub_leds, LED_LD2);
	startup_tick(4000);
	zassert_equal(stub_leds, LED_LD3);
	startup_tick(5000);
	zassert_equal(stub_leds, LED_LD4);
	startup_tick(6000);
	zassert_equal(stub_leds, LED_ALL);
	startup_tick(7000);
	zassert_equal(stub_leds, 0);          /* countdown, LEDs off */
	zassert_true(startup_active());
	startup_tick(36999);
	zassert_true(startup_active());
	startup_tick(37000);
	zassert_false(startup_active());
	zassert_equal(stub_leds, 0);
}
