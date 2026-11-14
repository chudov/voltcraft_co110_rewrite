#include <stdint.h>
#include <string.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include "history.h"
#include "settings.h"
#include "ui.h"

#define SLOTS DISP_CHART_COLS
#define PREFIX "co110/hist"

/* one step of one level; 8 bytes = the size of one NVS data unit, so nothing is wasted */
struct rec {
	uint16_t seq;  /* counts the steps of the level, wraps around */
	int16_t co2;
	int16_t rh;
	int16_t temp;  /* 0.1 degC, offset corrected */
} __packed;

_Static_assert(sizeof(struct rec) == 8, "one record is one 8 byte NVS data unit");

static uint32_t pushes;
static uint16_t next_seq[4];
static uint8_t next_slot[4];

static void slot_key(char key[16], int level, int slot)
{
	memcpy(key, PREFIX "/", sizeof(PREFIX));
	key[11] = (char)('0' + level);
	key[12] = '_';
	key[13] = (char)('0' + slot / 10);
	key[14] = (char)('0' + slot % 10);
	key[15] = '\0';
}

static void write_record(int level, int co2, int rh, int t)
{
	struct rec r = { .seq = next_seq[level], .co2 = (int16_t)co2, .rh = (int16_t)rh, .temp = (int16_t)t };
	char key[16];

	slot_key(key, level, next_slot[level]);
	co110_storage_put(key, &r, sizeof(r));
	next_slot[level] = (next_slot[level] + 1) % SLOTS;
	next_seq[level]++;
}

static void shift_in(int level, int co2, int rh, int t)
{
	for (int q = 0; q < 3; q++) {
		for (int c = DISP_CHART_COLS - 1; c > 0; c--) {
			co110_ud.hist[level][q][c] = co110_ud.hist[level][q][c - 1];
		}
	}
	co110_ud.hist[level][DISP_Q_CO2][0] = co2;
	co110_ud.hist[level][DISP_Q_RH][0] = rh;
	co110_ud.hist[level][DISP_Q_TEMP][0] = t;
	if (co110_ud.hist_count[level] < DISP_CHART_COLS) {
		co110_ud.hist_count[level]++;
	}
}

/* records read from the flash, by level and slot */
static struct rec tab[4][SLOTS];
static uint32_t present[4];

static void load_cb(const char *key, const void *buf, size_t len, void *user)
{
	ARG_UNUSED(user);

	/* key: "<level>_<slot>" */
	if (key == NULL || len != sizeof(struct rec) || key[0] < '1' || key[0] > '3' || key[1] != '_' ||
	    key[2] < '0' || key[2] > '9' || key[3] < '0' || key[3] > '9' || key[4] != '\0') {
		return;
	}
	int level = key[0] - '0';
	int slot = (key[2] - '0') * 10 + (key[3] - '0');

	if (slot < SLOTS) {
		memcpy(&tab[level][slot], buf, sizeof(struct rec));
		present[level] |= 1u << slot;
	}
}

/* rebuild the columns of one level: the newest record first, going back while the sequence is complete */
static void restore_level(int level)
{
	int best = -1;

	next_seq[level] = 0;
	next_slot[level] = 0;
	co110_ud.hist_count[level] = 0;
	for (int s = 0; s < SLOTS; s++) {
		if ((present[level] & (1u << s)) &&
		    (best < 0 || (int16_t)(tab[level][s].seq - tab[level][best].seq) > 0)) {
			best = s;
		}
	}
	if (best < 0) {
		return;
	}
	for (int c = 0; c < SLOTS; c++) {
		int s = (best - c + SLOTS) % SLOTS;
		const struct rec *r = &tab[level][s];

		if (!(present[level] & (1u << s)) || r->seq != (uint16_t)(tab[level][best].seq - c)) {
			break;
		}
		co110_ud.hist[level][DISP_Q_CO2][c] = r->co2;
		co110_ud.hist[level][DISP_Q_RH][c] = r->rh;
		co110_ud.hist[level][DISP_Q_TEMP][c] = r->temp;
		co110_ud.hist_count[level] = c + 1;
	}
	next_slot[level] = (best + 1) % SLOTS;
	next_seq[level] = tab[level][best].seq + 1;
}

void history_load(void)
{
	pushes = 0;
	memset(present, 0, sizeof(present));
	co110_storage_foreach(PREFIX, load_cb, NULL);
	for (int level = 1; level < 4; level++) {
		restore_level(level);
	}
}

void history_push(int co2, int rh, int tenths)
{
	pushes++;
	shift_in(0, co2, rh, tenths);
	if (pushes % 60 == 0) {        /* 60 x 5 s = 5 min */
		shift_in(1, co2, rh, tenths);
		write_record(1, co2, rh, tenths);
	}
	if (pushes % 1440 == 0) {      /* 2 h */
		shift_in(2, co2, rh, tenths);
		write_record(2, co2, rh, tenths);
	}
	if (pushes % 8640 == 0) {      /* 12 h */
		shift_in(3, co2, rh, tenths);
		write_record(3, co2, rh, tenths);
	}
}

void history_clear(void)
{
	char key[16];

	memset(co110_ud.hist, 0, sizeof(co110_ud.hist));
	memset(co110_ud.hist_count, 0, sizeof(co110_ud.hist_count));
	pushes = 0;
	for (int level = 1; level < 4; level++) {
		for (int s = 0; s < SLOTS; s++) {
			slot_key(key, level, s);
			co110_storage_delete(key);
		}
		next_seq[level] = 0;
		next_slot[level] = 0;
	}
}
