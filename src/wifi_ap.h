#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/*
 * Brings the ESP32 up as its own WiFi access point, so a phone can connect
 * directly to it without depending on venue/gym WiFi being present. Also
 * always brings up the station (client) interface alongside it (WIFI_MODE_
 * APSTA) - see wifi_sta_* below - so a phone's personal hotspot can be
 * joined as an *additional* uplink without ever taking the AP down.
 */
esp_err_t wifi_ap_start(const char *ssid, const char *password);

/*
 * If credentials were previously saved with wifi_sta_save_and_connect(),
 * kicks off a connection attempt to that network in the background
 * (non-blocking - doesn't delay the rest of boot). No-op if nothing was
 * ever saved. The AP keeps running regardless of whether this succeeds.
 */
void wifi_sta_try_saved(void);

/*
 * Saves `ssid`/`password` to NVS (so wifi_sta_try_saved() picks them up on
 * future boots) and attempts to connect now, blocking the calling task for
 * up to ~15s for a definite success/failure result - intended to be called
 * from the web UI's "connect to my hotspot" form handler, where the caller
 * wants to report back whether it actually worked.
 *
 * @param[out] ip_out  On success, filled with the dotted-quad IP obtained
 *                     on that network. May be NULL.
 * @param      ip_len  Size of `ip_out`.
 * @return true if connected and got an IP within the timeout.
 */
bool wifi_sta_save_and_connect(const char *ssid, const char *password, char *ip_out, size_t ip_len);

// True if the station interface currently has an IP on some network.
bool wifi_sta_is_connected(void);

/*
 * Scans for nearby WiFi networks (blocks for a few seconds) and writes the
 * result as a JSON array of {"ssid":..,"rssi":..} into `json_out`, strongest
 * signal first, deduplicated by SSID, hidden networks omitted (nothing
 * meaningful to select there). Returns false if the scan itself failed
 * (json_out left untouched in that case).
 */
bool wifi_sta_scan(char *json_out, size_t json_len);
