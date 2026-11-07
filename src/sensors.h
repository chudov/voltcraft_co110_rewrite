#ifndef SENSORS_H_
#define SENSORS_H_

#include <stdbool.h>

/* SHT30 on I2C2 (address 0x44): single shot, high repeatability, CRC checked.
 * temp_tenths in 0.1 degC, rh in %. Returns 0 or a negative errno. */
int sht30_measure(int *temp_tenths, int *rh_percent);
void sht30_recover_bus(void);

/* CM1106S on USART1, 9600 8N1: request frame 11 01 01 ED, answer 16 05 01 D1 D2 D3 D4 CS */
int cm1106_init(void);
void cm1106_request(void);
/* parse what has been received since the request; true and *ppm when a valid frame was found */
bool cm1106_get_result(int *ppm);

#endif
