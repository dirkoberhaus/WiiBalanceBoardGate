#pragma once

#include "esp_http_server.h"

// Call once at the end of app_main(), after everything else started
// successfully. Confirms the currently running image to the bootloader so it
// won't be rolled back on the next reset - see CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
// in sdkconfig.defaults for why this matters.
void ota_mark_app_valid(void);

// GET: returns {"running":"<version>"} for the currently active image.
esp_err_t ota_version_get_handler(httpd_req_t *req);

// POST: body is a raw firmware .bin (Content-Length required). Writes it to
// the inactive OTA slot, validates it, and - on success - reboots into it
// after the response is sent. On failure, the device keeps running the
// current image untouched.
esp_err_t ota_update_post_handler(httpd_req_t *req);
