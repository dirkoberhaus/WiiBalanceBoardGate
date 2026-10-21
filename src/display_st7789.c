#ifdef DISPLAY_ST7789

#include "display.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "font5x7.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display_st7789";

// TTGO T-Display pinout (matches LilyGO's own examples / TFT_eSPI's
// Setup25_TTGO_T_Display.h). Change these if your board differs.
#define PIN_MOSI GPIO_NUM_19
#define PIN_SCLK GPIO_NUM_18
#define PIN_CS   GPIO_NUM_5
#define PIN_DC   GPIO_NUM_16
#define PIN_RST  GPIO_NUM_23
#define PIN_BL   GPIO_NUM_4 // backlight, active high

// Landscape: 240x135 visible area out of a 240x320 controller RAM - the
// panel is wired with an offset into that RAM which needs compensating.
// NOTE: unverified against real hardware. If the image is offset/cropped,
// try swapping these to (52, 40); if it's mirrored/upside down, try MADCTL
// 0xA0 instead of 0x60 below.
#define SCREEN_W 240
#define SCREEN_H 135
#define COL_OFFSET 40
#define ROW_OFFSET 53
#define MADCTL_VALUE 0x60

#define COLOR_BG 0x0000 // black
#define COLOR_FG 0xFFFF // white

// Text drawn at 2x the OLED's native 6x8 cell - this screen has the room for
// noticeably bigger, more legible status text.
#define CELL_W 12
#define CELL_H 16
#define COLS (SCREEN_W / CELL_W)
#define ROWS (SCREEN_H / CELL_H)

// Max width display_fill_rect() accepts in one call - must cover a full-width
// fill (SCREEN_W); kept fixed to avoid a VLA. A silent truncation here once
// cut off part of a border - if you raise SCREEN_W, raise this too.
#define MAX_RECT_DIM SCREEN_W

static spi_device_handle_t s_spi = NULL;

static void write_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_DC, 0);
    spi_transaction_t t = {.length = 8, .tx_data = {cmd}, .flags = SPI_TRANS_USE_TXDATA};
    spi_device_polling_transmit(s_spi, &t);
}

static void write_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }
    gpio_set_level(PIN_DC, 1);
    spi_transaction_t t = {.length = len * 8, .tx_buffer = data};
    spi_device_polling_transmit(s_spi, &t);
}

static void write_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    write_cmd(cmd);
    write_data(data, len);
}

static void set_window(int x0, int y0, int x1, int y1)
{
    x0 += COL_OFFSET; x1 += COL_OFFSET;
    y0 += ROW_OFFSET; y1 += ROW_OFFSET;
    uint8_t caset[4] = {(x0 >> 8) & 0xFF, x0 & 0xFF, (x1 >> 8) & 0xFF, x1 & 0xFF};
    write_cmd_data(0x2A, caset, sizeof(caset));
    uint8_t raset[4] = {(y0 >> 8) & 0xFF, y0 & 0xFF, (y1 >> 8) & 0xFF, y1 & 0xFF};
    write_cmd_data(0x2B, raset, sizeof(raset));
    write_cmd(0x2C); // RAMWR
}

esp_err_t display_init(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST) | (1ULL << PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_cfg);
    gpio_set_level(PIN_BL, 1); // backlight on

    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 26 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    write_cmd(0x01); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));
    write_cmd(0x11); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));
    write_cmd_data(0x3A, (const uint8_t[]){0x55}, 1); // COLMOD: 16bpp RGB565
    write_cmd_data(0x36, (const uint8_t[]){MADCTL_VALUE}, 1);
    write_cmd(0x21); // INVON - this panel needs inversion on for correct colors
    write_cmd(0x13); // NORON
    vTaskDelay(pdMS_TO_TICKS(10));
    write_cmd(0x29); // DISPON
    vTaskDelay(pdMS_TO_TICKS(100));

    display_clear();
    ESP_LOGI(TAG, "Display initialized");
    return ESP_OK;
}

void display_clear(void)
{
    set_window(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    uint8_t row_buf[SCREEN_W * 2];
    memset(row_buf, COLOR_BG & 0xFF, sizeof(row_buf)); // COLOR_BG is 0x0000, so this is exact
    gpio_set_level(PIN_DC, 1);
    for (int y = 0; y < SCREEN_H; y++) {
        spi_transaction_t t = {.length = sizeof(row_buf) * 8, .tx_buffer = row_buf};
        spi_device_polling_transmit(s_spi, &t);
    }
}

void display_draw_text(uint8_t col, uint8_t row, const char *text)
{
    if (row >= ROWS) {
        return;
    }
    // Clears all the way to COLS, even past the end of `text` - callers
    // always draw a full row at a time (see display.h), and a shorter
    // string must blank out whatever a previous, longer one left behind at
    // the same row rather than leave stray trailing glyphs on screen.
    while (col < COLS) {
        const unsigned char *glyph = *text ? font5x7_glyph(*text) : NULL;
        int x0 = col * CELL_W, y0 = row * CELL_H;

        uint8_t buf[CELL_W * CELL_H * 2];
        for (int yy = 0; yy < CELL_H; yy++) {
            int fy = yy / 2; // each font pixel becomes a 2x2 block
            for (int xx = 0; xx < CELL_W; xx++) {
                int fx = xx / 2;
                bool on = glyph && fx < 5 && fy < 7 && ((glyph[fx] >> fy) & 1);
                uint16_t color = on ? COLOR_FG : COLOR_BG;
                int i = (yy * CELL_W + xx) * 2;
                buf[i] = (color >> 8) & 0xFF;
                buf[i + 1] = color & 0xFF;
            }
        }

        set_window(x0, y0, x0 + CELL_W - 1, y0 + CELL_H - 1);
        write_data(buf, sizeof(buf));

        col++;
        if (*text) {
            text++;
        }
    }
}

void display_flush(void)
{
    // display_draw_text() writes directly to the panel - nothing to flush.
}

int16_t display_width(void) { return SCREEN_W; }
int16_t display_height(void) { return SCREEN_H; }
bool display_has_graphics(void) { return true; }

void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (w > MAX_RECT_DIM) {
        ESP_LOGW(TAG, "fill_rect width %d clamped to %d", w, MAX_RECT_DIM);
        w = MAX_RECT_DIM;
    }
    set_window(x, y, x + w - 1, y + h - 1);
    uint16_t color = on ? COLOR_FG : COLOR_BG;
    uint8_t row_buf[MAX_RECT_DIM * 2];
    for (int i = 0; i < w; i++) {
        row_buf[i * 2] = (color >> 8) & 0xFF;
        row_buf[i * 2 + 1] = color & 0xFF;
    }
    gpio_set_level(PIN_DC, 1);
    for (int yy = 0; yy < h; yy++) {
        spi_transaction_t t = {.length = (size_t)w * 2 * 8, .tx_buffer = row_buf};
        spi_device_polling_transmit(s_spi, &t);
    }
}

void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on)
{
    display_fill_rect(x, y, w, 1, on);
    display_fill_rect(x, y + h - 1, w, 1, on);
    display_fill_rect(x, y, 1, h, on);
    display_fill_rect(x + w - 1, y, 1, h, on);
}

void display_fill_circle(int16_t cx, int16_t cy, int16_t r, bool on)
{
    for (int16_t yy = -r; yy <= r; yy++) {
        int16_t dx = (int16_t)sqrtf((float)(r * r - yy * yy));
        display_fill_rect(cx - dx, cy + yy, 2 * dx + 1, 1, on);
    }
}

#endif // DISPLAY_ST7789
