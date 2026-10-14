// Heltec WiFi LoRa32 V2 battery sense - disabled.
//
// Tried both pins Heltec has documented for this board's battery divider
// across hardware revisions: GPIO37 (ADC1_CH1, the V2.1+ pin) and GPIO13
// (ADC2_CH4, the original V2 pin). On this specific unit neither gives a
// plausible reading - GPIO37 reads near-zero (nothing connected there),
// GPIO13 reads implausibly high and stays pinned at 100% even when running
// on battery alone (ruling out the USB-charging-voltage explanation that
// accounts for battery_ttgo.c's smaller overshoot). Likely either this
// board's divider isn't populated, or it uses a pin Heltec hasn't
// documented - Heltec's hardware revisions within the "V2" label aren't
// consistently documented. Confirming either way needs a multimeter at
// GPIO13/GPIO37 against the battery pins, not more software guessing.
#ifdef DISPLAY_SSD1306

#include "battery.h"

bool battery_init(void) { return false; }
int battery_read_percent(void) { return -1; }

#endif // DISPLAY_SSD1306
