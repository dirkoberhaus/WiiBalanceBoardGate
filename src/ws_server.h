#pragma once

#include <stddef.h>

#include "esp_err.h"

// Starts an HTTP server with a WebSocket endpoint at "/ws" that streams
// balance board samples to any connected client(s).
esp_err_t ws_server_start(void);

// Sends `data` (of `len` bytes, treated as a text frame) to every currently
// connected WebSocket client. Safe to call from any task.
void ws_server_broadcast(const char *data, size_t len);
