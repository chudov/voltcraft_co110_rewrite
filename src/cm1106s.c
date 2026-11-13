/*
 * Cubic CM1106S CO2 sensor driver (UART, 9600 8N1), Zephyr sensor API.
 *
 *   request  11 01 01 ED            answer 16 05 01 D1 D2 D3 D4 CS   CO2 = D1 * 256 + D2 [ppm]
 *   zero point calibration to a reference value (400 ppm in fresh air):
 *            11 03 03 HI LO CS      answer 16 01 03 E6
 *   Every frame ends with a checksum byte: the sum of all bytes of a frame is 0 modulo 256.
 *
 * API:
 *   sensor_sample_fetch_chan(dev, SENSOR_CHAN_CO2) + sensor_channel_get(): CO2 in ppm (val1)
 *   sensor_attr_set(dev, SENSOR_CHAN_CO2, SENSOR_ATTR_CALIBRATION, val1 = reference ppm)
 * Power: PM_DEVICE_ACTION_SUSPEND / RESUME switch the optional vin-supply regulator.
 *
 * Not built or tested on the device when this was written.
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

#define RX_SIZE            64
#define MEASURE_REPLY_LEN  8
#define CALIB_REPLY_LEN    4
#define MEASURE_TIMEOUT_MS 300
#define CALIB_TIMEOUT_MS   100

struct cm1106s_config {
	const struct device *uart;
	const struct device *supply; /* NULL when the sensor is always powered */
};

struct cm1106s_data {
	struct ring_buf rb;           /* ISR is the only producer, the caller the only consumer */
	uint8_t rb_mem[RX_SIZE];
	struct k_sem rx_sem;          /* given by the ISR for every received chunk */
	struct k_mutex lock;          /* one transaction at a time */
	int32_t ppm;
	bool active;
};

static const uint8_t measure_req[4] = { 0x11, 0x01, 0x01, 0xED };
static const uint8_t measure_head[3] = { 0x16, 0x05, 0x01 };
static const uint8_t calib_head[3] = { 0x16, 0x01, 0x03 };

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

/* send a request and wait for a reply frame that starts with head[3], has rx_len bytes and a valid checksum */
static int cm1106s_xfer(const struct device *dev, const uint8_t *tx, size_t tx_len,
			const uint8_t *head, size_t rx_len, uint8_t *out, int timeout_ms)
{
	const struct cm1106s_config *cfg = dev->config;
	struct cm1106s_data *data = dev->data;
	uint8_t buf[RX_SIZE];
	size_t n = 0;
	int ret = -ETIMEDOUT;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->active) {
		k_mutex_unlock(&data->lock);
		return -EACCES; /* suspended: sensor not powered */
	}

	cm1106s_flush(dev);
	for (size_t i = 0; i < tx_len; i++) {
		uart_poll_out(cfg->uart, tx[i]);
	}

	int64_t end = k_uptime_get() + timeout_ms;

	while (1) {
		n += ring_buf_get(&data->rb, buf + n, sizeof(buf) - n);
		for (size_t i = 0; i + rx_len <= n; i++) {
			if (memcmp(&buf[i], head, 3) == 0 && cm1106s_sum(&buf[i], rx_len) == 0) {
				memcpy(out, &buf[i], rx_len);
				ret = 0;
				goto done;
			}
		}
		int64_t left = end - k_uptime_get();

		if (n >= sizeof(buf) || left <= 0) {
			ret = (n > 0) ? -EBADMSG : -ETIMEDOUT; /* bytes seen, but no valid frame */
			goto done;
		}
		(void)k_sem_take(&data->rx_sem, K_MSEC(left));
	}
done:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int cm1106s_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct cm1106s_data *data = dev->data;
	uint8_t r[MEASURE_REPLY_LEN];
	int ret;

	if (chan != SENSOR_CHAN_CO2 && chan != SENSOR_CHAN_ALL) {
		return -ENOTSUP;
	}
	ret = cm1106s_xfer(dev, measure_req, sizeof(measure_req), measure_head, sizeof(r), r,
			   MEASURE_TIMEOUT_MS);
	if (ret == 0) {
		data->ppm = (r[3] << 8) | r[4];
	}
	return ret;
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

static int cm1106s_attr_set(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr, const struct sensor_value *val)
{
	uint8_t r[CALIB_REPLY_LEN];

	if (chan != SENSOR_CHAN_CO2 || attr != SENSOR_ATTR_CALIBRATION) {
		return -ENOTSUP;
	}
	/* the allowed range of the sensor has not been checked, only the frame format limits it */
	if (val->val1 < 0 || val->val1 > 0xFFFF) {
		return -EINVAL;
	}

	uint8_t f[6] = { 0x11, 0x03, 0x03, (uint8_t)(val->val1 >> 8), (uint8_t)val->val1, 0 };

	f[5] = (uint8_t)(0x100 - cm1106s_sum(f, 5));
	return cm1106s_xfer(dev, f, sizeof(f), calib_head, sizeof(r), r, CALIB_TIMEOUT_MS);
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
};

#define CM1106S_INIT(inst)                                                                         \
	static struct cm1106s_data cm1106s_data_##inst;                                            \
	static const struct cm1106s_config cm1106s_cfg_##inst = {                                  \
		.uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                                          \
		.supply = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, vin_supply),                     \
				      (DEVICE_DT_GET(DT_INST_PHANDLE(inst, vin_supply))), (NULL)), \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, cm1106s_pm_action);                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, cm1106s_init, PM_DEVICE_DT_INST_GET(inst),              \
				     &cm1106s_data_##inst, &cm1106s_cfg_##inst, POST_KERNEL,       \
				     CONFIG_SENSOR_INIT_PRIORITY, &cm1106s_api);

DT_INST_FOREACH_STATUS_OKAY(CM1106S_INIT)
