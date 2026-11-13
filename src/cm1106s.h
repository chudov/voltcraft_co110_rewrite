/*
 * Cubic CM1106S CO2 sensor: driver specific parts of the Zephyr sensor API.
 * Standard parts: sensor_sample_fetch_chan() / sensor_channel_get(SENSOR_CHAN_CO2) in ppm,
 * sensor_attr_set(SENSOR_CHAN_CO2, SENSOR_ATTR_CALIBRATION) = zero point calibration (see driver).
 */
#ifndef CM1106S_H_
#define CM1106S_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

/* Auto calibration (ABC), command 0x10. Use with sensor_attr_set() / sensor_attr_get(),
 * channel SENSOR_CHAN_CO2. The sensor has no command to read these values back, so the
 * driver keeps the values it has sent (datasheet defaults until something is set). */
enum sensor_attribute_cm1106s {
	/* val1: 1 = ABC on, 0 = ABC off (sensor default: on) */
	SENSOR_ATTR_CM1106S_ABC_ENABLE = SENSOR_ATTR_PRIV_START,
	/* val1: calibration cycle in days, 1 to 30 (sensor default: 7) */
	SENSOR_ATTR_CM1106S_ABC_CYCLE_DAYS,
	/* val1: ABC base value in ppm (sensor default: 400) */
	SENSOR_ATTR_CM1106S_ABC_BASE_PPM,
};

#define CM1106S_VERSION_LEN 11 /* 10 ASCII characters + NUL */

/* serial number: five numbers of 0 to 9999 each (a 20 digit number) */
int cm1106s_read_serial(const struct device *dev, uint16_t sn[5]);

/* software version string, e.g. "CM V0.0.20" */
int cm1106s_read_version(const struct device *dev, char version[CM1106S_VERSION_LEN]);

#endif
