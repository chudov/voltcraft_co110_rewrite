#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include "sensors.h"

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

#define RX_SIZE 64
static volatile uint8_t rx_buf[RX_SIZE];
static volatile unsigned int rx_head; /* written by the ISR */
static unsigned int rx_tail;          /* used by the thread */

static void uart_isr(const struct device *dev, void *user)
{
	ARG_UNUSED(user);

	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev)) {
		uint8_t c;

		if (uart_fifo_read(dev, &c, 1) != 1) {
			break;
		}
		rx_buf[rx_head % RX_SIZE] = c;
		rx_head++;
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

	rx_tail = rx_head; /* drop old bytes */
	for (int i = 0; i < 4; i++) {
		uart_poll_out(uart, req[i]);
	}
}

bool cm1106_get_result(int *ppm)
{
	uint8_t f[RX_SIZE];
	int n = 0;

	while (rx_tail != rx_head && n < RX_SIZE) {
		f[n++] = rx_buf[rx_tail % RX_SIZE];
		rx_tail++;
	}
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
