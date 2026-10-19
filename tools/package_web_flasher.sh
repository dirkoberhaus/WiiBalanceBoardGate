#!/bin/bash
# Builds all three board environments and packages tools/web-flash/ into a
# zip a non-technical customer can unzip and run locally (see
# tools/web-flash/ANLEITUNG.txt) to flash a board over USB via the browser
# (ESP Web Tools / WebSerial) - no PlatformIO/esptool install needed on
# their end. Re-run this after every firmware change meant to reach boards
# that are still on the pre-OTA partition table.
set -euo pipefail
cd "$(dirname "$0")/.."

PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
ENVS=(heltec-lora32-v2 ttgo-t-display mini-d1)
WEBFLASH_DIR="tools/web-flash"
VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo unknown)"
DIST_DIR="dist"
ZIP_PATH="$DIST_DIR/wiibalanceboardgate-web-flasher-${VERSION}.zip"

echo "Building firmware for: ${ENVS[*]}"
for env in "${ENVS[@]}"; do
    "$PIO" run -e "$env"
done

echo "Copying flash artifacts into $WEBFLASH_DIR/bin/"
rm -rf "$WEBFLASH_DIR/bin"
for env in "${ENVS[@]}"; do
    mkdir -p "$WEBFLASH_DIR/bin/$env"
    for f in bootloader.bin partitions.bin ota_data_initial.bin firmware.bin; do
        cp ".pio/build/$env/$f" "$WEBFLASH_DIR/bin/$env/$f"
    done
done

mkdir -p "$DIST_DIR"
rm -f "$ZIP_PATH"
(cd "$WEBFLASH_DIR" && zip -rq "../../$ZIP_PATH" . -x '*.DS_Store')

echo "Done: $ZIP_PATH"
