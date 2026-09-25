#include <string.h>

#include "wifi_ap.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifi_ap";

#define STA_NVS_NAMESPACE "wifi_sta"
#define STA_MAX_RETRIES 5
#define STA_CONNECTED_BIT BIT0
#define STA_FAILED_BIT BIT1

static EventGroupHandle_t s_sta_event_group;
static int s_sta_retry_count = 0;
static bool s_sta_connected = false;
static char s_sta_ip[16] = {0};

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "Station " MACSTR " associated (aid=%d)", MAC2STR(e->mac), e->aid);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        // Common reasons: 2 = auth expired, 15 = 4-way handshake timeout,
        // 23/24 = IEEE 802.1X/PSK failure - a 15 here usually means BT/WiFi
        // radio coexistence starved the handshake, not an actually-wrong password.
        ESP_LOGW(TAG, "Station " MACSTR " disconnected, reason=%d", MAC2STR(e->mac), e->reason);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        if (s_sta_retry_count < STA_MAX_RETRIES) {
            s_sta_retry_count++;
            ESP_LOGW(TAG, "Hotspot connection attempt %d/%d...", s_sta_retry_count, STA_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "Giving up on the hotspot after %d tries - staying on the local AP only",
                     STA_MAX_RETRIES);
            xEventGroupSetBits(s_sta_event_group, STA_FAILED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_sta_retry_count = 0;
        s_sta_connected = true;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "Connected to hotspot, IP: %s", s_sta_ip);
        xEventGroupSetBits(s_sta_event_group, STA_CONNECTED_BIT);
    }
}

esp_err_t wifi_ap_start(const char *ssid, const char *password)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    // Always dual-mode: the AP never goes away, a phone's personal hotspot
    // (if configured) is only ever an *additional* uplink on top of it -
    // see wifi_sta_try_saved()/wifi_sta_save_and_connect().
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Suffix the SSID with part of this chip's own MAC so multiple gateways
    // flashed from the same firmware don't all broadcast an identical,
    // indistinguishable network name. Uses the ESP32's address (stable from
    // boot) rather than the balance board's (only known after it connects,
    // and would change if a different board gets paired to this gateway).
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    char full_ssid[33];
    snprintf(full_ssid, sizeof(full_ssid), "%s-%02X%02X%02X", ssid, mac[3], mac[4], mac[5]);

    wifi_config_t wifi_cfg = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_cfg.ap.ssid, full_ssid, sizeof(wifi_cfg.ap.ssid) - 1);
    wifi_cfg.ap.ssid_len = strlen(full_ssid);
    strncpy((char *)wifi_cfg.ap.password, password, sizeof(wifi_cfg.ap.password) - 1);
    if (strlen(password) == 0) {
        wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    s_sta_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP '%s' started, connect and browse to 192.168.4.1", full_ssid);
    return ESP_OK;
}

static void start_sta_connect(const char *ssid, const char *password)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = strlen(password) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    s_sta_retry_count = 0;
    xEventGroupClearBits(s_sta_event_group, STA_CONNECTED_BIT | STA_FAILED_BIT);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    esp_wifi_connect();
}

void wifi_sta_try_saved(void)
{
    nvs_handle_t h;
    if (nvs_open(STA_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return; // nothing saved yet
    }
    char ssid[33] = {0};
    char pass[65] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);
    esp_err_t err = nvs_get_str(h, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) {
        nvs_get_str(h, "pass", pass, &pass_len); // may legitimately be absent (open network)
    }
    nvs_close(h);

    if (err != ESP_OK) {
        return;
    }
    ESP_LOGI(TAG, "Trying saved hotspot '%s'...", ssid);
    start_sta_connect(ssid, pass);
}

bool wifi_sta_save_and_connect(const char *ssid, const char *password, char *ip_out, size_t ip_len)
{
    nvs_handle_t h;
    if (nvs_open(STA_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", password);
        nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "Could not save hotspot credentials to NVS (will still try connecting now)");
    }

    start_sta_connect(ssid, password);

    EventBits_t bits = xEventGroupWaitBits(s_sta_event_group, STA_CONNECTED_BIT | STA_FAILED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & STA_CONNECTED_BIT) {
        if (ip_out && ip_len > 0) {
            strncpy(ip_out, s_sta_ip, ip_len - 1);
            ip_out[ip_len - 1] = '\0';
        }
        return true;
    }
    return false;
}

bool wifi_sta_is_connected(void)
{
    return s_sta_connected;
}

#define WIFI_SCAN_MAX_RESULTS 15

static void sanitize_for_json(char *s)
{
    for (; *s; s++) {
        if (*s == '"' || *s == '\\' || (unsigned char)*s < 0x20) {
            *s = '_';
        }
    }
}

bool wifi_sta_scan(char *json_out, size_t json_len)
{
    wifi_scan_config_t scan_cfg = {0}; // defaults: all channels, active scan
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        return false;
    }

    uint16_t num = WIFI_SCAN_MAX_RESULTS;
    wifi_ap_record_t records[WIFI_SCAN_MAX_RESULTS];
    esp_wifi_scan_get_ap_records(&num, records);

    // Strongest signal first, simple insertion sort (num is tiny).
    for (int i = 1; i < num; i++) {
        wifi_ap_record_t key = records[i];
        int j = i - 1;
        while (j >= 0 && records[j].rssi < key.rssi) {
            records[j + 1] = records[j];
            j--;
        }
        records[j + 1] = key;
    }

    int pos = snprintf(json_out, json_len, "[");
    bool first = true;
    for (int i = 0; i < num; i++) {
        char ssid[33];
        strncpy(ssid, (const char *)records[i].ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        if (ssid[0] == '\0') {
            continue; // hidden network - nothing to select
        }
        sanitize_for_json(ssid);

        char needle[40];
        snprintf(needle, sizeof(needle), "\"%s\"", ssid);
        if (strstr(json_out, needle)) {
            continue; // duplicate AP broadcasting the same SSID (mesh, etc.)
        }

        int remaining = (int)json_len - pos;
        if (remaining < 48) {
            break; // not enough room left for another entry, safely
        }
        pos += snprintf(json_out + pos, remaining, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                         first ? "" : ",", ssid, records[i].rssi);
        first = false;
    }
    int remaining = (int)json_len - pos;
    snprintf(json_out + pos, remaining > 0 ? remaining : 1, "]");
    return true;
}
