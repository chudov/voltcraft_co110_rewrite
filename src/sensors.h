#ifndef SENSORS_H_
#define SENSORS_H_

#include <stdbool.h>

/* SHT30 on I2C2 (address 0x44): single shot, high repeatability, CRC checked.
 * temp_tenths in 0.1 degC, rh in %. Returns 0 or a negative errno. */
int sht30_measure(int *temp_tenths, int *rh_percent);
void sht30_recover_bus(void);

#endif
