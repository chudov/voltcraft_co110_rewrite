/*
 * Sensor threads. Both only talk to their sensor and post results to the UI thread.
 *   SHT30   every 4 s
 *   CM1106S every 5 s (also the chart sample period); after 40 polls without a valid frame
 *           the sensor is power cycled; zero point calibration on request of the UI.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "app.h"
#include "sensors.h"
#include "watchdog.h"

#define SHT_PERIOD_MS 4000
#define CO2_PERIOD_MS 5000
#define CO2_REPLY_WAIT_MS 300
#define CO2_MAX_FAILS 40
#define POWER_OFF_MS 500

#define U DT_PATH(zephyr_user)
static const struct gpio_dt_spec sensor_power = GPIO_DT_SPEC_GET(U, sensor_power_gpios);

K_SEM_DEFINE(cali_sem, 0, 1);
volatile bool co2_suspended;

static void sht_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int64_t next = k_uptime_get() + 500;
	int fails = 0;
	int wdt = wdt_channel(15000);

	while (1) {
		wdt_feed(wdt);
		k_sleep(K_TIMEOUT_ABS_MS(next));
		next += SHT_PERIOD_MS;

		int t, h;

		if (sht30_measure(&t, &h) == 0) {
			fails = 0;
			post(MSG_TH, t, h);
		} else if (++fails >= 5) {
			sht30_recover_bus();
			fails = 0;
		}
	}
}

static void co2_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	gpio_pin_configure_dt(&sensor_power, GPIO_OUTPUT_ACTIVE);
	cm1106_init();

	int64_t next = k_uptime_get() + 2000; /* give the sensor time to start */
	int fails = 0;
	int wdt = wdt_channel(20000);

	while (1) {
		wdt_feed(wdt);
		/* wake up for the next poll or earlier for a calibration request */
		if (k_sem_take(&cali_sem, K_TIMEOUT_ABS_MS(next)) == 0) {
			(void)cm1106_calibrate(400);
			next = k_uptime_get() + 2000;
			continue;
		}
		if (co2_suspended) {
			next = k_uptime_get() + 1000;
			continue;
		}

		int64_t start = k_uptime_get();
		int ppm;

		cm1106_request();
		k_msleep(CO2_REPLY_WAIT_MS);
		if (cm1106_get_result(&ppm)) {
			fails = 0;
			post(MSG_CO2, ppm, 0);
			next = start + CO2_PERIOD_MS;
		} else if (++fails >= CO2_MAX_FAILS) {
			fails = 0;
			post(MSG_CO2_LOST, 0, 0);
			gpio_pin_set_dt(&sensor_power, 0); /* power cycle the sensor */
			k_msleep(POWER_OFF_MS);
			gpio_pin_set_dt(&sensor_power, 1);
			next = k_uptime_get() + 2000;
		} else {
			next = start + CO2_PERIOD_MS;
		}
	}
}

K_THREAD_DEFINE(sht_tid, 1024, sht_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(co2_tid, 1024, co2_thread, NULL, NULL, NULL, 5, 0, 0);
