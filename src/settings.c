#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include "settings.h"
#include "alarm.h"

/* all items live under the "co110/" subtree of the Zephyr settings subsystem */
#define SUBTREE "co110"
#define K_ALARM SUBTREE "/alarm"
#define K_UNIT SUBTREE "/unit"
#define K_PRESET SUBTREE "/preset"
#define K_DARK SUBTREE "/dark"
#define K_TOFF SUBTREE "/temp_offset"
#define K_RHOFF SUBTREE "/rh_offset"
#define K_THR SUBTREE "/thresholds" /* low0, high0, low1, high1 */

struct settings g_set;
static bool ready;

/* first error of the last init/save (0 = none); readable with a debugger */
volatile int co110_settings_err;

static void apply(void)
{
	alarm_set_user_thresholds(0, g_set.low[0], g_set.high[0]);
	alarm_set_user_thresholds(1, g_set.low[1], g_set.high[1]);
}

static void defaults(void)
{
	memset(&g_set, 0, sizeof(g_set));
	g_set.alarm_on = 1;
	g_set.low[0] = 800;
	g_set.high[0] = 1200;
	g_set.low[1] = 600;
	g_set.high[1] = 900;
}

/* ---- loading: the standard handler of the "co110" subtree (called by settings_load_subtree) ---- */

static bool read_exact(settings_read_cb read_cb, void *cb_arg, size_t vlen, void *dst, size_t len)
{
	return vlen == len && read_cb(cb_arg, dst, len) == (ssize_t)len;
}

static void get_u8(settings_read_cb cb, void *arg, size_t vlen, uint8_t *dst, uint8_t max)
{
	uint8_t v;

	if (read_exact(cb, arg, vlen, &v, sizeof(v)) && v <= max) {
		*dst = v;
	}
}

static void get_i16(settings_read_cb cb, void *arg, size_t vlen, int16_t *dst, int16_t lo,
		    int16_t hi)
{
	int16_t v;

	if (read_exact(cb, arg, vlen, &v, sizeof(v)) && v >= lo && v <= hi) {
		*dst = v;
	}
}

static int co110_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	/* invalid or unknown items are skipped (return 0), the defaults stay */
	if (settings_name_steq(name, "alarm", &next) && !next) {
		get_u8(read_cb, cb_arg, len, &g_set.alarm_on, 1);
	} else if (settings_name_steq(name, "unit", &next) && !next) {
		get_u8(read_cb, cb_arg, len, &g_set.unit_f, 1);
	} else if (settings_name_steq(name, "preset", &next) && !next) {
		get_u8(read_cb, cb_arg, len, &g_set.preset, PRESET_COUNT - 1);
	} else if (settings_name_steq(name, "dark", &next) && !next) {
		get_u8(read_cb, cb_arg, len, &g_set.dark_mode, 1);
	} else if (settings_name_steq(name, "temp_offset", &next) && !next) {
		get_i16(read_cb, cb_arg, len, &g_set.temp_offset, -1000, 1000);
	} else if (settings_name_steq(name, "rh_offset", &next) && !next) {
		get_i16(read_cb, cb_arg, len, &g_set.rh_offset, -99, 99);
	} else if (settings_name_steq(name, "thresholds", &next) && !next) {
		uint16_t thr[4];

		if (read_exact(read_cb, cb_arg, len, thr, sizeof(thr)) && thr[0] < thr[1] &&
		    thr[2] < thr[3]) {
			g_set.low[0] = thr[0];
			g_set.high[0] = thr[1];
			g_set.low[1] = thr[2];
			g_set.high[1] = thr[3];
		}
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(co110, SUBTREE, NULL, co110_set, NULL, NULL);

/* ---- saving ---- */

static void put(int *first_err, const char *key, const void *val, size_t len)
{
	int rc = settings_save_one(key, val, len);

	if (rc != 0 && *first_err == 0) {
		*first_err = rc;
	}
}

static int save_all(void)
{
	int err = 0;
	uint16_t thr[4] = { g_set.low[0], g_set.high[0], g_set.low[1], g_set.high[1] };

	if (!ready) {
		return -ENODEV;
	}
	put(&err, K_ALARM, &g_set.alarm_on, sizeof(g_set.alarm_on));
	put(&err, K_UNIT, &g_set.unit_f, sizeof(g_set.unit_f));
	put(&err, K_PRESET, &g_set.preset, sizeof(g_set.preset));
	put(&err, K_DARK, &g_set.dark_mode, sizeof(g_set.dark_mode));
	put(&err, K_TOFF, &g_set.temp_offset, sizeof(g_set.temp_offset));
	put(&err, K_RHOFF, &g_set.rh_offset, sizeof(g_set.rh_offset));
	put(&err, K_THR, thr, sizeof(thr));
	return err;
}

void co110_settings_save(void)
{
	int rc = save_all();

	if (rc != 0) {
		co110_settings_err = rc;
	}
}

void co110_settings_factory_reset(void)
{
	defaults();
	apply();
	co110_settings_save();
}

/* returns 0 or the first error (settings init, load or the first save); the settings stay usable */
int co110_settings_init(void)
{
	int rc;

	co110_settings_err = 0;
	defaults();
	rc = settings_subsys_init();
	if (rc != 0) {
		co110_settings_err = rc;
		apply();
		return rc;
	}
	ready = true;

	rc = settings_load_subtree(SUBTREE);
	if (rc != 0) {
		co110_settings_err = rc;
	}
	apply();

	int rc_save = save_all(); /* stores the defaults of items that are still missing */

	if (rc_save != 0) {
		co110_settings_err = rc_save;
		return rc_save;
	}
	return rc;
}

/* ---- raw items for the history ---- */

struct get_ctx {
	void *buf;
	size_t len;
	bool ok;
};

static int get_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	struct get_ctx *c = param;

	ARG_UNUSED(key);
	if (read_exact(read_cb, cb_arg, len, c->buf, c->len)) {
		c->ok = true;
	}
	return 0;
}

bool co110_storage_get(const char *key, void *buf, size_t len)
{
	struct get_ctx c = { .buf = buf, .len = len, .ok = false };

	if (!ready) {
		return false;
	}
	(void)settings_load_subtree_direct(key, get_cb, &c);
	return c.ok;
}

void co110_storage_put(const char *key, const void *buf, size_t len)
{
	if (ready) {
		(void)settings_save_one(key, buf, len);
	}
}

void co110_storage_delete(const char *key)
{
	if (ready) {
		(void)settings_delete(key);
	}
}
