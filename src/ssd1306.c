#include "ssd1306.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ssd1306";

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

// 5x7 font, columns are bit0=top pixel..bit6=bottom pixel. Covers just what
// the status screen needs: space, '-', '.', ':', digits, uppercase letters.
typedef struct {
    char c;
    uint8_t col[5];
} glyph_t;

static const glyph_t kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};
#define FONT_LEN (sizeof(kFont) / sizeof(kFont[0]))

static const uint8_t *glyph_for(char c)
{
    for (size_t i = 0; i < FONT_LEN; i++) {
        if (kFont[i].c == c) {
            return kFont[i].col;
        }
    }
    return NULL; // unsupported char -> caller leaves the cell blank
}

static esp_err_t write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

esp_err_t ssd1306_init(void)
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

    ssd1306_clear();
    ssd1306_flush();
    ESP_LOGI(TAG, "Display initialized");
    return ESP_OK;
}

void ssd1306_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void ssd1306_draw_text(uint8_t col, uint8_t row, const char *text)
{
    if (row >= ROWS) {
        return;
    }
    while (*text && col < COLS) {
        const uint8_t *glyph = glyph_for(*text);
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

void ssd1306_flush(void)
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
