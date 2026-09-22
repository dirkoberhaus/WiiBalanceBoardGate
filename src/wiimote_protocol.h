#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Wiimote/Balance-Board wire protocol: command builders and report parsers.
 * Ported from RombergLab's wiiRemoteFramework/WiiRemote.m (DarwiinRemote),
 * which is a known-working reference implementation. Byte offsets here are
 * shifted by -2 relative to that Objective-C code's `dp[]` arrays, because
 * esp_hidh already strips the Bluetooth transaction header and report-ID
 * byte before handing us `data`/`length` (see ESP_HIDH_INPUT_EVENT).
 */

#define WIIMOTE_MAX_CMD_LEN 21

typedef struct {
    uint16_t top_right, bottom_right, top_left, bottom_left;
} wii_quad_t;

typedef struct {
    wii_quad_t quad[3]; // calibration points at 0kg, 17kg, 34kg
    bool valid;
} wii_bb_calib_t;

typedef struct {
    uint32_t seq;
    int32_t top_right_raw, bottom_right_raw, top_left_raw, bottom_left_raw;
    float top_right_kg, bottom_right_kg, top_left_kg, bottom_left_kg;
    bool kg_valid;
} wii_bb_sample_t;

// Command builders. Fill `out` (>= WIIMOTE_MAX_CMD_LEN bytes) with the OUTPUT
// report payload and set *report_id; return the payload length.
size_t wiimote_build_status_request(uint8_t *out, uint8_t *report_id);
size_t wiimote_build_write(uint8_t *out, uint8_t *report_id, uint32_t addr, const uint8_t *data, size_t len);
size_t wiimote_build_read(uint8_t *out, uint8_t *report_id, uint32_t addr, uint16_t len);
size_t wiimote_build_set_report_mode(uint8_t *out, uint8_t *report_id, uint8_t mode);

// Status report (report ID 0x20).
bool wiimote_parse_status(const uint8_t *data, size_t len, uint8_t *battery_raw, bool *ext_attached);

// Read-data ack (report ID 0x21) for the expansion ID read at 0x04A400F0.
bool wiimote_is_balance_board_ext_id(const uint8_t *data, size_t len);

// Read-data ack (report ID 0x21) for the balance board's two calibration reads.
void wiimote_bb_calib_apply_low(wii_bb_calib_t *calib, const uint8_t *data, size_t len);
void wiimote_bb_calib_apply_high(wii_bb_calib_t *calib, const uint8_t *data, size_t len);

// Balance board data report (report ID 0x34).
void wiimote_bb_parse_report(const uint8_t *data, size_t len, const wii_bb_calib_t *calib, wii_bb_sample_t *out);
