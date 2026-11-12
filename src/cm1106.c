#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include "sensors.h"

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

/* received bytes: the ISR is the only producer, the sensor thread the only consumer */
#define RX_SIZE 64
RING_BUF_DECLARE(cm1106_rx_rb, RX_SIZE);

/* drop everything received so far */
static void rx_flush(void)
{
	uint8_t tmp[RX_SIZE];

	while (ring_buf_get(&cm1106_rx_rb, tmp, sizeof(tmp)) > 0) {
	}
}

static void uart_isr(const struct device *dev, void *user)
{
	ARG_UNUSED(user);

	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev)) {
		uint8_t c[16];
		int n = uart_fifo_read(dev, c, sizeof(c));

		if (n <= 0) {
			break;
		}
		ring_buf_put(&cm1106_rx_rb, c, n); /* a full buffer drops the new bytes */
	}
}

int cm1106_init(void)
{
	if (!device_is_ready(uart)) {
		return -ENODEV;
	}
	uart_irq_callback_user_data_set(uart, uart_isr, NULL);
	uart_irq_rx_enable(uart);
	return 0;
}

void cm1106_request(void)
{
	static const uint8_t req[4] = { 0x11, 0x01, 0x01, 0xED };

	rx_flush(); /* drop old bytes */
	for (int i = 0; i < 4; i++) {
		uart_poll_out(uart, req[i]);
	}
}

bool cm1106_get_result(int *ppm)
{
	uint8_t f[RX_SIZE];
	int n = ring_buf_get(&cm1106_rx_rb, f, sizeof(f));

	for (int i = 0; i + 8 <= n; i++) {
		if (f[i] != 0x16 || f[i + 1] != 0x05 || f[i + 2] != 0x01) {
			continue;
		}
		uint8_t sum = 0;

		for (int k = 0; k < 7; k++) {
			sum += f[i + k];
		}
		if ((uint8_t)(0x100 - sum) == f[i + 7]) {
			*ppm = (f[i + 3] << 8) | f[i + 4];
			return true;
		}
	}
	return false;
}

bool cm1106_calibrate(int ppm)
{
	uint8_t f[6] = { 0x11, 0x03, 0x03, (uint8_t)(ppm >> 8), (uint8_t)ppm, 0 };
	uint8_t sum = 0;

	for (int i = 0; i < 5; i++) {
		sum += f[i];
	}
	f[5] = (uint8_t)(0x100 - sum);

	for (int attempt = 0; attempt < 10; attempt++) {
		rx_flush();
		for (int i = 0; i < 6; i++) {
			uart_poll_out(uart, f[i]);
		}
		k_msleep(50);

		uint8_t r[RX_SIZE];
		int n = ring_buf_get(&cm1106_rx_rb, r, sizeof(r));

		for (int i = 0; i + 4 <= n; i++) {
			if (r[i] == 0x16 && r[i + 1] == 0x01 && r[i + 2] == 0x03 && r[i + 3] == 0xE6) {
				return true;
			}
		}
	}
	return false;
}
