#pragma once

#include "wiimote_protocol.h"

// Called from the Bluetooth event task for every parsed balance board sample.
// Do not block for long in this callback.
typedef void (*wiimote_bt_sample_cb_t)(const wii_bb_sample_t *sample);

// Brings up the Bluetooth Classic controller/stack, then continuously scans
// for and connects to a device whose name starts with "Nintendo RVL", runs
// the Wiimote handshake, and invokes `cb` for every balance-board report.
// Reconnects automatically if the board disconnects.
void wiimote_bt_start(wiimote_bt_sample_cb_t cb);
