/*
 * Sensor threads. Both only talk to their sensor and post results to the UI thread.
 *   SHT30   every 4 s
 *   CM1106S every 5 s (also the chart sample period); after 40 polls without a valid frame
 *           the sensor is power cycled (PM suspend/resume of the driver); zero point calibration
 *           on request of the UI. Driver: src/cm1106s.c (Zephyr sensor API).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include "app.h"
#include "sensors.h"
#include "watchdog.h"

#define SHT_PERIOD_MS 4000
#define CO2_PERIOD_MS 5000
#define CO2_MAX_FAILS 40
#define POWER_OFF_MS 500
#define CALI_ATTEMPTS 10

static const struct device *const co2_dev = DEVICE_DT_GET(DT_NODELABEL(cm1106s));

K_SEM_DEFINE(cali_sem, 0, 1);
volatile bool co2_suspended;

static void sht_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int64_t next = k_uptime_get() + 500;
	int fails = 0;
	int wdt = co110_wdt_channel(15000);

	while (1) {
		co110_wdt_feed(wdt);
		k_sleep(K_TIMEOUT_ABS_MS(next));
		next += SHT_PERIOD_MS;

		int t, h;

		if (sht30_measure(&t, &h) == 0) {
			fails = 0;
			co110_post(MSG_TH, t, h);
		} else if (++fails >= 5) {
			sht30_recover_bus();
			fails = 0;
		}
	}
}

static bool co2_calibrate(int ppm)
{
	struct sensor_value ref = { .val1 = ppm, .val2 = 0 };

	for (int attempt = 0; attempt < CALI_ATTEMPTS; attempt++) {
		if (sensor_attr_set(co2_dev, SENSOR_CHAN_CO2, SENSOR_ATTR_CALIBRATION, &ref) == 0) {
			return true;
		}
	}
	return false;
}

static void co2_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int wdt = co110_wdt_channel(20000);

	if (!device_is_ready(co2_dev)) {
		co110_post(MSG_CO2_LOST, 0, 0);
		while (1) { /* driver init failed (UART or supply not ready): nothing to poll */
			co110_wdt_feed(wdt);
			k_msleep(1000);
		}
	}

	int64_t next = k_uptime_get() + 2000; /* give the sensor time to start */
	int fails = 0;

	while (1) {
		co110_wdt_feed(wdt);
		/* wake up for the next poll or earlier for a calibration request */
		if (k_sem_take(&cali_sem, K_TIMEOUT_ABS_MS(next)) == 0) {
			(void)co2_calibrate(400);
			next = k_uptime_get() + 2000;
			continue;
		}
		if (co2_suspended) {
			next = k_uptime_get() + 1000;
			continue;
		}

		int64_t start = k_uptime_get();
		struct sensor_value v;

		if (sensor_sample_fetch_chan(co2_dev, SENSOR_CHAN_CO2) == 0 &&
		    sensor_channel_get(co2_dev, SENSOR_CHAN_CO2, &v) == 0) {
			fails = 0;
			co110_post(MSG_CO2, v.val1, 0);
			next = start + CO2_PERIOD_MS;
		} else if (++fails >= CO2_MAX_FAILS) {
			fails = 0;
			co110_post(MSG_CO2_LOST, 0, 0);
			/* power cycle the sensor: the driver switches its supply regulator */
			(void)pm_device_action_run(co2_dev, PM_DEVICE_ACTION_SUSPEND);
			k_msleep(POWER_OFF_MS);
			(void)pm_device_action_run(co2_dev, PM_DEVICE_ACTION_RESUME);
			next = k_uptime_get() + 2000;
		} else {
			next = start + CO2_PERIOD_MS;
		}
	}
}

K_THREAD_DEFINE(sht_tid, 1024, sht_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(co2_tid, 1024, co2_thread, NULL, NULL, NULL, 5, 0, 0);
