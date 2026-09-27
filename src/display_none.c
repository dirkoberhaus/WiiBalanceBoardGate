// Stub backend for boards with no display at all (e.g. a bare Mini D1
// ESP32) - compiled in whenever neither DISPLAY_SSD1306 nor DISPLAY_ST7789
// is defined, so display_init() et al. still link. display_init() reporting
// failure is exactly what main.c already treats as "no display" and handles
// gracefully.
#if !defined(DISPLAY_SSD1306) && !defined(DISPLAY_ST7789)

#include "display.h"

esp_err_t display_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void display_clear(void) {}
int16_t display_width(void) { return 0; }
int16_t display_height(void) { return 0; }
void display_draw_text(uint8_t col, uint8_t row, const char *text) { (void)col; (void)row; (void)text; }
void display_flush(void) {}
bool display_has_graphics(void) { return false; }
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) { (void)x; (void)y; (void)w; (void)h; (void)on; }
void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) { (void)x; (void)y; (void)w; (void)h; (void)on; }
void display_fill_circle(int16_t cx, int16_t cy, int16_t r, bool on) { (void)cx; (void)cy; (void)r; (void)on; }

#endif // !DISPLAY_SSD1306 && !DISPLAY_ST7789
