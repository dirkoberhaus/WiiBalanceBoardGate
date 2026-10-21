#include <stdio.h>

#include "driver/gpio.h"
#include "esp_coexist.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "battery.h"
#include "display.h"
#include "dns_server.h"
#include "ota.h"
#include "wifi_ap.h"
#include "ws_server.h"
#include "wiimote_bt.h"
#if defined(DISPLAY_SSD1306) || defined(DISPLAY_ST7789)
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#endif

static const char *TAG = "main";

// Change these before flashing, or move to Kconfig/NVS later.
#define WIFI_AP_SSID "WiiBalanceBoardGate"
#define WIFI_AP_PASSWORD "balanceboard"

// Heltec WiFi LoRa32 V2's PRG button, wired active-low with an external pull-up.
// Doubles as a tare ("zero out the current load") button for the balance board.
#define TARE_BUTTON_GPIO GPIO_NUM_0

// GPIO2 is the onboard LED on essentially every plain ESP32 DevKitC-style
// board (and is unused/unrouted, so harmless, on the Heltec and TTGO boards
// too) - lights it up as a simple "board is connected and streaming" signal
// for setups without a display.
#define STATUS_LED_GPIO GPIO_NUM_2

// Redraw the live weight rows at most this often - they arrive at ~100Hz,
// far faster than the OLED (and I2C bus) need to be driven.
#define SAMPLE_REDRAW_INTERVAL_US (200 * 1000)
#define TARE_MSG_DURATION_US (2 * 1000 * 1000)

// Heltec/TTGO only (see the deep-sleep block in app_main()/header_timer_cb()):
// if the Balance Board hasn't been connected for this long, sleep until the
// PRG/tare button wakes the board back up. Mini D1 has no dedicated button to
// wake it with, so it's excluded - it just keeps scanning forever.
#define DEEP_SLEEP_IDLE_US (120 * 1000 * 1000)

static wiimote_bt_state_t s_bt_state = WIIMOTE_BT_SCANNING;
static int64_t s_last_redraw_us = 0;
static int64_t s_tare_msg_until_us = 0;
static int64_t s_last_connected_us = 0;

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

// Row for the battery line on boards with no graphics panel (SSD1306) - rows
// 4-7 are otherwise unused there (0:WIFI, 1:BT, 2:tare message, 3:UNCALIBRATED).
// The ST7789's rows are packed tight around its COP panel (see COP_PANEL_TOP_PX),
// so there's no spare row there - its battery reading (when it has one) rides
// along in the WIFI line instead.
#define BATTERY_ROW 4

static void redraw_header(void)
{
    char line[48]; // generous vs. the display's actual column count - just truncates on draw
    bool gfx = display_has_graphics();
    int batt = battery_read_percent();

    if (gfx && batt >= 0) {
        snprintf(line, sizeof(line), "WIFI C:%d BAT:%d%%", ws_server_client_count(), batt);
    } else {
        snprintf(line, sizeof(line), "WIFI AP  C:%d", ws_server_client_count());
    }

    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    display_draw_text(0, 0, line);
    display_draw_text(0, 1, bt_state_text(s_bt_state));
    if (!gfx) {
        if (batt >= 0) {
            snprintf(line, sizeof(line), "BAT: %d%%", batt);
            display_draw_text(0, BATTERY_ROW, line);
        } else {
            display_draw_text(0, BATTERY_ROW, ""); // blank the row
        }
    }
    display_flush();
    xSemaphoreGive(s_display_mutex);
}

#if defined(DISPLAY_SSD1306) || defined(DISPLAY_ST7789)
static void enter_deep_sleep(void)
{
    ESP_LOGW(TAG, "No Balance Board for %d minutes - going to deep sleep. "
                   "Press the PRG/tare button to wake up.", (int)(DEEP_SLEEP_IDLE_US / 1000000 / 60));
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    display_draw_text(0, 0, "SLEEPING");
    display_draw_text(0, 1, "press button to wake");
    display_flush();
    xSemaphoreGive(s_display_mutex);
    vTaskDelay(pdMS_TO_TICKS(200)); // let the display transaction finish

    rtc_gpio_pullup_en(TARE_BUTTON_GPIO);
    rtc_gpio_pulldown_dis(TARE_BUTTON_GPIO);
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(TARE_BUTTON_GPIO, 0)); // wake on button press (active low)
    esp_deep_sleep_start();
}
#endif

static void header_timer_cb(void *arg)
{
    (void)arg;
    redraw_header();

#if defined(DISPLAY_SSD1306) || defined(DISPLAY_ST7789)
    if (s_bt_state != WIIMOTE_BT_STREAMING &&
        esp_timer_get_time() - s_last_connected_us >= DEEP_SLEEP_IDLE_US) {
        enter_deep_sleep();
    }
#endif
}

static void on_bt_state(wiimote_bt_state_t state)
{
    s_bt_state = state;
    gpio_set_level(STATUS_LED_GPIO, state == WIIMOTE_BT_STREAMING ? 1 : 0);
    // The WiFi AP runs the whole time, which shares the single radio with
    // classic BT - fine once streaming (small, interrupt-driven HID
    // reports), but GAP inquiry/paging while scanning or opening the
    // connection is far more latency-sensitive and loses badly to WiFi
    // beacon/traffic contention on boards with less RF margin (see
    // ARCHITECTURE.md). Bias the coexistence arbiter toward BT for exactly
    // that fragile window, then hand fairness back to WiFi once streaming.
    esp_coex_preference_set(state == WIIMOTE_BT_STREAMING ? ESP_COEX_PREFER_BALANCE : ESP_COEX_PREFER_BT);
    if (state == WIIMOTE_BT_STREAMING) {
        s_last_connected_us = esp_timer_get_time(); // resets the deep-sleep idle timer
    }
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

    // Deliberately no kg numbers shown on-device: this runs in youth sport
    // contexts, where displaying someone's body weight where others might
    // see it isn't appropriate. Only the graphical (relative) COP panel -
    // not a numeric readout - conveys anything at all; that's all balance
    // training actually needs. Same reasoning applies to the web UI, where
    // the numbers live behind an explicit "show details" toggle instead of
    // being on the main screen.
    uint8_t status_row = gfx ? 7 : 3; // below the graphics panel vs. its own row
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    if (s->kg_valid) {
        display_draw_text(0, status_row, "");
    } else {
        display_draw_text(0, status_row, "UNCALIBRATED");
    }

    if (s_tare_msg_until_us && now >= s_tare_msg_until_us) {
        display_draw_text(0, 2, ""); // blank the row
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

    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);
    gpio_set_level(STATUS_LED_GPIO, 0);

    esp_err_t disp_err = display_init();
    if (disp_err == ESP_OK) {
        display_draw_text(0, 0, "WIFI AP  C:0");
        display_draw_text(0, 1, "BT: SCANNING");
        init_cop_panel();
        display_flush();
    } else {
        ESP_LOGW(TAG, "No display (or init failed) - continuing without it");
    }

    if (!battery_init()) {
        ESP_LOGI(TAG, "No battery sense on this board - skipping");
    }
    s_last_connected_us = esp_timer_get_time(); // starts the deep-sleep idle countdown from boot

    ESP_ERROR_CHECK(wifi_ap_start(WIFI_AP_SSID, WIFI_AP_PASSWORD));
    wifi_sta_try_saved(); // if a phone hotspot was saved before, try it in the background
    ESP_ERROR_CHECK(dns_server_start()); // lets any hostname (e.g. http://board/) resolve, not just the IP
    ESP_ERROR_CHECK(ws_server_start());
    wiimote_bt_start(on_sample, on_bt_state);
    xTaskCreate(tare_button_task, "tare_btn", 2048, NULL, 5, NULL);

    // Everything above came up without crashing - tell the bootloader this
    // image is good, so a freshly-OTA'd build that reboot-loops gets rolled
    // back automatically instead of leaving the board stuck.
    ota_mark_app_valid();

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
