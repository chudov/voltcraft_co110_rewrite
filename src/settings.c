#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include "settings.h"
#include "alarm.h"

#define NVS_ID_SETTINGS 1

struct settings g_set;
static struct nvs_fs fs;
static bool ready;

static void apply(void)
{
	alarm_set_user_thresholds(0, g_set.low[0], g_set.high[0]);
	alarm_set_user_thresholds(1, g_set.low[1], g_set.high[1]);
}

static void defaults(void)
{
	memset(&g_set, 0, sizeof(g_set));
	g_set.magic = SETTINGS_MAGIC;
	g_set.alarm_on = 1;
	g_set.low[0] = 800;
	g_set.high[0] = 1200;
	g_set.low[1] = 600;
	g_set.high[1] = 900;
}

void settings_save(void)
{
	if (ready) {
		(void)nvs_write(&fs, NVS_ID_SETTINGS, &g_set, sizeof(g_set));
	}
}

void settings_factory_reset(void)
{
	defaults();
	apply();
	settings_save();
}

int settings_init(void)
{
	struct flash_pages_info info;
	struct settings tmp;
	int rc;

	defaults();
	fs.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
	if (!device_is_ready(fs.flash_device)) {
		apply();
		return -ENODEV;
	}
	fs.offset = FIXED_PARTITION_OFFSET(storage_partition);
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc != 0) {
		apply();
		return rc;
	}
	fs.sector_size = info.size;
	fs.sector_count = FIXED_PARTITION_SIZE(storage_partition) / info.size;
	rc = nvs_mount(&fs);
	if (rc != 0) {
		apply();
		return rc;
	}
	ready = true;

	rc = nvs_read(&fs, NVS_ID_SETTINGS, &tmp, sizeof(tmp));
	if (rc == (int)sizeof(tmp) && tmp.magic == SETTINGS_MAGIC && tmp.preset < PRESET_COUNT) {
		g_set = tmp;
	} else {
		settings_save(); /* first start: store the defaults */
	}
	apply();
	return 0;
}
