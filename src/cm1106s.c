/*
 * Cubic CM1106S CO2 sensor driver (UART, 9600 8N1), Zephyr sensor API.
 * Protocol: datasheet CM1106S-W V0.1 (2020-06-18), section "UART Communication Protocol".
 *
 *   request  11 LEN CMD DATA.. CS     LEN = 1 + number of data bytes
 *   reply    16 LEN CMD DATA.. CS
 *   CS = 256 - (sum of all bytes before it) mod 256, i.e. the sum of a whole frame is 0 mod 256
 *
 *   CMD 0x01  read CO2         request 11 01 01 ED      reply 16 05 01 D1 D2 D3 D4 CS, CO2 = D1*256 + D2 ppm
 *   CMD 0x03  calibrate        request 11 03 03 HI LO CS  (target 400 to 1500 ppm)   reply 16 01 03 E6
 *   CMD 0x10  ABC settings     request 11 07 10 64 EN DAYS BH BL 64 CS               reply 16 01 10 D9
 *   CMD 0x1F  serial number    request 11 01 1F CF      reply 16 0B 1F SN1..SN5 (2 bytes each) CS
 *   CMD 0x1E  software version request 11 01 1E D0      reply 16 0C 1E 10 ASCII bytes, 1 reserved, CS
 *
 * API:
 *   sensor_sample_fetch_chan(dev, SENSOR_CHAN_CO2) + sensor_channel_get(): CO2 in ppm (val1)
 *   sensor_attr_set(dev, SENSOR_CHAN_CO2, SENSOR_ATTR_CALIBRATION, val1 = reference ppm)
 *   ABC attributes and serial number / version functions: see cm1106s.h
 * Power: PM_DEVICE_ACTION_SUSPEND / RESUME switch the optional vin-supply regulator.
 *
 * Not built or tested on the device when this was written (the read and calibration commands
 * are the ones the original firmware uses; ABC, serial number and version are new).
 */
#define DT_DRV_COMPAT cubic_cm1106s

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/ring_buffer.h>
#include "cm1106s.h"

#define RX_SIZE            64
#define MEASURE_TIMEOUT_MS 300
#define CMD_TIMEOUT_MS     300 /* every other command; the original firmware waited 50 ms per calibration try */

#define FRAME_REQ  0x11
#define FRAME_RSP  0x16
#define CMD_READ_CO2  0x01
#define CMD_CALIBRATE 0x03
#define CMD_ABC       0x10
#define CMD_VERSION   0x1E
#define CMD_SERIAL    0x1F

#define MAX_TX_DATA 8
#define MAX_RX_DATA 11

#define CAL_MIN_PPM 400
#define CAL_MAX_PPM 1500
#define ABC_MIN_DAYS 1
#define ABC_MAX_DAYS 30

/* datasheet defaults of the auto calibration */
#define ABC_DEFAULT_ENABLE 1
#define ABC_DEFAULT_DAYS   7
#define ABC_DEFAULT_BASE   400

struct cm1106s_abc {
	uint8_t enable; /* 1 = on */
	uint8_t days;
	uint16_t base_ppm;
};

struct cm1106s_config {
	const struct device *uart;
	const struct device *supply; /* NULL when the sensor is always powered */
	bool abc_from_dt;            /* send the ABC setting after every power up */
	struct cm1106s_abc abc_dt;
};

struct cm1106s_data {
	struct ring_buf rb;           /* ISR is the only producer, the caller the only consumer */
	uint8_t rb_mem[RX_SIZE];
	struct k_sem rx_sem;          /* given by the ISR for every received chunk */
	struct k_mutex lock;          /* one transaction at a time */
	int32_t ppm;
	bool active;
	struct cm1106s_abc abc;       /* what the sensor has been told last (it cannot be read back) */
	bool abc_pending;             /* send the ABC setting after the next successful reading */
};

static uint8_t cm1106s_sum(const uint8_t *f, size_t len)
{
	uint8_t sum = 0;

	for (size_t i = 0; i < len; i++) {
		sum += f[i];
	}
	return sum;
}

static void cm1106s_isr(const struct device *uart, void *user)
{
	const struct device *dev = user;
	struct cm1106s_data *data = dev->data;

	uart_irq_update(uart);
	while (uart_irq_rx_ready(uart)) {
		uint8_t c[16];
		int n = uart_fifo_read(uart, c, sizeof(c));

		if (n <= 0) {
			break;
		}
		ring_buf_put(&data->rb, c, n); /* a full buffer drops the new bytes */
		k_sem_give(&data->rx_sem);
	}
}

/* drop everything received so far; the RX interrupt is off while the buffer is reset */
static void cm1106s_flush(const struct device *dev)
{
	const struct cm1106s_config *cfg = dev->config;
	struct cm1106s_data *data = dev->data;
	uint8_t c;

	uart_irq_rx_disable(cfg->uart);
	while (uart_fifo_read(cfg->uart, &c, 1) > 0) {
	}
	ring_buf_reset(&data->rb);
	k_sem_reset(&data->rx_sem);
	uart_irq_rx_enable(cfg->uart);
}

/*
 * One command: send 11 LEN CMD tx.. CS, wait for 16 LEN CMD rx.. CS (rx_len data bytes).
 * check_cs: verify the checksum of the reply. The caller holds the lock.
 */
static int cm1106s_cmd_locked(const struct device *dev, uint8_t cmd, const uint8_t *tx,
			      size_t tx_len, uint8_t *rx, size_t rx_len, int timeout_ms,
			      bool check_cs)
{
	const struct cm1106s_config *cfg = dev->config;
	struct cm1106s_data *data = dev->data;
	uint8_t req[4 + MAX_TX_DATA];
	const uint8_t head[3] = { FRAME_RSP, (uint8_t)(rx_len + 1), cmd };
	const size_t req_len = 4 + tx_len;
	const size_t rsp_len = 4 + rx_len;
	uint8_t buf[RX_SIZE];
	size_t n = 0;

	if (tx_len > MAX_TX_DATA || rx_len > MAX_RX_DATA) {
		return -EINVAL;
	}
	if (!data->active) {
		return -EACCES; /* suspended: sensor not powered */
	}

	req[0] = FRAME_REQ;
	req[1] = (uint8_t)(tx_len + 1);
	req[2] = cmd;
	if (tx_len > 0) {
		memcpy(&req[3], tx, tx_len);
	}
	req[req_len - 1] = (uint8_t)(0x100 - cm1106s_sum(req, req_len - 1));

	cm1106s_flush(dev);
	for (size_t i = 0; i < req_len; i++) {
		uart_poll_out(cfg->uart, req[i]);
	}

	int64_t end = k_uptime_get() + timeout_ms;

	while (1) {
		n += ring_buf_get(&data->rb, buf + n, sizeof(buf) - n);
		for (size_t i = 0; i + rsp_len <= n; i++) {
			if (memcmp(&buf[i], head, 3) == 0 &&
			    (!check_cs || cm1106s_sum(&buf[i], rsp_len) == 0)) {
				if (rx_len > 0) {
					memcpy(rx, &buf[i + 3], rx_len);
				}
				return 0;
			}
		}
		int64_t left = end - k_uptime_get();

		if (n >= sizeof(buf) || left <= 0) {
			return (n > 0) ? -EBADMSG : -ETIMEDOUT; /* bytes seen, but no valid frame */
		}
		(void)k_sem_take(&data->rx_sem, K_MSEC(left));
	}
}

static int cm1106s_cmd(const struct device *dev, uint8_t cmd, const uint8_t *tx, size_t tx_len,
		       uint8_t *rx, size_t rx_len, int timeout_ms, bool check_cs)
{
	struct cm1106s_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = cm1106s_cmd_locked(dev, cmd, tx, tx_len, rx, rx_len, timeout_ms, check_cs);
	k_mutex_unlock(&data->lock);
	return ret;
}

/* send the ABC setting: 11 07 10 64 EN DAYS BH BL 64 CS, reply 16 01 10 D9 */
static int cm1106s_send_abc(const struct device *dev, const struct cm1106s_abc *abc)
{
	const uint8_t tx[6] = { 0x64, abc->enable ? 0 : 2, abc->days, (uint8_t)(abc->base_ppm >> 8),
				(uint8_t)abc->base_ppm, 0x64 };

	return cm1106s_cmd(dev, CMD_ABC, tx, sizeof(tx), NULL, 0, CMD_TIMEOUT_MS, true);
}

static int cm1106s_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct cm1106s_data *data = dev->data;
	uint8_t r[4];
	int ret;

	if (chan != SENSOR_CHAN_CO2 && chan != SENSOR_CHAN_ALL) {
		return -ENOTSUP;
	}
	ret = cm1106s_cmd(dev, CMD_READ_CO2, NULL, 0, r, sizeof(r), MEASURE_TIMEOUT_MS, true);
	if (ret != 0) {
		return ret;
	}
	data->ppm = (r[0] << 8) | r[1]; /* r[2], r[3] are reserved */

	/* the sensor answers now: send the ABC setting that has to follow every power up */
	if (data->abc_pending) {
		struct cm1106s_abc abc = data->abc;

		if (cm1106s_send_abc(dev, &abc) == 0) {
			data->abc_pending = false;
		}
	}
	return 0;
}

static int cm1106s_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *val)
{
	struct cm1106s_data *data = dev->data;

	if (chan != SENSOR_CHAN_CO2) {
		return -ENOTSUP;
	}
	val->val1 = data->ppm;
	val->val2 = 0;
	return 0;
}

static int cm1106s_abc_set(const struct device *dev, enum sensor_attribute attr,
			   const struct sensor_value *val)
{
	struct cm1106s_data *data = dev->data;
	struct cm1106s_abc abc = data->abc;
	int ret;

	switch ((int)attr) {
	case SENSOR_ATTR_CM1106S_ABC_ENABLE:
		if (val->val1 != 0 && val->val1 != 1) {
			return -EINVAL;
		}
		abc.enable = val->val1;
		break;
	case SENSOR_ATTR_CM1106S_ABC_CYCLE_DAYS:
		if (val->val1 < ABC_MIN_DAYS || val->val1 > ABC_MAX_DAYS) {
			return -EINVAL;
		}
		abc.days = val->val1;
		break;
	case SENSOR_ATTR_CM1106S_ABC_BASE_PPM:
		/* the datasheet does not give a range, only the frame format limits it */
		if (val->val1 < 0 || val->val1 > 0xFFFF) {
			return -EINVAL;
		}
		abc.base_ppm = val->val1;
		break;
	default:
		return -ENOTSUP;
	}

	ret = cm1106s_send_abc(dev, &abc);
	if (ret == 0) {
		data->abc = abc;
		data->abc_pending = false; /* the sensor has the complete setting now */
	}
	return ret;
}

static int cm1106s_attr_set(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr, const struct sensor_value *val)
{
	if (chan != SENSOR_CHAN_CO2) {
		return -ENOTSUP;
	}
	if (attr == SENSOR_ATTR_CALIBRATION) {
		/* datasheet 4.3: target 400 to 1500 ppm; the ambient air must have been at the target
		 * value for 2 minutes before this is sent */
		if (val->val1 < CAL_MIN_PPM || val->val1 > CAL_MAX_PPM) {
			return -EINVAL;
		}

		const uint8_t tx[2] = { (uint8_t)(val->val1 >> 8), (uint8_t)val->val1 };

		return cm1106s_cmd(dev, CMD_CALIBRATE, tx, sizeof(tx), NULL, 0, CMD_TIMEOUT_MS, true);
	}
	return cm1106s_abc_set(dev, attr, val);
}

static int cm1106s_attr_get(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr, struct sensor_value *val)
{
	struct cm1106s_data *data = dev->data;

	if (chan != SENSOR_CHAN_CO2) {
		return -ENOTSUP;
	}
	val->val2 = 0;
	switch ((int)attr) {
	case SENSOR_ATTR_CM1106S_ABC_ENABLE:
		val->val1 = data->abc.enable;
		return 0;
	case SENSOR_ATTR_CM1106S_ABC_CYCLE_DAYS:
		val->val1 = data->abc.days;
		return 0;
	case SENSOR_ATTR_CM1106S_ABC_BASE_PPM:
		val->val1 = data->abc.base_ppm;
		return 0;
	default:
		return -ENOTSUP;
	}
}

int cm1106s_read_serial(const struct device *dev, uint16_t sn[5])
{
	uint8_t r[10];
	int ret = cm1106s_cmd(dev, CMD_SERIAL, NULL, 0, r, sizeof(r), CMD_TIMEOUT_MS, true);

	if (ret == 0) {
		for (int i = 0; i < 5; i++) {
			sn[i] = (r[2 * i] << 8) | r[2 * i + 1]; /* high byte first, like every 2 byte value */
		}
	}
	return ret;
}

int cm1106s_read_version(const struct device *dev, char version[CM1106S_VERSION_LEN])
{
	uint8_t r[11];
	int ret = cm1106s_cmd(dev, CMD_VERSION, NULL, 0, r, sizeof(r), CMD_TIMEOUT_MS,
			    false /* the checksum of the datasheet example does not add up */);

	if (ret == 0) {
		memcpy(version, r, CM1106S_VERSION_LEN - 1); /* r[10] is reserved */
		version[CM1106S_VERSION_LEN - 1] = '\0';
	}
	return ret;
}

static int cm1106s_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct cm1106s_config *cfg = dev->config;
	struct cm1106s_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		if (cfg->supply != NULL) {
			ret = regulator_enable(cfg->supply);
		}
		if (ret == 0) {
			data->active = true;
			data->abc = cfg->abc_dt; /* a power cycle may reset the sensor settings */
			data->abc_pending = cfg->abc_from_dt;
			cm1106s_flush(dev); /* also enables the RX interrupt */
		}
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		data->active = false;
		uart_irq_rx_disable(cfg->uart);
		if (cfg->supply != NULL) {
			ret = regulator_disable(cfg->supply);
		}
		break;
	default:
		ret = -ENOTSUP;
		break;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static int cm1106s_init(const struct device *dev)
{
	const struct cm1106s_config *cfg = dev->config;
	struct cm1106s_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->uart) || (cfg->supply != NULL && !device_is_ready(cfg->supply))) {
		return -ENODEV;
	}
	ring_buf_init(&data->rb, sizeof(data->rb_mem), data->rb_mem);
	k_sem_init(&data->rx_sem, 0, 1);
	k_mutex_init(&data->lock);

	ret = uart_irq_callback_user_data_set(cfg->uart, cm1106s_isr, (void *)dev);
	if (ret < 0) {
		return ret;
	}
	return pm_device_driver_init(dev, cm1106s_pm_action); /* starts active: RESUME */
}

static DEVICE_API(sensor, cm1106s_api) = {
	.sample_fetch = cm1106s_sample_fetch,
	.channel_get = cm1106s_channel_get,
	.attr_set = cm1106s_attr_set,
	.attr_get = cm1106s_attr_get,
};

#define CM1106S_HAS_ABC(inst)                                                                      \
	(DT_INST_NODE_HAS_PROP(inst, abc_enable) || DT_INST_NODE_HAS_PROP(inst, abc_cycle_days) || \
	 DT_INST_NODE_HAS_PROP(inst, abc_base_ppm))

#define CM1106S_INIT(inst)                                                                         \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, abc_cycle_days, ABC_DEFAULT_DAYS) >= ABC_MIN_DAYS &&    \
			     DT_INST_PROP_OR(inst, abc_cycle_days, ABC_DEFAULT_DAYS) <= ABC_MAX_DAYS,\
		     "abc-cycle-days must be 1 to 30");                                            \
	static struct cm1106s_data cm1106s_data_##inst;                                            \
	static const struct cm1106s_config cm1106s_cfg_##inst = {                                  \
		.uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                                          \
		.supply = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, vin_supply),                     \
				      (DEVICE_DT_GET(DT_INST_PHANDLE(inst, vin_supply))), (NULL)), \
		.abc_from_dt = CM1106S_HAS_ABC(inst),                                              \
		.abc_dt = {                                                                        \
			.enable = DT_INST_PROP_OR(inst, abc_enable, ABC_DEFAULT_ENABLE),           \
			.days = DT_INST_PROP_OR(inst, abc_cycle_days, ABC_DEFAULT_DAYS),           \
			.base_ppm = DT_INST_PROP_OR(inst, abc_base_ppm, ABC_DEFAULT_BASE),         \
		},                                                                                 \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, cm1106s_pm_action);                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, cm1106s_init, PM_DEVICE_DT_INST_GET(inst),              \
				     &cm1106s_data_##inst, &cm1106s_cfg_##inst, POST_KERNEL,       \
				     CONFIG_SENSOR_INIT_PRIORITY, &cm1106s_api);

DT_INST_FOREACH_STATUS_OKAY(CM1106S_INIT)
