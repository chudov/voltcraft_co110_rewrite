#ifndef UI_H_
#define UI_H_

#include <stdbool.h>
#include <stdint.h>
#include "display.h"

/* EV_TIMEOUT and EV_CALI_END are generated inside ui_tick() */
enum ui_event { EV_UP, EV_DOWN, EV_MENU, EV_ENTER, EV_MENU_LONG, EV_ENTER_LONG, EV_TIMEOUT, EV_CALI_END };

/* measured values; the history holds offset corrected degC tenths, RH %, CO2 ppm */
enum ui_screen { UI_MAIN, UI_MENU, UI_ALARM, UI_PLANT, UI_LOG, UI_CALI, UI_UNIT, UI_ADV, UI_OFFSET };

struct ui_data {
	int co2;
	bool co2_valid;
	bool co2_error;
	int t_raw;   /* 0.1 degC, without offset */
	int rh_raw;  /* %, without offset */
	bool th_valid;
	int hist[4][3][DISP_CHART_COLS]; /* [level][quantity][0 = newest], see history.h */
	int hist_count[4];
};

extern struct ui_data ud;

void ui_init(void);
enum ui_screen ui_current_screen(void);

/* true if the event did something (key click) */
bool ui_event(enum ui_event ev, int64_t now);
void ui_tick(int64_t now);      /* menu time-out, calibration countdown */
void ui_render(int64_t now);    /* draws the frame buffer, call lcd_flush() afterwards */

bool ui_calibrating(void);      /* the 250 s countdown runs */
bool ui_calibration_due(void);  /* true once, when the countdown has ended */

#endif
