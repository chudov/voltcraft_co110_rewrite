#ifndef ALARM_H_
#define ALARM_H_

#include <stdbool.h>

/* Hardware independent CO2 zone logic of the original firmware. */
enum zone { ZONE_NONE = -1, ZONE_OK = 0, ZONE_WARN = 1, ZONE_ALERT = 2 };

#define PRESET_COUNT 10

/* user editable thresholds of preset 0 (human) and 1 (custom plant) */
void alarm_set_user_thresholds(int preset, int low, int high);

/* thresholds in ppm of a preset (0..9) */
void alarm_thresholds(int preset, int *low, int *high);

/*
 * preset 0 (human):  co2 < low OK, low <= co2 < high WARN, co2 >= high ALERT + buzzer
 * preset 1..9 (plant): low < co2 < high OK, co2 <= low ALERT + buzzer, co2 >= high WARN
 */
enum zone alarm_evaluate(int co2, int preset, bool *buzzer_request);

#endif
