#include "dns_server.h"

#include <string.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"

static const char *TAG = "dns_server";

#define DNS_PORT 53
#define MAX_PACKET 512

// Minimal DNS header field offsets/layout (RFC 1035 §4.1.1) - not using a
// packed struct since the question section between the header and the parts
// we build ourselves is variable-length anyway.
#define DNS_HDR_LEN 12
#define QTYPE_A 1

static uint32_t s_ap_ip = 0; // network byte order

// Finds the end of the QNAME (the 0x00 terminator) starting at `qname`,
// within a buffer of `len` bytes total. Returns NULL if it runs off the end
// (truncated/malformed packet).
static const uint8_t *find_qname_end(const uint8_t *qname, const uint8_t *pkt_end)
{
    const uint8_t *p = qname;
    while (p < pkt_end) {
        uint8_t label_len = *p;
        if (label_len == 0) {
            return p;
        }
        p += 1 + label_len;
    }
    return NULL;
}

static void handle_query(int sock, struct sockaddr_in *from, socklen_t from_len,
                          const uint8_t *req, int req_len)
{
    if (req_len < DNS_HDR_LEN + 5) { // header + at least a 1-byte name + type + class
        return;
    }

    uint16_t qdcount = (req[4] << 8) | req[5];
    if (qdcount < 1) {
        return;
    }

    const uint8_t *pkt_end = req + req_len;
    const uint8_t *qname = req + DNS_HDR_LEN;
    const uint8_t *qname_end = find_qname_end(qname, pkt_end);
    if (!qname_end || qname_end + 5 > pkt_end) {
        return; // malformed/truncated
    }
    const uint8_t *qtype_ptr = qname_end + 1;
    uint16_t qtype = (qtype_ptr[0] << 8) | qtype_ptr[1];
    size_t question_len = (qtype_ptr + 4) - qname; // QNAME + QTYPE + QCLASS

    if (qtype != QTYPE_A) {
        // Don't answer AAAA/other queries - answering them with an A record
        // would be a type mismatch a strict resolver could reject. The
        // client will fall back to an A query shortly, which we do answer.
        return;
    }

    uint8_t resp[DNS_HDR_LEN + 512];
    size_t pos = 0;

    memcpy(resp, req, 2); // transaction ID, echoed
    resp[2] = 0x81;        // response, standard query, recursion desired
    resp[3] = 0x80;        // recursion available, no error
    resp[4] = 0x00; resp[5] = 0x01; // QDCOUNT = 1
    resp[6] = 0x00; resp[7] = 0x01; // ANCOUNT = 1
    resp[8] = 0x00; resp[9] = 0x00; // NSCOUNT = 0
    resp[10] = 0x00; resp[11] = 0x00; // ARCOUNT = 0
    pos = DNS_HDR_LEN;

    if (pos + question_len > sizeof(resp)) {
        return; // implausibly long name - drop rather than overflow
    }
    memcpy(resp + pos, qname, question_len);
    pos += question_len;

    uint8_t answer[] = {
        0xC0, 0x0C,             // NAME: pointer to the question's QNAME at offset 12
        0x00, 0x01,             // TYPE = A
        0x00, 0x01,             // CLASS = IN
        0x00, 0x00, 0x00, 0x3C, // TTL = 60s
        0x00, 0x04,             // RDLENGTH = 4
        0, 0, 0, 0,             // RDATA, filled in below
    };
    memcpy(answer + 12, &s_ap_ip, 4);

    if (pos + sizeof(answer) > sizeof(resp)) {
        return;
    }
    memcpy(resp + pos, answer, sizeof(answer));
    pos += sizeof(answer);

    sendto(sock, resp, pos, 0, (struct sockaddr *)from, from_len);
}

static void dns_server_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind() to port %d failed", DNS_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Catch-all DNS server listening on port %d", DNS_PORT);

    uint8_t buf[MAX_PACKET];
    while (1) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (len > 0) {
            handle_query(sock, &from, from_len, buf, len);
        }
    }
}

esp_err_t dns_server_start(void)
{
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) {
        return ESP_FAIL;
    }
    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(ap_netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }
    s_ap_ip = ip_info.ip.addr;

    if (xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
