#ifndef APP_H_
#define APP_H_

#include <stdbool.h>
#include <zephyr/kernel.h>

/* messages for the UI thread (the main thread owns the display and all UI state) */
enum msg_type {
	MSG_KEY,            /* a = enum ui_event */
	MSG_COMBO,          /* UP + DOWN held: toggle dark mode */
	MSG_TH,             /* a = temperature in 0.1 degC, b = RH in % (SHT30, without offsets) */
	MSG_CO2,            /* a = ppm */
	MSG_CO2_LOST,       /* no valid CO2 frame for 40 polls, the sensor is being power cycled */
	MSG_BACKLIGHT_OFF,  /* 63 s timer expired */
	MSG_ALARM_BEEP,     /* periodic alarm beep timer */
};

struct msg {
	enum msg_type type;
	int a;
	int b;
};

extern struct k_msgq app_q;

static inline void post(enum msg_type type, int a, int b)
{
	struct msg m = { type, a, b };

	(void)k_msgq_put(&app_q, &m, K_NO_WAIT); /* safe from threads, timers and ISRs */
}

/* UI -> CM1106S thread */
extern struct k_sem cali_sem;       /* given when the 400 ppm calibration has to be sent */
extern volatile bool co2_suspended; /* true during the 250 s calibration countdown */

/* touch keys: GPIO interrupts + work item, posts MSG_KEY / MSG_COMBO */
int input_init(void);

#endif
