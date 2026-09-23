#pragma once

#include "wiimote_protocol.h"

// Called from the Bluetooth event task for every parsed balance board sample.
// Do not block for long in this callback.
typedef void (*wiimote_bt_sample_cb_t)(const wii_bb_sample_t *sample);

typedef enum {
    WIIMOTE_BT_SCANNING,
    WIIMOTE_BT_CONNECTING,
    WIIMOTE_BT_HANDSHAKE,
    WIIMOTE_BT_STREAMING,
} wiimote_bt_state_t;

// Called whenever the connection state changes. Do not block for long.
typedef void (*wiimote_bt_state_cb_t)(wiimote_bt_state_t state);

// Brings up the Bluetooth Classic controller/stack, then continuously scans
// for and connects to a device whose name starts with "Nintendo RVL", runs
// the Wiimote handshake, and invokes `sample_cb` for every balance-board
// report and `state_cb` on every connection state change. Reconnects
// automatically if the board disconnects. `state_cb` may be NULL.
void wiimote_bt_start(wiimote_bt_sample_cb_t sample_cb, wiimote_bt_state_cb_t state_cb);

// Zeros out the current reading: the next incoming sample's kg values are
// captured as an offset and subtracted from every reading from then on
// (like a kitchen scale's tare button). Safe to call from any task.
void wiimote_bt_tare(void);
