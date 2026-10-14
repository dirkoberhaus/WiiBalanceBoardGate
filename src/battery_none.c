// Stub for boards with no battery-sense circuitry (e.g. a bare Mini D1
// ESP32) - compiled in whenever neither DISPLAY_SSD1306 nor DISPLAY_ST7789
// is defined, mirroring display_none.c.
#if !defined(DISPLAY_SSD1306) && !defined(DISPLAY_ST7789)

#include "battery.h"

bool battery_init(void) { return false; }
int battery_read_percent(void) { return -1; }

#endif // !DISPLAY_SSD1306 && !DISPLAY_ST7789
