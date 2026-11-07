#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include "sensors.h"

#define SHT30_ADDR 0x44

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c2));

static uint8_t crc8(const uint8_t *d, int n)
{
	uint8_t crc = 0xFF;

	for (int i = 0; i < n; i++) {
		crc ^= d[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
		}
	}
	return crc;
}

void sht30_recover_bus(void)
{
	(void)i2c_recover_bus(i2c);
}

int sht30_measure(int *temp_tenths, int *rh_percent)
{
	static const uint8_t cmd[2] = { 0x2C, 0x06 };
	uint8_t b[6];
	int ret;

	if (!device_is_ready(i2c)) {
		return -ENODEV;
	}
	ret = i2c_write(i2c, cmd, sizeof(cmd), SHT30_ADDR);
	if (ret != 0) {
		return ret;
	}
	k_msleep(20); /* high repeatability needs at most 15.5 ms */
	ret = i2c_read(i2c, b, sizeof(b), SHT30_ADDR);
	if (ret != 0) {
		return ret;
	}
	if (crc8(&b[0], 2) != b[2] || crc8(&b[3], 2) != b[5]) {
		return -EIO;
	}

	int32_t rt = (b[0] << 8) | b[1];
	int32_t rr = (b[3] << 8) | b[4];

	*temp_tenths = (rt * 1750 + 32767) / 65535 - 450; /* -45 + 175 * raw / 65535, in 0.1 degC */
	*rh_percent = (rr * 100 + 32767) / 65535;
	return 0;
}
