#ifndef CHART_LEVEL_H_
#define CHART_LEVEL_H_

/*
 * Bar height 0..8 of a value inside a chart whose scale runs from vmin to vmax, as the original
 * firmware computes it (FUN_08000d20): the range is split into 8 equal steps,
 *   value < vmin              -> 0
 *   value < vmin + 1 step     -> 1
 *   vmax <= value             -> 8      (also a chart of one value or of equal values)
 *   otherwise the step the value falls into (2..8)
 * The same function draws the dot columns (the value of the column) and the eight row arrows
 * (the value 1000, the 1000 ppm mark of the CO2 scale).
 */
int co110_chart_level(int vmin, int vmax, int value);

/*
 * The original divides the range (max - min) by a double constant stored at flash address
 * 0x08000E38 (FUN_0800049c is a double division, called from FUN_08000d20) to get the step.
 * Read from the flash image: 8.0 (bytes 00 00 00 00 00 00 20 40), so 8 equal steps.
 */
#define CHART_STEPS 8

#endif
