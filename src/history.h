#ifndef HISTORY_H_
#define HISTORY_H_

/*
 * Chart history, 17 columns per level (newest first) in co110_ud.hist[level][quantity][column]:
 * level 0 = 5 s per column (RAM only), 1 = 5 min, 2 = 2 h, 3 = 12 h (stored in flash).
 */
void history_load(void);
void history_push(int co2, int rh, int tenths); /* call every 5 s with a valid reading */
void history_clear(void);

#endif
