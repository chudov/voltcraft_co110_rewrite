#include <string.h>
#include "history.h"
#include "settings.h"
#include "ui.h"

static const char *const hist_key[4] = { NULL, "co110/hist1", "co110/hist2", "co110/hist3" };
#define MAGIC 0x4849

struct blob {
	uint16_t magic;
	uint16_t count;
	int16_t v[3][DISP_CHART_COLS];
};

static uint32_t pushes;

static void shift_in(int level, int co2, int rh, int t)
{
	for (int q = 0; q < 3; q++) {
		for (int c = DISP_CHART_COLS - 1; c > 0; c--) {
			ud.hist[level][q][c] = ud.hist[level][q][c - 1];
		}
	}
	ud.hist[level][DISP_Q_CO2][0] = co2;
	ud.hist[level][DISP_Q_RH][0] = rh;
	ud.hist[level][DISP_Q_TEMP][0] = t;
	if (ud.hist_count[level] < DISP_CHART_COLS) {
		ud.hist_count[level]++;
	}
}

static void save_level(int level)
{
	struct blob b = { .magic = MAGIC, .count = ud.hist_count[level] };

	for (int q = 0; q < 3; q++) {
		for (int c = 0; c < DISP_CHART_COLS; c++) {
			b.v[q][c] = (int16_t)ud.hist[level][q][c];
		}
	}
	storage_put(hist_key[level], &b, sizeof(b));
}

void history_load(void)
{
	for (int level = 1; level < 4; level++) {
		struct blob b;

		if (storage_get(hist_key[level], &b, sizeof(b)) && b.magic == MAGIC &&
		    b.count <= DISP_CHART_COLS) {
			ud.hist_count[level] = b.count;
			for (int q = 0; q < 3; q++) {
				for (int c = 0; c < DISP_CHART_COLS; c++) {
					ud.hist[level][q][c] = b.v[q][c];
				}
			}
		}
	}
}

void history_push(int co2, int rh, int tenths)
{
	pushes++;
	shift_in(0, co2, rh, tenths);
	if (pushes % 60 == 0) {        /* 60 x 5 s = 5 min */
		shift_in(1, co2, rh, tenths);
		save_level(1);
	}
	if (pushes % 1440 == 0) {      /* 2 h */
		shift_in(2, co2, rh, tenths);
		save_level(2);
	}
	if (pushes % 8640 == 0) {      /* 12 h */
		shift_in(3, co2, rh, tenths);
		save_level(3);
	}
}

void history_clear(void)
{
	memset(ud.hist, 0, sizeof(ud.hist));
	memset(ud.hist_count, 0, sizeof(ud.hist_count));
	pushes = 0;
	for (int level = 1; level < 4; level++) {
		storage_delete(hist_key[level]);
	}
}
