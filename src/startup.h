#ifndef STARTUP_H_
#define STARTUP_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Start-up sequence of the original firmware (state machine, Zephyr SMF), about 37 s:
 *   test     2 s   all LCD segments and all LEDs on
 *   ld1      1 s   LD1
 *   lo1/lo2  1 s each  LD2 / LD3, "Lo" and the low CO2 threshold
 *   hi1/hi2  1 s each  LD4 / all LEDs, "Hi" and the high CO2 threshold
 *   count   30 s   LEDs off, countdown in the CO2 digits (the sensors already run)
 */
void startup_init(int64_t now);
void startup_tick(int64_t now);   /* runs the state machine */
bool startup_active(void);
void startup_render(int preset);  /* draws into the frame buffer, nothing is sent */

#endif
