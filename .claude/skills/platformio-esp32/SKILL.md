---
name: platformio-esp32
description: Find, flash, and read serial output from the ESP32 boards used in WiiBalanceBoardGate via PlatformIO, from inside Claude Code's non-interactive Bash tool.
---

# PlatformIO / ESP32 access for WiiBalanceBoardGate

This project targets plain ESP32 (classic-BT-capable) boards via PlatformIO with
espidf. Three physical boards exist: a Heltec WiFi LoRa32 V2 and a TTGO
T-Display (both with a built-in display), and a bare "Mini D1"-style ESP32
board with no display. Which one is connected is only knowable from what
firmware is actually running on it (see "Identify which board/build is
connected" below) — don't assume from the port name.

## `pio` is not on PATH

The Bash tool's shell does not have PlatformIO's venv on PATH. Always call the
full path, or export it once per session:

```bash
~/.platformio/penv/bin/pio <command>
# or
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

## Finding the connected board

```bash
~/.platformio/penv/bin/pio device list
```

Look for an entry like `/dev/cu.usbserial-XXXXXXXX` with a real Hardware ID
(e.g. `VID:PID=1A86:55D4`, "USB Single Serial" — a CH340/CH343/CH9102 USB-UART
chip, which is what these boards use). Ignore `/dev/cu.debug-console`,
`/dev/cu.Bluetooth-*`, and other unrelated macOS virtual ports.

## Building and flashing

Two named environments exist in `platformio.ini` (`board = esp32dev` for
both, they differ only in `-D DISPLAY_SSD1306` vs `-D DISPLAY_ST7789`):

```bash
~/.platformio/penv/bin/pio run -e heltec-lora32-v2               # build only
~/.platformio/penv/bin/pio run -e ttgo-t-display -t upload --upload-port /dev/cu.usbserial-XXXXXXXX
```

There is no dedicated environment for the bare "Mini D1" (no-display) board —
it gets flashed with one of the two existing environments; the display init
code just runs against nonexistent hardware and is harmless (it doesn't
verify the display actually acknowledges anything, so `display_*: Display
initialized` in the log does **not** prove a physical display is present).

## Reading serial output non-interactively

`pio device monitor` refuses to run under the Bash tool: it requires a real
interactive terminal and errors with `UserSideException: ... requires an
interactive terminal on stdin`. Read the port directly with pyserial instead
(PlatformIO's own venv already has it installed):

```bash
~/.platformio/penv/bin/python -c "
import serial, time
port = '/dev/cu.usbserial-XXXXXXXX'
ser = serial.Serial(port, 115200, timeout=1)
end = time.time() + 20   # capture window in seconds
data = b''
while time.time() < end:
    chunk = ser.read(256)
    if chunk:
        data += chunk
ser.close()
print(data.decode(errors='replace'))
"
```

Print each chunk as it arrives (`print(chunk.decode(...), end='', flush=True)`
inside the loop) if you need to watch it live rather than dump it all at the
end.

### Forcing a reset to capture the boot log

If the board is already running, you'll only see whatever it logs *after*
you attach — not the boot banner. Toggle DTR/RTS before reading to trigger
the board's auto-reset circuit (same sequence esptool uses):

```python
ser.dtr = False
ser.rts = True
time.sleep(0.1)
ser.rts = False
time.sleep(0.1)
ser.dtr = True
```

**Caveat:** this toggling transiently disturbs GPIO0. In this project GPIO0
doubles as the tare-button input (`TARE_BUTTON_GPIO` in `src/main.c`), so a
`main: Tare button pressed` line immediately after a DTR/RTS-triggered reset
is very likely an artifact of the reset sequence itself, not a real button
press — don't read it as a hardware fault.

## Identify which board/build is connected

The boot log's `app_init` block tells you what's actually flashed, which may
be older than `main`'s HEAD and may not match the physical board (see above):

```
I (601) app_init: Project name:     WiiBalanceBoardGate
I (606) app_init: App version:      <git describe, "-dirty" if uncommitted changes were present at build time>
I (610) app_init: Compile time:     <build timestamp>
```

And shortly after, one of:
```
I (...) display_ssd1306: Display initialized     <- built with -D DISPLAY_SSD1306 (heltec-lora32-v2 env)
I (...) display_st7789: Display initialized      <- built with -D DISPLAY_ST7789 (ttgo-t-display env)
```
No such line at all (just `main: No display (or init failed) - continuing
without it`) means `display_init()` genuinely failed — but a line being
present does *not* confirm a physical display exists, only that the SPI/I2C
init calls returned OK.

## Normal vs. abnormal boot

A healthy boot's first lines look like:
```
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
```
If instead you see `boot:0x1 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))`,
GPIO0 was held low at reset (stuck tare button, bad wiring, or a manual
boot-mode entry) and the app never runs — that's the actual "board doesn't
boot" signature, distinct from BT/WiFi-level issues where the app boots fine
but `wiimote_bt: Scanning for a Wii Balance Board...` just never finds a
device (that's Bluetooth Classic inquiry discovery/timing, not a firmware or
hardware fault — see `src/wiimote_bt.c`).
