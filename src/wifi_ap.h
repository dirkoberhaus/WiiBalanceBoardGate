#pragma once

#include "esp_err.h"

/*
 * Brings the ESP32 up as its own WiFi access point, so a phone can connect
 * directly to it without depending on venue/gym WiFi being present.
 */
esp_err_t wifi_ap_start(const char *ssid, const char *password);
