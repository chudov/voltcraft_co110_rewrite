#include "chart_level.h"

int co110_chart_level(int vmin, int vmax, int value)
{
	const int range = vmax - vmin;
	const int d = (value - vmin) * CHART_STEPS; /* the value is at d / range steps above vmin */

	if (value < vmin) {
		return 0;
	}
	if (d < range) {
		return 1;
	}
	if (vmax <= value) {
		return 8;
	}
	for (int k = 1; k < 8; k++) {
		if (d >= k * range && d <= (k + 1) * range) {
			return k + 1;
		}
	}
	return 8;
}
