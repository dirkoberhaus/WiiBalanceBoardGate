#include "ws_server.h"

#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "ws_server";

#define MAX_CLIENTS 4

static httpd_handle_t s_server = NULL;
static int s_client_fds[MAX_CLIENTS];

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Handshake just completed - remember this client's socket for broadcasts.
        int fd = httpd_req_to_sockfd(req);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (s_client_fds[i] == -1) {
                s_client_fds[i] = fd;
                ESP_LOGI(TAG, "Client connected (fd=%d)", fd);
                break;
            }
        }
        return ESP_OK;
    }

    // We don't expect meaningful data from clients, just drain frames so the
    // connection stays alive and closes are detected.
    httpd_ws_frame_t frame = {0};
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    if (frame.len > 0) {
        uint8_t *buf = calloc(1, frame.len + 1);
        if (!buf) {
            return ESP_ERR_NO_MEM;
        }
        frame.payload = buf;
        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        free(buf);
    }
    return ret;
}

static const httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .is_websocket = true,
};

esp_err_t ws_server_start(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        s_client_fds[i] = -1;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = MAX_CLIENTS + 1;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_register_uri_handler(s_server, &ws_uri);
}

void ws_server_broadcast(const char *data, size_t len)
{
    if (!s_server) {
        return;
    }
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = len,
    };
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_client_fds[i] == -1) {
            continue;
        }
        esp_err_t err = httpd_ws_send_frame_async(s_server, s_client_fds[i], &frame);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "Client fd=%d gone, dropping", s_client_fds[i]);
            s_client_fds[i] = -1;
        }
    }
}
