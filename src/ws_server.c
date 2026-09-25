#include "ws_server.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "index_html.h"
#include "wifi_ap.h"

static const char *TAG = "ws_server";

// esp_http_server reserves 3 sockets for itself out of CONFIG_LWIP_MAX_SOCKETS
// (default 10) - 7 is the max we're allowed to configure here.
#define MAX_SOCKETS 7

static httpd_handle_t s_server = NULL;

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "New WebSocket client");
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

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, sizeof(index_html) - 1);
}

static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
};

// Minimal application/x-www-form-urlencoded percent-decoding, in place.
static void url_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '+') {
            *out++ = ' ';
            s++;
        } else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

// POST body: "ssid=...&password=..." (application/x-www-form-urlencoded).
// Saves the credentials and attempts to connect the station interface to
// them right away - see wifi_sta_save_and_connect() for why this blocks.
static esp_err_t wifi_sta_post_handler(httpd_req_t *req)
{
    char body[192];
    int total = req->content_len;
    if (total <= 0 || total >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request body");
        return ESP_FAIL;
    }
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, body + received, total - received);
        if (r <= 0) {
            return ESP_FAIL;
        }
        received += r;
    }
    body[total] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};
    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }
    httpd_query_key_value(body, "password", password, sizeof(password)); // absent = open network
    url_decode(ssid);
    url_decode(password);

    char ip[16] = {0};
    bool ok = wifi_sta_save_and_connect(ssid, password, ip, sizeof(ip));

    char resp[64];
    int n = snprintf(resp, sizeof(resp), "{\"ok\":%s,\"ip\":\"%s\"}", ok ? "true" : "false", ip);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, n);
    return ESP_OK;
}

static const httpd_uri_t wifi_sta_uri = {
    .uri = "/wifi-sta",
    .method = HTTP_POST,
    .handler = wifi_sta_post_handler,
};

static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
    char json[1024];
    if (!wifi_sta_scan(json, sizeof(json))) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static const httpd_uri_t wifi_scan_uri = {
    .uri = "/wifi-scan",
    .method = HTTP_GET,
    .handler = wifi_scan_get_handler,
};

esp_err_t ws_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = MAX_SOCKETS;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(s_server, &root_uri);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(s_server, &wifi_sta_uri);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(s_server, &wifi_scan_uri);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_register_uri_handler(s_server, &ws_uri);
}

// Fills `out_fds` (capacity MAX_SOCKETS) with the fds that are actual live
// WebSocket clients, returns how many. The full session list includes plain
// HTTP sockets (e.g. someone mid-load of "/") too, which we filter out.
static size_t list_ws_clients(int *out_fds)
{
    int fds[MAX_SOCKETS];
    size_t n = MAX_SOCKETS;
    if (!s_server || httpd_get_client_list(s_server, &n, fds) != ESP_OK) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            out_fds[count++] = fds[i];
        }
    }
    return count;
}

int ws_server_client_count(void)
{
    int fds[MAX_SOCKETS];
    return (int)list_ws_clients(fds);
}

// httpd_ws_send_frame_async() is misleadingly named - per its own header
// comment it's a low-level, *synchronous* send meant to be called from
// within a httpd_queue_work() work item, not directly from an arbitrary
// task. Called directly (as this used to), a client whose connection went
// stale without a clean close (phone walked out of WiFi range, etc.) can
// make the underlying socket send() block for a long time - and since this
// was called straight from the Bluetooth event task, that stalled *all*
// Bluetooth processing, sample logging and display updates right along with
// it: a total, silent freeze with no crash to explain it. Queuing the actual
// send onto the httpd's own task instead means a stuck client only stalls
// WebSocket delivery, not the whole device.
#define MAX_BROADCAST_LEN 256

typedef struct {
    size_t len;
    char data[MAX_BROADCAST_LEN];
} broadcast_work_t;

static void broadcast_work_fn(void *arg)
{
    broadcast_work_t *w = (broadcast_work_t *)arg;
    int fds[MAX_SOCKETS];
    size_t n = list_ws_clients(fds);

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)w->data,
        .len = w->len,
    };
    for (size_t i = 0; i < n; i++) {
        esp_err_t err = httpd_ws_send_frame_async(s_server, fds[i], &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Send to fd=%d failed: %s", fds[i], esp_err_to_name(err));
        }
    }
    free(w);
}

void ws_server_broadcast(const char *data, size_t len)
{
    if (!s_server || len == 0) {
        return;
    }
    if (len > MAX_BROADCAST_LEN) {
        len = MAX_BROADCAST_LEN; // shouldn't happen - our JSON messages are much smaller
    }
    broadcast_work_t *w = malloc(sizeof(broadcast_work_t));
    if (!w) {
        return;
    }
    memcpy(w->data, data, len);
    w->len = len;
    if (httpd_queue_work(s_server, broadcast_work_fn, w) != ESP_OK) {
        free(w);
    }
}
