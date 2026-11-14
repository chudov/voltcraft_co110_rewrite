#ifndef HISTORY_H_
#define HISTORY_H_

/*
 * Chart history, 17 columns per level (newest first) in co110_ud.hist[level][quantity][column]:
 * level 0 = 5 s per column (RAM only), 1 = 5 min, 2 = 2 h, 3 = 12 h (stored in flash).
 *
 * Flash: one small record per step and level, written into a ring of 17 slots per level
 * (items "co110/hist/<level>_<slot>", 8 bytes: sequence number, CO2, RH, temperature).
 * A step writes 16 bytes of flash (8 byte record + 8 byte NVS entry); the old scheme rewrote a
 * 106 byte blob. The newest slot is found by the sequence number, so no pointer item is needed.
 */
void history_load(void);
void history_push(int co2, int rh, int tenths); /* call every 5 s with a valid reading */
void history_clear(void);

#endif
