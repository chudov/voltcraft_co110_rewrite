/*
 * SHT30 temperature/humidity through Zephyr's standard sensor driver
 * (compatible "sensirion,sht3xd", node `sht30` on I2C2 at 0x44). The driver does the
 * single-shot measurement and checks both CRC bytes.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include "sensors.h"

static const struct device *const sht = DEVICE_DT_GET(DT_NODELABEL(sht30));
static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c2));

void sht30_recover_bus(void)
{
	(void)i2c_recover_bus(i2c);
}

int sht30_measure(int *temp_tenths, int *rh_percent)
{
	struct sensor_value t, h;
	int ret;

	if (!device_is_ready(sht)) {
		return -ENODEV;
	}
	ret = sensor_sample_fetch(sht);
	if (ret != 0) {
		return ret;
	}
	ret = sensor_channel_get(sht, SENSOR_CHAN_AMBIENT_TEMP, &t);
	if (ret != 0) {
		return ret;
	}
	ret = sensor_channel_get(sht, SENSOR_CHAN_HUMIDITY, &h);
	if (ret != 0) {
		return ret;
	}

	int64_t mt = sensor_value_to_milli(&t); /* milli degC */
	int64_t mh = sensor_value_to_milli(&h); /* milli %RH */

	*temp_tenths = (int)((mt + (mt >= 0 ? 50 : -50)) / 100);
	*rh_percent = (int)((mh + 500) / 1000);
	return 0;
}
