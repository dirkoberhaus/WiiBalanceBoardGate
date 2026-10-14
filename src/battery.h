#pragma once

#include <stdbool.h>

// Battery-sense support, where the board has it (Heltec/TTGO - both can run
// off a LiPo). Mini D1 has no battery circuitry at all; battery_init()
// returns false there and battery_read_percent() always returns -1.

// Call once at startup, after display_init(). Returns false if this board
// has no battery-sense hardware.
bool battery_init(void);

// Returns an estimated 0-100 charge level, or -1 if unsupported/unavailable.
// This is a rough linear mapping of the sensed voltage, not a real discharge
// curve - treat it as "how worried should I be", not a precise reading.
int battery_read_percent(void);
