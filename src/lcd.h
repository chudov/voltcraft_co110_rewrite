#ifndef LCD_H_
#define LCD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LCD_FB_SIZE 39 /* bytes, sent as one burst at address 0 */

extern uint8_t lcd_fb[LCD_FB_SIZE];

int lcd_init(void);
void lcd_command(uint8_t cmd);
void lcd_write(uint8_t addr, const uint8_t *buf, size_t len);
void lcd_flush(void);
void lcd_fill(uint8_t value);
void lcd_set(uint8_t byte, uint8_t bit, bool on);

#endif
