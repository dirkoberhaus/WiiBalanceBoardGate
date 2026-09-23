#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Shared status-display interface, implemented once per supported board:
 *  - display_ssd1306.c  (Heltec WiFi LoRa32 V2's 128x64 OLED)
 *  - display_st7789.c   (TTGO T-Display's 240x135 color TFT)
 * Selected at build time via the DISPLAY_SSD1306 / DISPLAY_ST7789 flag set
 * per PlatformIO environment in platformio.ini.
 */

esp_err_t display_init(void);
void display_clear(void);

// Physical panel size in pixels.
int16_t display_width(void);
int16_t display_height(void);

// Text-cell grid, cell size varies per backend (see display_width/height to
// compute how many columns/rows actually fit). Both backends fit at least
// the ~16 columns x 8 rows the status screens in main.c use.
void display_draw_text(uint8_t col, uint8_t row, const char *text);

// Pushes what display_draw_text() built up to the panel.
void display_flush(void);

// True if display_fill_rect/draw_rect/fill_circle are real (not no-ops) -
// only the color TFT backend bothers, since the tiny OLED has no room for
// a graphical panel alongside its status text.
bool display_has_graphics(void);
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on);
void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on);
void display_fill_circle(int16_t cx, int16_t cy, int16_t r, bool on);
