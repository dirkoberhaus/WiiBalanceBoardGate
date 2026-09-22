#include "wiimote_protocol.h"

#include <string.h>

#define WII_DECRYPT(x) ((uint8_t)((((x) ^ 0x17) + 0x17) & 0xFF))

size_t wiimote_build_status_request(uint8_t *out, uint8_t *report_id)
{
    *report_id = 0x15;
    out[0] = 0x00;
    return 1;
}

size_t wiimote_build_write(uint8_t *out, uint8_t *report_id, uint32_t addr, const uint8_t *data, size_t len)
{
    *report_id = 0x16;
    memset(out, 0, 21);
    out[0] = (addr >> 24) & 0xFF;
    out[1] = (addr >> 16) & 0xFF;
    out[2] = (addr >> 8) & 0xFF;
    out[3] = addr & 0xFF;
    out[4] = (uint8_t)len;
    if (len > 16) {
        len = 16; // wire format only allows 16 data bytes per write
    }
    memcpy(out + 5, data, len);
    return 21;
}

size_t wiimote_build_read(uint8_t *out, uint8_t *report_id, uint32_t addr, uint16_t len)
{
    *report_id = 0x17;
    out[0] = (addr >> 24) & 0xFF;
    out[1] = (addr >> 16) & 0xFF;
    out[2] = (addr >> 8) & 0xFF;
    out[3] = addr & 0xFF;
    out[4] = (len >> 8) & 0xFF;
    out[5] = len & 0xFF;
    return 6;
}

size_t wiimote_build_set_report_mode(uint8_t *out, uint8_t *report_id, uint8_t mode)
{
    *report_id = 0x12;
    out[0] = 0x02; // continuous reporting, no rumble
    out[1] = mode;
    return 2;
}

bool wiimote_parse_status(const uint8_t *data, size_t len, uint8_t *battery_raw, bool *ext_attached)
{
    if (len < 6) {
        return false;
    }
    *ext_attached = (data[2] & 0x02) != 0;
    *battery_raw = data[5];
    return true;
}

bool wiimote_is_balance_board_ext_id(const uint8_t *data, size_t len)
{
    if (len < 20) {
        return false;
    }
    return WII_DECRYPT(data[19]) == 0x2A;
}

void wiimote_bb_calib_apply_low(wii_bb_calib_t *calib, const uint8_t *data, size_t len)
{
    if (len < 17) {
        return;
    }
    calib->quad[0].top_right    = ((uint16_t)data[5] << 8) | data[6];
    calib->quad[0].bottom_right = ((uint16_t)data[7] << 8) | data[8];
    calib->quad[0].top_left     = ((uint16_t)data[9] << 8) | data[10];
    calib->quad[0].bottom_left  = ((uint16_t)data[11] << 8) | data[12];
    calib->quad[1].top_right    = ((uint16_t)data[13] << 8) | data[14];
    calib->quad[1].bottom_right = ((uint16_t)data[15] << 8) | data[16];
}

static bool quads_increasing(const wii_quad_t *q)
{
    return q[0].top_right < q[1].top_right && q[1].top_right < q[2].top_right &&
           q[0].bottom_right < q[1].bottom_right && q[1].bottom_right < q[2].bottom_right &&
           q[0].top_left < q[1].top_left && q[1].top_left < q[2].top_left &&
           q[0].bottom_left < q[1].bottom_left && q[1].bottom_left < q[2].bottom_left;
}

void wiimote_bb_calib_apply_high(wii_bb_calib_t *calib, const uint8_t *data, size_t len)
{
    if (len < 17) {
        return;
    }
    calib->quad[1].top_left     = ((uint16_t)data[5] << 8) | data[6];
    calib->quad[1].bottom_left  = ((uint16_t)data[7] << 8) | data[8];
    calib->quad[2].top_right    = ((uint16_t)data[9] << 8) | data[10];
    calib->quad[2].bottom_right = ((uint16_t)data[11] << 8) | data[12];
    calib->quad[2].top_left     = ((uint16_t)data[13] << 8) | data[14];
    calib->quad[2].bottom_left  = ((uint16_t)data[15] << 8) | data[16];
    calib->valid = quads_increasing(calib->quad);
}

// valLow -> calibratedLow, valHigh -> calibratedHigh, linearly interpolated/extrapolated.
static float bb_interp2(int32_t raw, uint16_t val_low, uint16_t val_high, float low, float high)
{
    return low + ((float)(raw - (int32_t)val_low) * (high - low) / (float)((int32_t)val_high - (int32_t)val_low));
}

static float bb_interpolate(int32_t raw, uint16_t val0, uint16_t val17, uint16_t val34)
{
    if (raw < val17) {
        return bb_interp2(raw, val0, val17, 0.0f, 17.0f);
    }
    return bb_interp2(raw, val17, val34, 17.0f, 34.0f);
}

void wiimote_bb_parse_report(const uint8_t *data, size_t len, const wii_bb_calib_t *calib, wii_bb_sample_t *out)
{
    if (len < 10) {
        return;
    }
    out->top_right_raw    = ((uint16_t)data[2] << 8) | data[3];
    out->bottom_right_raw = ((uint16_t)data[4] << 8) | data[5];
    out->top_left_raw     = ((uint16_t)data[6] << 8) | data[7];
    out->bottom_left_raw  = ((uint16_t)data[8] << 8) | data[9];

    out->kg_valid = calib->valid;
    if (calib->valid) {
        out->top_right_kg = bb_interpolate(out->top_right_raw, calib->quad[0].top_right,
                                            calib->quad[1].top_right, calib->quad[2].top_right);
        out->bottom_right_kg = bb_interpolate(out->bottom_right_raw, calib->quad[0].bottom_right,
                                               calib->quad[1].bottom_right, calib->quad[2].bottom_right);
        out->top_left_kg = bb_interpolate(out->top_left_raw, calib->quad[0].top_left,
                                           calib->quad[1].top_left, calib->quad[2].top_left);
        out->bottom_left_kg = bb_interpolate(out->bottom_left_raw, calib->quad[0].bottom_left,
                                              calib->quad[1].bottom_left, calib->quad[2].bottom_left);
    }
}
