#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdbool.h>
#include <stdint.h>

#define DISP_CHART_COLS 17

enum disp_co2_mode { DISP_CO2_VALUE, DISP_CO2_DASHES, DISP_CO2_ERROR };
enum disp_quantity { DISP_Q_CO2, DISP_Q_RH, DISP_Q_TEMP };

/* all draw functions only change the frame buffer; call lcd_flush() afterwards */
void disp_clear(void);

void disp_co2(int ppm, enum disp_co2_mode mode);
void disp_startup_countdown(int n);              /* start-up frame: 2 digits, arrows, MIN/MAX 0, no CO2 ppm */
void disp_co2_text(const char text[4]);          /* four characters, see glyph() in display.c */
void disp_co2_countdown(int n);                   /* two digits in the middle of the CO2 digits */
void disp_rh_text(char left, char right, bool percent); /* e.g. 'L','0' = Lo, 'H','I' = Hi */
void disp_blank_co2(void);
void disp_blank_temp(void);
void disp_blank_rh(void);
void disp_temp(int tenths, bool fahrenheit);   /* -99 .. 999 (tenths) */
void disp_rh(int percent);                      /* 0 .. 99 */

/* chart and MIN/MAX numbers; values are CO2 ppm, RH % or tenths of a degree */
void disp_chart(const int *values, int count, enum disp_quantity q,
		bool fahrenheit, int selected_col, bool hide_selected);
void disp_log_cursor(bool on);                  /* the left-right arrow symbol of the LOG screen */
void disp_zoom(int zoom);                       /* 0: 5 s .. 3: 0.5 d per division */
void disp_menu(int item);                       /* 0 = none, 1..6 = ALARM PLANT LOG CALI UNIT ADV */
void disp_alarm(bool bell_visible, bool muted);
void disp_preset(int preset);                   /* 0 = human, others = plant */

#endif
