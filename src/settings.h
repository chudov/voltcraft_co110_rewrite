#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Settings in RAM; stored with the Zephyr settings subsystem, one key per item under "co110/". */
struct settings {
	uint8_t alarm_on;       /* 1 = alarm beeper enabled */
	uint8_t unit_f;         /* 0 = degC, 1 = degF */
	uint8_t preset;         /* 0 = human, 1 = custom plant, 2..9 = plant presets */
	uint8_t dark_mode;      /* 1 = dark mode: LEDs dimmed, no key click / start-up beep */
	int16_t temp_offset;    /* 0.1 degC added to the measured temperature */
	int16_t rh_offset;      /* % added to the measured humidity */
	uint16_t low[2];        /* user thresholds of preset 0 and 1 (ppm) */
	uint16_t high[2];
};

extern struct settings g_set;

/* init the settings subsystem and load the items (defaults for missing or invalid ones), apply them */
int co110_settings_init(void);
/* store all items that changed (the settings subsystem skips unchanged values) */
void co110_settings_save(void);
void co110_settings_factory_reset(void);

/* raw items for other modules (history), keys like "co110/hist1" */
bool co110_storage_get(const char *key, void *buf, size_t len);
void co110_storage_put(const char *key, const void *buf, size_t len);
void co110_storage_delete(const char *key);

#endif
