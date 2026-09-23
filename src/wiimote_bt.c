#include "wiimote_bt.h"

#include <stdlib.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wiimote_bt";

// Only devices whose Bluetooth name starts with this are considered - covers
// both the Wiimote ("Nintendo RVL-CNT-01") and the Balance Board
// ("Nintendo RVL-WBC-01"); we then confirm via the expansion device ID.
static const char *kNamePrefix = "Nintendo RVL";

typedef enum {
    WBT_SCANNING,
    WBT_OPENING,
    WBT_WAIT_STATUS,
    WBT_WAIT_EXT_ID,
    WBT_WAIT_CALIB_LOW,
    WBT_WAIT_CALIB_HIGH,
    WBT_STREAMING,
} wbt_state_t;

static wbt_state_t s_state = WBT_SCANNING;
static wii_bb_calib_t s_calib;
static uint32_t s_seq = 0;
static wiimote_bt_sample_cb_t s_sample_cb = NULL;
static wiimote_bt_state_cb_t s_state_cb = NULL;
static esp_hidh_dev_t *s_dev = NULL;

// Tare: subtracts whatever was on the board at the moment of the request from
// every corner's kg reading from then on, like a kitchen scale's tare button.
static volatile bool s_tare_requested = false;
static float s_tare_kg[4] = {0}; // top_left, top_right, bottom_left, bottom_right

typedef struct {
    wiimote_bt_sample_cb_t sample_cb;
    wiimote_bt_state_cb_t state_cb;
} bt_init_args_t;

static void notify_state(wiimote_bt_state_t state)
{
    if (s_state_cb) {
        s_state_cb(state);
    }
}

static void send_status_request(esp_hidh_dev_t *dev)
{
    uint8_t buf[WIIMOTE_MAX_CMD_LEN];
    uint8_t report_id;
    size_t len = wiimote_build_status_request(buf, &report_id);
    esp_hidh_dev_output_set(dev, 0, report_id, buf, len);
}

static void send_write(esp_hidh_dev_t *dev, uint32_t addr, const uint8_t *data, size_t data_len)
{
    uint8_t buf[WIIMOTE_MAX_CMD_LEN];
    uint8_t report_id;
    size_t len = wiimote_build_write(buf, &report_id, addr, data, data_len);
    esp_hidh_dev_output_set(dev, 0, report_id, buf, len);
}

static void send_read(esp_hidh_dev_t *dev, uint32_t addr, uint16_t read_len)
{
    uint8_t buf[WIIMOTE_MAX_CMD_LEN];
    uint8_t report_id;
    size_t len = wiimote_build_read(buf, &report_id, addr, read_len);
    esp_hidh_dev_output_set(dev, 0, report_id, buf, len);
}

static void send_set_report_mode(esp_hidh_dev_t *dev, uint8_t mode)
{
    uint8_t buf[WIIMOTE_MAX_CMD_LEN];
    uint8_t report_id;
    size_t len = wiimote_build_set_report_mode(buf, &report_id, mode);
    esp_hidh_dev_output_set(dev, 0, report_id, buf, len);
}

static void start_scan(void)
{
    s_state = WBT_SCANNING;
    s_dev = NULL;
    memset(&s_calib, 0, sizeof(s_calib));
    notify_state(WIIMOTE_BT_SCANNING);
    ESP_LOGI(TAG, "Scanning for a Wii Balance Board...");
    // ~15 * 1.28s inquiry window; GAP keeps reporting ESP_BT_GAP_DISC_RES_EVT
    // for each device seen until this elapses or we cancel it ourselves.
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 15, 0);
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED && s_state == WBT_SCANNING) {
            // Inquiry window elapsed with no match - just keep trying.
            start_scan();
        }
        return;
    }

    if (event != ESP_BT_GAP_DISC_RES_EVT || s_state != WBT_SCANNING) {
        return;
    }

    for (int i = 0; i < param->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
        if (p->type != ESP_BT_GAP_DEV_PROP_BDNAME) {
            continue;
        }
        const char *name = (const char *)p->val;
        if (strncmp(name, kNamePrefix, strlen(kNamePrefix)) != 0) {
            continue;
        }
        ESP_LOGI(TAG, "Found '%s', connecting...", name);
        s_state = WBT_OPENING;
        notify_state(WIIMOTE_BT_CONNECTING);
        esp_bt_gap_cancel_discovery();
        esp_hidh_dev_open(param->disc_res.bda, ESP_HID_TRANSPORT_BT, 0);
        return;
    }
}

static void hidh_cb(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDH_START_EVENT:
        ESP_LOGI(TAG, "HID host started");
        start_scan();
        break;

    case ESP_HIDH_OPEN_EVENT:
        if (param->open.status != ESP_OK) {
            ESP_LOGW(TAG, "Open failed: %s - retrying scan", esp_err_to_name(param->open.status));
            start_scan();
            break;
        }
        ESP_LOGI(TAG, "Connected, requesting status");
        s_dev = param->open.dev;
        s_state = WBT_WAIT_STATUS;
        notify_state(WIIMOTE_BT_HANDSHAKE);
        send_status_request(s_dev);
        break;

    case ESP_HIDH_CLOSE_EVENT:
        ESP_LOGW(TAG, "Disconnected - rescanning");
        esp_hidh_dev_free(param->close.dev);
        vTaskDelay(pdMS_TO_TICKS(500));
        start_scan();
        break;

    case ESP_HIDH_INPUT_EVENT: {
        uint8_t report_id = (uint8_t)param->input.report_id;
        const uint8_t *data = param->input.data;
        size_t len = param->input.length;
        esp_hidh_dev_t *dev = param->input.dev;

        if (report_id == 0x20 && s_state == WBT_WAIT_STATUS) {
            uint8_t battery;
            bool ext_attached;
            if (wiimote_parse_status(data, len, &battery, &ext_attached) && ext_attached) {
                ESP_LOGI(TAG, "Expansion attached, battery=%u/0xC0", battery);
                uint8_t zero = 0x00;
                send_write(dev, 0x04A40040, &zero, 1);
                vTaskDelay(pdMS_TO_TICKS(10));
                send_read(dev, 0x04A400F0, 16);
                s_state = WBT_WAIT_EXT_ID;
            }
        } else if (report_id == 0x21 && s_state == WBT_WAIT_EXT_ID) {
            if (wiimote_is_balance_board_ext_id(data, len)) {
                ESP_LOGI(TAG, "Balance Board expansion confirmed, reading calibration");
                send_read(dev, 0x04A40024, 12);
                s_state = WBT_WAIT_CALIB_LOW;
            } else {
                ESP_LOGE(TAG, "Attached expansion is not a Balance Board");
            }
        } else if (report_id == 0x21 && s_state == WBT_WAIT_CALIB_LOW) {
            wiimote_bb_calib_apply_low(&s_calib, data, len);
            send_read(dev, 0x04A40030, 12);
            s_state = WBT_WAIT_CALIB_HIGH;
        } else if (report_id == 0x21 && s_state == WBT_WAIT_CALIB_HIGH) {
            wiimote_bb_calib_apply_high(&s_calib, data, len);
            ESP_LOGI(TAG, "Calibration %s", s_calib.valid ? "OK" : "INVALID (weight will be uncalibrated)");
            send_set_report_mode(dev, 0x34);
            s_state = WBT_STREAMING;
            notify_state(WIIMOTE_BT_STREAMING);
            ESP_LOGI(TAG, "Streaming started");
        } else if (report_id == 0x34 && s_state == WBT_STREAMING) {
            wii_bb_sample_t sample = {0};
            wiimote_bb_parse_report(data, len, &s_calib, &sample);
            sample.seq = ++s_seq;
            if (sample.kg_valid) {
                if (s_tare_requested) {
                    s_tare_kg[0] = sample.top_left_kg;
                    s_tare_kg[1] = sample.top_right_kg;
                    s_tare_kg[2] = sample.bottom_left_kg;
                    s_tare_kg[3] = sample.bottom_right_kg;
                    s_tare_requested = false;
                    ESP_LOGI(TAG, "Tare captured: tl=%.1f tr=%.1f bl=%.1f br=%.1f kg", s_tare_kg[0],
                             s_tare_kg[1], s_tare_kg[2], s_tare_kg[3]);
                }
                sample.top_left_kg -= s_tare_kg[0];
                sample.top_right_kg -= s_tare_kg[1];
                sample.bottom_left_kg -= s_tare_kg[2];
                sample.bottom_right_kg -= s_tare_kg[3];
            }
            if (sample.seq % 100 == 1) {
                ESP_LOGI(TAG, "sample #%lu tl=%.1f tr=%.1f bl=%.1f br=%.1f kg", (unsigned long)sample.seq,
                         sample.top_left_kg, sample.top_right_kg, sample.bottom_left_kg, sample.bottom_right_kg);
            }
            if (s_sample_cb) {
                s_sample_cb(&sample);
            }
        } else if (s_state != WBT_STREAMING) {
            // Expected while working through the handshake below/above the state we're
            // currently in (e.g. late 0x22 write-acks) - fine to ignore.
            ESP_LOGD(TAG, "Ignoring report 0x%02x in state %d", report_id, s_state);
        }
        // NOTE: if no ESP_HIDH_INPUT_EVENT ever arrives after ESP_HIDH_OPEN_EVENT,
        // esp_hidh's report-descriptor matching (bt_hidh.c, ESP_HIDH_DATA_IND_EVT,
        // "Not find report handle") is the most likely reason - the Balance Board's
        // HID descriptor is not a well-behaved one. The fix in that case is to patch
        // that function to fall back to forwarding raw, unmatched reports instead of
        // dropping them.
        break;
    }

    default:
        break;
    }
}

static void bt_init_task(void *arg)
{
    bt_init_args_t *args = (bt_init_args_t *)arg;
    s_sample_cb = args->sample_cb;
    s_state_cb = args->state_cb;
    free(args);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // We only ever initiate connections outward, no need to be seen/connectable.
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));

    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_cb,
        .event_stack_size = 6144, // sample_cb draws to the display, which needs some headroom
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    vTaskDelete(NULL);
}

void wiimote_bt_start(wiimote_bt_sample_cb_t sample_cb, wiimote_bt_state_cb_t state_cb)
{
    bt_init_args_t *args = malloc(sizeof(bt_init_args_t));
    args->sample_cb = sample_cb;
    args->state_cb = state_cb;
    xTaskCreate(bt_init_task, "wbt_init", 4096, args, 5, NULL);
}

void wiimote_bt_tare(void)
{
    s_tare_requested = true;
}
