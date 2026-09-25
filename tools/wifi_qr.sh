#!/usr/bin/env bash
# Generates a "scan to join WiFi" QR code PNG for one WiiBalanceBoardGate
# device. Each device has its own SSID (a per-chip MAC suffix is appended at
# boot - see the "AP '...' started" line in its serial log), so this needs
# to be run once per physical board, after flashing it.
#
# Requires `qrencode` (brew install qrencode / apt install qrencode).
#
# Usage: tools/wifi_qr.sh <SSID> [password] [output.png]
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <SSID> [password] [output.png]" >&2
    echo "Example: $0 WiiBalanceBoardGate-XXXXXX balanceboard" >&2
    exit 1
fi

ssid="$1"
password="${2:-balanceboard}"
out="${3:-wifi-qr-${ssid}.png}"

if ! command -v qrencode >/dev/null 2>&1; then
    echo "qrencode not found - install it first (brew install qrencode)" >&2
    exit 1
fi

# WiFi QR payload format (recognized natively by iOS/Android camera apps).
# Escape the characters the spec treats as special: \ ; , " and :
escape() {
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/;/\\;/g' -e 's/,/\\,/g' -e 's/"/\\"/g' -e 's/:/\\:/g'
}

ssid_esc=$(escape "$ssid")
pass_esc=$(escape "$password")

qrencode -o "$out" -s 10 -m 2 "WIFI:T:WPA;S:${ssid_esc};P:${pass_esc};;"
echo "Wrote $out - print it or display it next to the board."
