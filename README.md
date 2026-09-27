# WiiBalanceBoardGate

ESP32 firmware that connects to a Wii Balance Board over classic Bluetooth
(BR/EDR) as a HID host, and re-broadcasts the live weight data over its own
WiFi access point — as JSON over WebSocket, plus a small built-in web page
that visualizes it. Useful for devices whose own Bluetooth stack struggles
with the board's legacy, pairing-less handshake (e.g. modern Macs/iPhones).

## Hardware requirements

**The ESP32 chip must support classic Bluetooth (BR/EDR)**, not just BLE.
That means the original **ESP32** (Xtensa LX6, dual-core) — printed on the
module as something like `ESP32-D0WDQ6`, `ESP32-WROOM-32`, etc. It does
**not** include ESP32-S2, S3, C2, C3, C6 or H2 — none of those have classic
Bluetooth, only BLE (or none at all), and the board simply won't be found.

Three boards are supported out of the box, each its own PlatformIO
environment:

| Environment | Board | Display |
|---|---|---|
| `heltec-lora32-v2` (default) | Heltec WiFi LoRa32 V2 | 128x64 SSD1306 OLED |
| `ttgo-t-display` | TTGO/LILYGO T-Display | 240x135 ST7789 color TFT |
| `mini-d1` | Bare ESP32 (e.g. a Mini D1 board), no display | none |

All use plain `board = esp32dev` in `platformio.ini` — pins for the display,
buttons etc. are hardcoded per board in `src/display_ssd1306.c` /
`src/display_st7789.c` / `src/display_none.c` (see comments there). The
`mini-d1` environment sets no display build flag at all; `display_none.c`'s
stub is what gets compiled in whenever neither `DISPLAY_SSD1306` nor
`DISPLAY_ST7789` is defined, and `display_init()` reporting failure is
already handled gracefully everywhere it's used. Porting to another
original-ESP32 board with its own display mainly means adding a fourth
display backend.

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension). The
  `espressif32` platform and toolchain are fetched automatically on first
  build — no manual ESP-IDF install needed.
- A USB cable to your board.

## Build

```sh
# Heltec WiFi LoRa32 V2 (default environment)
pio run -e heltec-lora32-v2

# TTGO T-Display
pio run -e ttgo-t-display

# Bare ESP32, no display (e.g. Mini D1)
pio run -e mini-d1
```

The first build downloads the ESP32 toolchain/framework and can take
several minutes; after that, incremental builds are fast.

A pre-build step (`patch_framework.py`) automatically patches one file in
the downloaded ESP-IDF framework — see [ARCHITECTURE.md](ARCHITECTURE.md#the-bta_sec_none-patch)
for why this is necessary. It's idempotent and safe to ignore; you'll see a
`Patched ...` line the first time it runs.

## Flash

Find your board's serial port:

```sh
pio device list
```

Then, matching the environment to the board you're flashing:

```sh
pio run -e heltec-lora32-v2 --target upload --upload-port /dev/cu.usbserial-XXXX
pio run -e ttgo-t-display --target upload --upload-port /dev/cu.usbserial-XXXX
pio run -e mini-d1 --target upload --upload-port /dev/cu.usbserial-XXXX
```

(Omit `--upload-port` to let PlatformIO auto-detect it, if you only have one
board connected.)

## Watch the logs

`pio device monitor` needs an interactive terminal. If you're driving it
from a script (or an AI agent), talk to the serial port directly instead,
e.g. with Python's `pyserial`:

```python
import serial, time
ser = serial.Serial('/dev/cu.usbserial-XXXX', 115200, timeout=1)
# Toggle DTR/RTS to force a fresh reset so you catch the boot log from the start:
ser.dtr = False; ser.rts = True; time.sleep(0.1); ser.rts = False; time.sleep(0.3)
while True:
    data = ser.read(4096)
    if data:
        print(data.decode('utf-8', errors='replace'), end='')
```

## Using it

1. Power the board. It starts its own WiFi access point named
   `WiiBalanceBoardGate-XXXXXX` (the suffix is unique per chip, so multiple
   gateways don't collide) with password `balanceboard` — change both in
   `src/main.c` (`WIFI_AP_SSID` / `WIFI_AP_PASSWORD`) before flashing if you
   want something else. Since the SSID is only final after flashing (it
   includes a per-chip suffix), generate a "scan to join" QR code for a
   board once you know it, and print/stick it on the board:
   ```sh
   tools/wifi_qr.sh WiiBalanceBoardGate-XXXXXX balanceboard
   ```
   (needs `qrencode` - `brew install qrencode` / `apt install qrencode`)
2. Press the little **SYNC** button in the Balance Board's battery
   compartment. The board only accepts a Bluetooth connection during the
   ~20s window after that press — this needs to happen every time, there's
   no persistent pairing (see [ARCHITECTURE.md](ARCHITECTURE.md#why-sync-every-time)).
3. Connect your phone/laptop to the `WiiBalanceBoardGate-XXXXXX` network and
   browse to **any hostname** — e.g. `http://board/` — for the live
   weight/COP visualization with tap-to-set target and audio deviation
   feedback. The device runs a catch-all DNS server that resolves every name
   to itself, so you don't need to remember an IP address; `192.168.4.1`
   still works too. Connect to `/ws` on whichever host you used for the raw
   JSON WebSocket stream.
4. The onboard display shows connection status and, on the TTGO T-Display,
   a graphical center-of-pressure panel. Press the board's built-in button
   (GPIO0 — the Heltec's PRG button / TTGO's top button) to tare (zero out
   whatever's currently on the board). The `mini-d1` build has no display, so
   status instead comes from the GPIO2 LED (lit while streaming); a tare
   button still needs to be wired to GPIO0 by hand if you want one, since a
   bare ESP32 module has no dedicated button broken out to it — and since the
   board already auto-tares itself once on connect, this is optional.

## Configuration knobs

| What | Where |
|---|---|
| WiFi AP name/password | `WIFI_AP_SSID` / `WIFI_AP_PASSWORD` in `src/main.c` |
| Tare/mode button GPIO | `TARE_BUTTON_GPIO` in `src/main.c` |
| Display pins | top of `src/display_ssd1306.c` / `src/display_st7789.c` |
| Bluetooth device name filter | `kNamePrefix` in `src/wiimote_bt.c` |

See [ARCHITECTURE.md](ARCHITECTURE.md) for how the pieces fit together.
