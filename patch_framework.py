"""
PlatformIO pre-build script: the Wii Balance Board has no real Bluetooth
pairing support, but ESP-IDF's classic-BT HID host (btc_hh.c) hardcodes
BTA_SEC_AUTHENTICATE | BTA_SEC_ENCRYPT for both BTA_HhEnable and BTA_HhOpen,
which makes the connection fail. There is no Kconfig option for this, so we
patch the vendored framework file directly. Idempotent - safe to run on every
build, and re-applies itself if a fresh/updated framework package is fetched.
"""

Import("env")

import re

platform = env.PioPlatform()
framework_dir = platform.get_package_dir("framework-espidf")
target = (
    f"{framework_dir}/components/bt/host/bluedroid/btc/profile/std/hid/btc_hh.c"
)

MARKER = "WiiBalanceBoardGate"

with open(target, "r") as f:
    src = f.read()

if MARKER not in src:
    before = src
    src = src.replace(
        "BTA_HhEnable(BTA_SEC_AUTHENTICATE | BTA_SEC_ENCRYPT, bte_hh_evt);",
        f"BTA_HhEnable(BTA_SEC_NONE, bte_hh_evt); // {MARKER}: board has no pairing support",
    )
    src = re.sub(
        r"BTA_HhOpen\(arg->connect\.bd_addr, BTA_HH_PROTO_RPT_MODE, \(BTA_SEC_AUTHENTICATE \| BTA_SEC_ENCRYPT\)\);",
        f"BTA_HhOpen(arg->connect.bd_addr, BTA_HH_PROTO_RPT_MODE, BTA_SEC_NONE); // {MARKER}: board has no pairing support",
        src,
    )
    if src == before:
        print(f"WARNING: {target} did not match the expected patch pattern - "
              "the ESP-IDF version may have changed this file; patch it manually.")
    else:
        with open(target, "w") as f:
            f.write(src)
        print(f"Patched {target} for unauthenticated classic-BT HID (Balance Board support)")
