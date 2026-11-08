#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_MAGIC 0xC0A1

struct settings {
	uint16_t magic;
	uint8_t alarm_on;       /* 1 = alarm beeper enabled */
	uint8_t unit_f;         /* 0 = degC, 1 = degF */
	uint8_t preset;         /* 0 = human, 1 = custom plant, 2..9 = plant presets */
	uint8_t reserved;
	int16_t temp_offset;    /* 0.1 degC added to the measured temperature */
	int16_t rh_offset;      /* % added to the measured humidity */
	uint16_t low[2];        /* user thresholds of preset 0 and 1 (ppm) */
	uint16_t high[2];
};

extern struct settings g_set;

/* mount the storage and load the settings (defaults when empty or invalid), then apply them */
int settings_init(void);
void settings_save(void);
void settings_factory_reset(void);

/* raw access to the NVS storage for other modules (history); ids 1 = settings, 10..13 = history */
bool storage_get(uint16_t id, void *buf, size_t len);
void storage_put(uint16_t id, const void *buf, size_t len);
void storage_delete(uint16_t id);

#endif
