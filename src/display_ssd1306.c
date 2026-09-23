#ifdef DISPLAY_SSD1306

#include "display.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "font5x7.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display_ssd1306";

// Heltec WiFi LoRa32 V2 pinout. Change these if your board differs.
#define PIN_SDA GPIO_NUM_4
#define PIN_SCL GPIO_NUM_15
#define PIN_RST GPIO_NUM_16
#define PIN_VEXT GPIO_NUM_21 // powers the OLED + other peripherals, active low

#define I2C_ADDR 0x3C
#define WIDTH 128
#define HEIGHT 64
#define PAGES (HEIGHT / 8)

#define COLS (WIDTH / 6)  // 5px glyph + 1px spacing
#define ROWS PAGES        // one text row per page, glyphs are <= 7px tall

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_fb[WIDTH * PAGES];

static esp_err_t write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

esp_err_t display_init(void)
{
    gpio_config_t vext_cfg = {
        .pin_bit_mask = 1ULL << PIN_VEXT,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&vext_cfg);
    gpio_set_level(PIN_VEXT, 0); // power on the OLED

    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    static const uint8_t init_cmds[] = {
        0xAE,       // display off
        0xD5, 0x80, // clock divide
        0xA8, 0x3F, // multiplex ratio = 64
        0xD3, 0x00, // display offset = 0
        0x40,       // start line = 0
        0x8D, 0x14, // charge pump on
        0x20, 0x00, // horizontal addressing mode
        0xA1,       // segment remap
        0xC8,       // COM scan direction
        0xDA, 0x12, // COM pins config
        0x81, 0xCF, // contrast
        0xD9, 0xF1, // precharge
        0xDB, 0x40, // VCOMH deselect level
        0xA4,       // resume RAM content display
        0xA6,       // normal (not inverted)
        0xAF,       // display on
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        err = write_cmd(init_cmds[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "init cmd 0x%02x failed: %s", init_cmds[i], esp_err_to_name(err));
            return err;
        }
    }

    display_clear();
    display_flush();
    ESP_LOGI(TAG, "Display initialized");
    return ESP_OK;
}

void display_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void display_draw_text(uint8_t col, uint8_t row, const char *text)
{
    if (row >= ROWS) {
        return;
    }
    while (*text && col < COLS) {
        const unsigned char *glyph = font5x7_glyph(*text);
        uint8_t *cell = &s_fb[row * WIDTH + col * 6];
        if (glyph) {
            memcpy(cell, glyph, 5);
        } else {
            memset(cell, 0, 5);
        }
        cell[5] = 0x00; // inter-character spacing column
        col++;
        text++;
    }
}

int16_t display_width(void) { return WIDTH; }
int16_t display_height(void) { return HEIGHT; }

bool display_has_graphics(void) { return false; }
// No room for a graphics panel next to the status text on this tiny screen -
// these exist only to satisfy display.h; display_has_graphics() means
// callers should never actually invoke them.
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) { (void)x; (void)y; (void)w; (void)h; (void)on; }
void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) { (void)x; (void)y; (void)w; (void)h; (void)on; }
void display_fill_circle(int16_t cx, int16_t cy, int16_t r, bool on) { (void)cx; (void)cy; (void)r; (void)on; }

void display_flush(void)
{
    write_cmd(0x21); // set column address range
    write_cmd(0x00);
    write_cmd(WIDTH - 1);
    write_cmd(0x22); // set page address range
    write_cmd(0x00);
    write_cmd(PAGES - 1);

    uint8_t buf[WIDTH + 1];
    buf[0] = 0x40; // data (not command) stream
    for (int page = 0; page < PAGES; page++) {
        memcpy(buf + 1, &s_fb[page * WIDTH], WIDTH);
        esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "flush page %d failed: %s", page, esp_err_to_name(err));
        }
    }
}

#endif // DISPLAY_SSD1306
