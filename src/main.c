#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "wifi_ap.h"
#include "ws_server.h"
#include "wiimote_bt.h"

static const char *TAG = "main";

// Change these before flashing, or move to Kconfig/NVS later.
#define WIFI_AP_SSID "WiiBalanceBoardGate"
#define WIFI_AP_PASSWORD "balanceboard"

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
}

void app_main(void)
{
    ESP_LOGI(TAG, "WiiBalanceBoardGate starting");

    ESP_ERROR_CHECK(wifi_ap_start(WIFI_AP_SSID, WIFI_AP_PASSWORD));
    ESP_ERROR_CHECK(ws_server_start());
    wiimote_bt_start(on_sample);
}
