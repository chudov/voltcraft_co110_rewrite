#ifndef LEDS_H_
#define LEDS_H_

#include <stdbool.h>

/* bit 0 = LD4 green, bit 1 = LD3 yellow, bit 2 = LD2 red, bit 3 = LD1 green (power) */
#define LED_LD4 0x1
#define LED_LD3 0x2
#define LED_LD2 0x4
#define LED_LD1 0x8
#define LED_ALL 0xF

void leds_init(void);
void leds_set(unsigned int mask);   /* logical on/off, dimmed when dark mode is active */
void leds_set_dark(bool dark);
bool leds_is_dark(void);

#endif
