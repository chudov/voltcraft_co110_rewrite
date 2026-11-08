#ifndef UI_H_
#define UI_H_

#include <stdbool.h>
#include <stdint.h>
#include "display.h"

enum ui_event { EV_UP, EV_DOWN, EV_MENU, EV_ENTER, EV_MENU_LONG, EV_ENTER_LONG };

/* measured values; the history holds offset corrected degC tenths, RH %, CO2 ppm */
struct ui_data {
	int co2;
	bool co2_valid;
	bool co2_error;
	int t_raw;   /* 0.1 degC, without offset */
	int rh_raw;  /* %, without offset */
	bool th_valid;
	int hist[3][DISP_CHART_COLS]; /* [quantity][0 = newest] */
	int hist_count;
};

extern struct ui_data ud;

/* true if the event did something (key click) */
bool ui_event(enum ui_event ev, int64_t now);
void ui_tick(int64_t now);      /* menu time-out, calibration countdown */
void ui_render(int64_t now);    /* draws the frame buffer, call lcd_flush() afterwards */

bool ui_calibrating(void);      /* the 250 s countdown runs */
bool ui_calibration_due(void);  /* true once, when the countdown has ended */

#endif
