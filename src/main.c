#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "display.h"
#include "dns_server.h"
#include "wifi_ap.h"
#include "ws_server.h"
#include "wiimote_bt.h"

static const char *TAG = "main";

// Change these before flashing, or move to Kconfig/NVS later.
#define WIFI_AP_SSID "WiiBalanceBoardGate"
#define WIFI_AP_PASSWORD "balanceboard"

// Heltec WiFi LoRa32 V2's PRG button, wired active-low with an external pull-up.
// Doubles as a tare ("zero out the current load") button for the balance board.
#define TARE_BUTTON_GPIO GPIO_NUM_0

// Redraw the live weight rows at most this often - they arrive at ~100Hz,
// far faster than the OLED (and I2C bus) need to be driven.
#define SAMPLE_REDRAW_INTERVAL_US (200 * 1000)
#define TARE_MSG_DURATION_US (2 * 1000 * 1000)

static wiimote_bt_state_t s_bt_state = WIIMOTE_BT_SCANNING;
static int64_t s_last_redraw_us = 0;
static int64_t s_tare_msg_until_us = 0;

// The display backends (SPI/I2C) aren't reentrant, but redraw_header(),
// on_sample() and tare_button_task() run on three different tasks (esp_timer,
// the Bluetooth event task, and this task, respectively) and all touch the
// display. Without this, two of them interleaving mid-draw could corrupt the
// panel's addressing state and hang it - this is exactly what caused the
// TTGO T-Display to freeze after pressing the tare button while samples were
// still coming in.
static SemaphoreHandle_t s_display_mutex;

// Graphical center-of-pressure panel - only does anything when
// display_has_graphics() (i.e. the color TFT). Stacked *below* the status
// text rows (not beside them) so its width never has to guess how long the
// text lines next to it might get - text is 3 rows at the top (WIFI/BT/tare,
// each display's cell height), the panel fills the middle, and one more text
// row (TOTAL) sits at the very bottom.
#define COP_PANEL_MARGIN 6
// T-Display's text rows are 16px tall: rows 0-2 (WIFI/BT/tare) end at 48px,
// and row 7 (TOTAL) starts at 112px - these line up exactly with that grid
// so the panel can't bleed into either.
#define COP_PANEL_TOP_PX 48
#define COP_PANEL_BOTTOM_PX 23
static int16_t s_cop_x, s_cop_y, s_cop_w, s_cop_h;

static void init_cop_panel(void)
{
    if (!display_has_graphics()) {
        return;
    }
    s_cop_x = 4;
    s_cop_y = COP_PANEL_TOP_PX;
    s_cop_w = display_width() - 8;
    s_cop_h = display_height() - COP_PANEL_TOP_PX - COP_PANEL_BOTTOM_PX;
    display_draw_rect(s_cop_x, s_cop_y, s_cop_w, s_cop_h, true);
}

static void redraw_cop_panel(const wii_bb_sample_t *s)
{
    if (!display_has_graphics()) {
        return;
    }
    display_fill_rect(s_cop_x + 1, s_cop_y + 1, s_cop_w - 2, s_cop_h - 2, false);

    float total = s->top_left_kg + s->top_right_kg + s->bottom_left_kg + s->bottom_right_kg;
    if (!s->kg_valid || total < 1) {
        return;
    }
    float cx = ((s->top_right_kg + s->bottom_right_kg) - (s->top_left_kg + s->bottom_left_kg)) / total;
    float cy = ((s->top_left_kg + s->top_right_kg) - (s->bottom_left_kg + s->bottom_right_kg)) / total;
    if (cx < -1) cx = -1;
    if (cx > 1) cx = 1;
    if (cy < -1) cy = -1;
    if (cy > 1) cy = 1;

    int16_t half_w = s_cop_w / 2 - COP_PANEL_MARGIN;
    int16_t half_h = s_cop_h / 2 - COP_PANEL_MARGIN;
    int16_t px = s_cop_x + s_cop_w / 2 + (int16_t)(cx * half_w);
    int16_t py = s_cop_y + s_cop_h / 2 - (int16_t)(cy * half_h);
    display_fill_circle(px, py, 5, true);
}

static const char *bt_state_text(wiimote_bt_state_t state)
{
    switch (state) {
    case WIIMOTE_BT_SCANNING:   return "BT: SCANNING";
    case WIIMOTE_BT_CONNECTING: return "BT: CONNECTING";
    case WIIMOTE_BT_HANDSHAKE:  return "BT: HANDSHAKE";
    case WIIMOTE_BT_STREAMING:  return "BT: STREAMING";
    default:                    return "BT: -";
    }
}

static void redraw_header(void)
{
    char line[22];
    snprintf(line, sizeof(line), "WIFI AP  C:%d", ws_server_client_count());
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    display_draw_text(0, 0, line);
    display_draw_text(0, 1, bt_state_text(s_bt_state));
    display_flush();
    xSemaphoreGive(s_display_mutex);
}

static void header_timer_cb(void *arg)
{
    (void)arg;
    redraw_header();
}

static void on_bt_state(wiimote_bt_state_t state)
{
    s_bt_state = state;
    redraw_header();
}

static void on_sample(const wii_bb_sample_t *s)
{
    char json[192];
    int n = snprintf(json, sizeof(json),
                      "{\"seq\":%lu,\"esp_us\":%lld,"
                      "\"tr_raw\":%ld,\"br_raw\":%ld,\"tl_raw\":%ld,\"bl_raw\":%ld,"
                      "\"kg_valid\":%s,\"tr_kg\":%.3f,\"br_kg\":%.3f,\"tl_kg\":%.3f,\"bl_kg\":%.3f}",
                      (unsigned long)s->seq, (long long)esp_timer_get_time(),
                      (long)s->top_right_raw, (long)s->bottom_right_raw,
                      (long)s->top_left_raw, (long)s->bottom_left_raw,
                      s->kg_valid ? "true" : "false",
                      s->top_right_kg, s->bottom_right_kg, s->top_left_kg, s->bottom_left_kg);
    if (n > 0) {
        ws_server_broadcast(json, (size_t)n);
    }

    int64_t now = esp_timer_get_time();
    if (now - s_last_redraw_us < SAMPLE_REDRAW_INTERVAL_US) {
        return;
    }
    s_last_redraw_us = now;

    bool gfx = display_has_graphics();
    uint8_t total_row = gfx ? 7 : 6; // below the graphics panel vs. below the corner readout

    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    char line[22];
    if (s->kg_valid) {
        if (!gfx) {
            // The graphics panel already shows this visually - no room (or
            // need) for the per-corner numbers next to it on that screen.
            snprintf(line, sizeof(line), "TL%5.1f TR%5.1f", s->top_left_kg, s->top_right_kg);
            display_draw_text(0, 3, line);
            snprintf(line, sizeof(line), "BL%5.1f BR%5.1f", s->bottom_left_kg, s->bottom_right_kg);
            display_draw_text(0, 4, line);
        }
        float total = s->top_left_kg + s->top_right_kg + s->bottom_left_kg + s->bottom_right_kg;
        snprintf(line, sizeof(line), "TOTAL: %6.1f KG", total);
        display_draw_text(0, total_row, line);
    } else {
        display_draw_text(0, gfx ? total_row : 3, "UNCALIBRATED");
    }

    if (s_tare_msg_until_us && now >= s_tare_msg_until_us) {
        display_draw_text(0, 2, "                    "); // blank the row
        s_tare_msg_until_us = 0;
    }
    redraw_cop_panel(s);
    display_flush();
    xSemaphoreGive(s_display_mutex);
}

static void tare_button_task(void *arg)
{
    (void)arg;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << TARE_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);

    bool was_pressed = false;
    while (1) {
        bool pressed = gpio_get_level(TARE_BUTTON_GPIO) == 0;
        if (pressed && !was_pressed) {
            vTaskDelay(pdMS_TO_TICKS(30)); // debounce
            if (gpio_get_level(TARE_BUTTON_GPIO) == 0) {
                ESP_LOGI(TAG, "Tare button pressed");
                wiimote_bt_tare();
                xSemaphoreTake(s_display_mutex, portMAX_DELAY);
                display_draw_text(0, 2, "TARE SET");
                display_flush();
                xSemaphoreGive(s_display_mutex);
                s_tare_msg_until_us = esp_timer_get_time() + TARE_MSG_DURATION_US;
            }
        }
        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "WiiBalanceBoardGate starting");

    s_display_mutex = xSemaphoreCreateMutex();

    esp_err_t disp_err = display_init();
    if (disp_err == ESP_OK) {
        display_draw_text(0, 0, "WIFI AP  C:0");
        display_draw_text(0, 1, "BT: SCANNING");
        init_cop_panel();
        display_flush();
    } else {
        ESP_LOGW(TAG, "No display (or init failed) - continuing without it");
    }

    ESP_ERROR_CHECK(wifi_ap_start(WIFI_AP_SSID, WIFI_AP_PASSWORD));
    ESP_ERROR_CHECK(dns_server_start()); // lets any hostname (e.g. http://board/) resolve, not just the IP
    ESP_ERROR_CHECK(ws_server_start());
    wiimote_bt_start(on_sample, on_bt_state);
    xTaskCreate(tare_button_task, "tare_btn", 2048, NULL, 5, NULL);

    if (disp_err == ESP_OK) {
        // The header (WiFi client count) otherwise only redraws on a BT
        // state change, which usually happens before anyone opens the web
        // page - without this it stays stuck showing the count from then.
        const esp_timer_create_args_t header_timer_args = {
            .callback = &header_timer_cb,
            .name = "header_redraw",
        };
        esp_timer_handle_t header_timer;
        ESP_ERROR_CHECK(esp_timer_create(&header_timer_args, &header_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(header_timer, 1000000)); // 1s
    }
}
