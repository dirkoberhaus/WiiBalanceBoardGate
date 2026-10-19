#include "ota.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

// Chunk size for reading the upload body - on the heap, not the stack, since
// the httpd worker task's stack is small (see ws_server.c's own notes on
// that task).
#define OTA_RECV_BUF_LEN 4096

// Gives the HTTP response time to actually reach the client's socket before
// the reboot tears the connection down.
#define OTA_REBOOT_DELAY_MS 1000

void ota_mark_app_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        // Not fatal - just means this build doesn't have rollback enabled
        // (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=n) or this is the factory
        // image from before OTA existed on this board.
        ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback: %s", esp_err_to_name(err));
    }
}

esp_err_t ota_version_get_handler(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t desc;
    char json[96];
    int n;
    if (running && esp_ota_get_partition_description(running, &desc) == ESP_OK) {
        n = snprintf(json, sizeof(json), "{\"running\":\"%s\"}", desc.version);
    } else {
        n = snprintf(json, sizeof(json), "{\"running\":\"unknown\"}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS));
    esp_restart();
}

esp_err_t ota_update_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Content-Length");
        return ESP_FAIL;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Starting OTA write to \"%s\" (%d bytes incoming)", target->label, req->content_len);

    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(target, req->content_len, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return ESP_FAIL;
    }

    uint8_t *buf = malloc(OTA_RECV_BUF_LEN);
    if (!buf) {
        esp_ota_abort(handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    bool write_failed = false;
    while (remaining > 0) {
        int to_read = remaining < OTA_RECV_BUF_LEN ? remaining : OTA_RECV_BUF_LEN;
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue; // retry, matches httpd_req_recv()'s documented usage pattern
            }
            ESP_LOGE(TAG, "httpd_req_recv failed: %d", r);
            write_failed = true;
            break;
        }
        err = esp_ota_write(handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            write_failed = true;
            break;
        }
        remaining -= r;
    }
    free(buf);

    if (write_failed) {
        esp_ota_abort(handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        return ESP_FAIL;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        // Most commonly ESP_ERR_OTA_VALIDATE_FAILED - image header/magic byte
        // or (if secure boot is on) signature check failed. Current firmware
        // keeps running untouched either way.
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware image");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_set_boot_partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA write complete, rebooting into \"%s\"", target->label);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);

    // Reboot from a separate task, not this handler: esp_restart() never
    // returns, which would leave the response above unflushed and the httpd
    // worker task in a weird state instead of a normal handler return.
    xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}
