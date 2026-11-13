/* Stand-ins for the hardware dependent modules used by ui.c and startup.c. */
#include <string.h>
#include <zephyr/sys/util.h>
#include "display.h"
#include "settings.h"
#include "history.h"
#include "leds.h"
#include "lcd.h"

struct settings g_set;
int stub_save_count;
int stub_reset_count;
int stub_hist_clear_count;
unsigned int stub_leds;
int stub_menu_line;

void stub_defaults(void)
{
	memset(&g_set, 0, sizeof(g_set));
	g_set.alarm_on = 1;
	g_set.low[0] = 800;
	g_set.high[0] = 1200;
	g_set.low[1] = 600;
	g_set.high[1] = 900;
}

void co110_settings_save(void)
{
	stub_save_count++;
}

void co110_settings_factory_reset(void)
{
	stub_reset_count++;
	stub_defaults();
}

void history_clear(void)
{
	stub_hist_clear_count++;
}

void leds_set(unsigned int mask)
{
	stub_leds = mask;
}

void lcd_fill(uint8_t value)
{
	ARG_UNUSED(value);
}

/* display functions: nothing to draw, only the menu line is recorded */
void disp_clear(void) {}
void disp_co2(int ppm, enum disp_co2_mode mode) { ARG_UNUSED(ppm); ARG_UNUSED(mode); }
void disp_co2_countdown(int n) { ARG_UNUSED(n); }
void disp_rh_text(char l, char r) { ARG_UNUSED(l); ARG_UNUSED(r); }
void disp_blank_co2(void) {}
void disp_blank_temp(void) {}
void disp_blank_rh(void) {}
void disp_temp(int tenths, bool f) { ARG_UNUSED(tenths); ARG_UNUSED(f); }
void disp_rh(int percent) { ARG_UNUSED(percent); }
void disp_chart(const int *v, int count, enum disp_quantity q, bool f, int sel, bool hide)
{
	ARG_UNUSED(v); ARG_UNUSED(count); ARG_UNUSED(q); ARG_UNUSED(f); ARG_UNUSED(sel);
	ARG_UNUSED(hide);
}
void disp_log_cursor(bool on) { ARG_UNUSED(on); }
void disp_zoom(int z) { ARG_UNUSED(z); }
void disp_menu(int item) { stub_menu_line = item; }
void disp_alarm(bool bell, bool muted) { ARG_UNUSED(bell); ARG_UNUSED(muted); }
void disp_preset(int preset) { ARG_UNUSED(preset); }
