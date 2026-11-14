/*
 * Flash ring of the chart history: the storage functions are replaced by a table in RAM.
 * A "power cycle" is history_load() after the RAM columns have been wiped.
 */
#include <string.h>
#include <zephyr/ztest.h>
#include "history.h"
#include "settings.h"
#include "ui.h"

struct ui_data co110_ud;

/* ---- RAM storage ---- */
#define MAX_ITEMS 80
static struct {
	char key[24];
	uint8_t data[16];
	size_t len;
	bool used;
} items[MAX_ITEMS];
static int put_count;

static int find(const char *key)
{
	for (int i = 0; i < MAX_ITEMS; i++) {
		if (items[i].used && strcmp(items[i].key, key) == 0) {
			return i;
		}
	}
	return -1;
}

bool co110_storage_get(const char *key, void *buf, size_t len)
{
	int i = find(key);

	if (i < 0 || items[i].len != len) {
		return false;
	}
	memcpy(buf, items[i].data, len);
	return true;
}

void co110_storage_put(const char *key, const void *buf, size_t len)
{
	int i = find(key);

	if (i < 0) {
		for (i = 0; i < MAX_ITEMS && items[i].used; i++) {
		}
		zassert_true(i < MAX_ITEMS, "storage table full");
		items[i].used = true;
		strcpy(items[i].key, key);
	}
	memcpy(items[i].data, buf, len);
	items[i].len = len;
	put_count++;
}

void co110_storage_delete(const char *key)
{
	int i = find(key);

	if (i >= 0) {
		items[i].used = false;
	}
}

void co110_storage_foreach(const char *prefix, co110_storage_cb cb, void *user)
{
	size_t n = strlen(prefix);

	for (int i = 0; i < MAX_ITEMS; i++) {
		if (items[i].used && strncmp(items[i].key, prefix, n) == 0 && items[i].key[n] == '/') {
			cb(&items[i].key[n + 1], items[i].data, items[i].len, user);
		}
	}
}

/* ---- helpers ---- */
static void before(void *f)
{
	ARG_UNUSED(f);
	memset(items, 0, sizeof(items));
	put_count = 0;
	memset(&co110_ud, 0, sizeof(co110_ud));
	history_load();
}

static void power_cycle(void)
{
	memset(&co110_ud, 0, sizeof(co110_ud));
	history_load();
}

/* sample n: CO2 = 1000 + n, RH = n % 100, temperature = 200 + n % 50 */
static void push_n(int first, int count)
{
	for (int n = first; n < first + count; n++) {
		history_push(1000 + n, n % 100, 200 + n % 50);
	}
}

ZTEST_SUITE(history, NULL, NULL, before, NULL, NULL);

ZTEST(history, test_nothing_stored_is_empty)
{
	for (int level = 0; level < 4; level++) {
		zassert_equal(co110_ud.hist_count[level], 0);
	}
}

ZTEST(history, test_one_record_per_step_and_16_bytes_of_flash)
{
	push_n(1, 59);
	zassert_equal(put_count, 0, "nothing is written before the 5 min step");
	push_n(60, 1);
	zassert_equal(put_count, 1);
	zassert_not_equal(find("co110/hist/1_00"), -1);
	zassert_equal(items[find("co110/hist/1_00")].len, 8, "8 byte record");
}

ZTEST(history, test_restore_after_power_cycle)
{
	push_n(1, 60 * 5);      /* five steps of the 5 min level */
	power_cycle();
	zassert_equal(co110_ud.hist_count[1], 5);
	zassert_equal(co110_ud.hist[1][DISP_Q_CO2][0], 1000 + 300, "newest step = sample 300");
	zassert_equal(co110_ud.hist[1][DISP_Q_CO2][4], 1000 + 60, "oldest step = sample 60");
	zassert_equal(co110_ud.hist[1][DISP_Q_RH][0], 300 % 100);
	zassert_equal(co110_ud.hist[1][DISP_Q_TEMP][0], 200 + 300 % 50);
	zassert_equal(co110_ud.hist_count[2], 0);
}

ZTEST(history, test_ring_wraps_and_keeps_17_columns)
{
	push_n(1, 60 * 40);     /* 40 steps: the 17 slots are overwritten more than twice */
	zassert_equal(co110_ud.hist_count[1], 17);
	power_cycle();
	zassert_equal(co110_ud.hist_count[1], 17);
	for (int c = 0; c < 17; c++) {
		zassert_equal(co110_ud.hist[1][DISP_Q_CO2][c], 1000 + 60 * (40 - c), "column %d", c);
	}
}

ZTEST(history, test_writing_continues_after_power_cycle)
{
	push_n(1, 60 * 20);
	power_cycle();
	push_n(1201, 60 * 3);   /* three more steps, counters start again after a power cycle */
	power_cycle();
	zassert_equal(co110_ud.hist_count[1], 17);
	zassert_equal(co110_ud.hist[1][DISP_Q_CO2][0], 1000 + 1380);
	zassert_equal(co110_ud.hist[1][DISP_Q_CO2][2], 1000 + 1260);
	zassert_equal(co110_ud.hist[1][DISP_Q_CO2][3], 1000 + 60 * 20, "step before the power cycle");
}

ZTEST(history, test_other_levels)
{
	push_n(1, 8640);        /* 12 h: 144 steps of 5 min, 6 of 2 h, 1 of 12 h */
	power_cycle();
	zassert_equal(co110_ud.hist_count[1], 17);
	zassert_equal(co110_ud.hist_count[2], 6);
	zassert_equal(co110_ud.hist_count[3], 1);
	zassert_equal(co110_ud.hist[3][DISP_Q_CO2][0], 1000 + 8640);
	zassert_equal(co110_ud.hist[2][DISP_Q_CO2][0], 1000 + 8640);
	zassert_equal(co110_ud.hist[2][DISP_Q_CO2][1], 1000 + 7200);
}

ZTEST(history, test_sequence_wraps_around)
{
	/* a ring written 70000 steps ago: sequence numbers wrapped, the newest is still found */
	for (int s = 0; s < 17; s++) {
		char key[16] = "co110/hist/1_00";
		uint16_t rec[4] = { (uint16_t)(65530 + s), (uint16_t)(2000 + s), 50, 210 };

		key[13] = '0' + s / 10;
		key[14] = '0' + s % 10;
		co110_storage_put(key, rec, sizeof(rec));
	}
	power_cycle();
	zassert_equal(co110_ud.hist_count[1], 17);
	zassert_equal(co110_ud.hist[1][DISP_Q_CO2][0], 2016, "newest = highest sequence number (65546 mod 65536)");
	zassert_equal(co110_ud.hist[1][DISP_Q_CO2][16], 2000);
}

ZTEST(history, test_broken_sequence_stops_the_column_chain)
{
	push_n(1, 60 * 10);
	co110_storage_delete("co110/hist/1_05");
	power_cycle();
	zassert_equal(co110_ud.hist_count[1], 4, "columns after the gap are dropped");
	zassert_equal(co110_ud.hist[1][DISP_Q_CO2][0], 1000 + 600);
}

ZTEST(history, test_clear_removes_everything)
{
	push_n(1, 8640);
	history_clear();
	zassert_equal(co110_ud.hist_count[1], 0);
	power_cycle();
	for (int level = 0; level < 4; level++) {
		zassert_equal(co110_ud.hist_count[level], 0);
	}
}
