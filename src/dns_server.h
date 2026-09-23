#pragma once

#include "esp_err.h"

/*
 * Tiny catch-all DNS server: answers every A-record query on the SoftAP
 * network with this device's own IP, so any hostname resolves to it
 * (e.g. http://board/) instead of needing to type 192.168.4.1. Works on
 * any phone/OS since it's just plain DNS - unlike mDNS/Bonjour ".local"
 * names, which Android's browsers resolve unreliably.
 */
esp_err_t dns_server_start(void);
