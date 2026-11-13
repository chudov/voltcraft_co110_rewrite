#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include "startup.h"
#include "display.h"
#include "leds.h"
#include "alarm.h"
#include "lcd.h"

enum su_state { SU_TEST, SU_LD1, SU_LO1, SU_LO2, SU_HI1, SU_HI2, SU_COUNT, SU_DONE };

struct su_obj {
	struct smf_ctx ctx;   /* must be first */
	int64_t now;
	int64_t entered;
	enum su_state step;
	bool active;
};

static struct su_obj su;

static const struct smf_state su_states[];

/* one state per step: entry sets the LEDs, run leaves the state after its duration */
#define SU_STEP(name, idx, led_mask, duration_ms, next)                                 \
	static void name##_entry(void *x)                                               \
	{                                                                               \
		struct su_obj *o = x;                                                   \
		o->step = idx;                                                          \
		o->entered = o->now;                                                    \
		leds_set(led_mask);                                                     \
	}                                                                               \
	static enum smf_state_result name##_run(void *x)                                \
	{                                                                               \
		struct su_obj *o = x;                                                   \
		if (o->now - o->entered >= (duration_ms)) {                             \
			smf_set_state(SMF_CTX(o), &su_states[next]);                    \
		}                                                                       \
		return SMF_EVENT_HANDLED;                                               \
	}

SU_STEP(su_test, SU_TEST, LED_ALL, 2000, SU_LD1)
SU_STEP(su_ld1, SU_LD1, LED_LD1, 1000, SU_LO1)
SU_STEP(su_lo1, SU_LO1, LED_LD2, 1000, SU_LO2)
SU_STEP(su_lo2, SU_LO2, LED_LD3, 1000, SU_HI1)
SU_STEP(su_hi1, SU_HI1, LED_LD4, 1000, SU_HI2)
SU_STEP(su_hi2, SU_HI2, LED_ALL, 1000, SU_COUNT)
SU_STEP(su_count, SU_COUNT, 0, 30000, SU_DONE)

static void su_done_entry(void *x)
{
	struct su_obj *o = x;

	o->step = SU_DONE;
	o->active = false;
	leds_set(0);
}

static enum smf_state_result su_done_run(void *x)
{
	ARG_UNUSED(x);
	return SMF_EVENT_HANDLED;
}

static const struct smf_state su_states[] = {
	[SU_TEST] = SMF_CREATE_STATE(su_test_entry, su_test_run, NULL, NULL, NULL),
	[SU_LD1] = SMF_CREATE_STATE(su_ld1_entry, su_ld1_run, NULL, NULL, NULL),
	[SU_LO1] = SMF_CREATE_STATE(su_lo1_entry, su_lo1_run, NULL, NULL, NULL),
	[SU_LO2] = SMF_CREATE_STATE(su_lo2_entry, su_lo2_run, NULL, NULL, NULL),
	[SU_HI1] = SMF_CREATE_STATE(su_hi1_entry, su_hi1_run, NULL, NULL, NULL),
	[SU_HI2] = SMF_CREATE_STATE(su_hi2_entry, su_hi2_run, NULL, NULL, NULL),
	[SU_COUNT] = SMF_CREATE_STATE(su_count_entry, su_count_run, NULL, NULL, NULL),
	[SU_DONE] = SMF_CREATE_STATE(su_done_entry, su_done_run, NULL, NULL, NULL),
};

void startup_init(int64_t now)
{
	su.now = now;
	su.active = true;
	smf_set_initial(SMF_CTX(&su), &su_states[SU_TEST]);
}

void startup_tick(int64_t now)
{
	if (su.active) {
		su.now = now;
		(void)smf_run_state(SMF_CTX(&su));
	}
}

bool startup_active(void)
{
	return su.active;
}

void startup_render(int preset)
{
	int low, high;

	alarm_thresholds(preset, &low, &high);
	disp_clear();

	switch (su.step) {
	case SU_TEST:
		lcd_fill(0xFF);
		break;
	case SU_LO1:
	case SU_LO2:
		disp_co2(low, DISP_CO2_VALUE);
		disp_rh_text('L', '0', true);
		break;
	case SU_HI1:
	case SU_HI2:
		disp_co2(high, DISP_CO2_VALUE);
		disp_rh_text('H', 'I', true);
		break;
	case SU_COUNT:
		disp_startup_countdown(30 - (int)((su.now - su.entered) / 1000));
		break;
	default: /* SU_LD1, SU_DONE: blank display */
		break;
	}
}
