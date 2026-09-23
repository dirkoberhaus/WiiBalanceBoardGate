#pragma once

#include <stdint.h>

#include "esp_err.h"

/*
 * Minimal driver for the 128x64 SSD1306 OLED on the Heltec WiFi LoRa32 V2
 * board (I2C: SDA=GPIO4, SCL=GPIO15, RST=GPIO16, address 0x3C; display power
 * gated by Vext on GPIO21, active low). Adjust ssd1306.c's pin defines if
 * you're on different hardware.
 */

esp_err_t ssd1306_init(void);
void ssd1306_clear(void);

// Screen is 21 character columns x 8 rows (6x8 px cells for a 5x7 font).
// Text outside a supported glyph (see ssd1306.c) is skipped, not garbled.
void ssd1306_draw_text(uint8_t col, uint8_t row, const char *text);

// Pushes the framebuffer built up by ssd1306_draw_text() to the panel.
void ssd1306_flush(void);
