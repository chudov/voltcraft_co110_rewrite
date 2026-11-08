#include "alarm.h"

struct th {
	int low;
	int high;
};

static struct th table[PRESET_COUNT] = {
	{ 800, 1200 },   /* 0 human, user editable */
	{ 600, 900 },    /* 1 custom plant, user editable */
	{ 600, 900 },
	{ 800, 1000 },
	{ 1000, 1500 },
	{ 800, 1400 },
	{ 800, 1400 },
	{ 1200, 1800 },
	{ 800, 1200 },
	{ 800, 1200 },
};

void alarm_set_user_thresholds(int preset, int low, int high)
{
	if (preset == 0 || preset == 1) {
		table[preset].low = low;
		table[preset].high = high;
	}
}

void alarm_thresholds(int preset, int *low, int *high)
{
	if (preset < 0 || preset >= PRESET_COUNT) {
		preset = 0;
	}
	*low = table[preset].low;
	*high = table[preset].high;
}

enum zone alarm_evaluate(int co2, int preset, bool *buzzer_request)
{
	int low, high;

	alarm_thresholds(preset, &low, &high);
	*buzzer_request = false;

	if (preset == 0) {
		if (co2 < low) {
			return ZONE_OK;
		}
		if (co2 < high) {
			return ZONE_WARN;
		}
		*buzzer_request = true;
		return ZONE_ALERT;
	}

	if (co2 >= high) {
		return ZONE_WARN;
	}
	if (co2 > low) {
		return ZONE_OK;
	}
	*buzzer_request = true;
	return ZONE_ALERT;
}
