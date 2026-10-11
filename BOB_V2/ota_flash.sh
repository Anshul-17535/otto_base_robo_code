#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
#  BOB WiFi OTA Flash Script
#  Compiles the firmware and pushes it to the robot over WiFi — no USB needed.
#
#  Usage:
#    ./BOB_V2/ota_flash.sh                      # uses defaults below
#    ./BOB_V2/ota_flash.sh 192.168.1.15         # override robot IP
#    ./BOB_V2/ota_flash.sh 192.168.1.15 mypass  # override IP + OTA password
#
#  Prerequisites (one-time):
#    The robot must be running a firmware built with PartitionScheme=app3M_fat9M_16MB
#    (two 3 MB OTA slots).  If the robot is still on huge_app, use USB once, then
#    this script handles all future updates.
#
#  What it does:
#    1. Compiles BOB_V2/Otto_ESP32S3 with the OTA partition scheme
#    2. POSTs the .bin to http://<ROBOT_IP>/firmware
#    3. Robot flashes the inactive OTA slot and reboots automatically
#    4. Waits for the robot to come back up and prints /hw status
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail
cd "$(dirname "$0")/.."

ROBOT_IP="${1:-192.168.1.15}"
OTA_PASS="${2:-bob12345}"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,UploadSpeed=921600"
SKETCH="BOB_V2/Otto_ESP32S3"
BUILD_DIR="/tmp/bob-ota-async-build"
ARDUINO_CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║           BOB WiFi OTA Flash                                     ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo "  Robot  : http://$ROBOT_IP"
echo "  Sketch : $SKETCH"
echo "  Build  : $BUILD_DIR"
echo ""

# ── 1. Compile ────────────────────────────────────────────────────────────────
echo "▶ Compiling…"
"$ARDUINO_CLI" compile \
  --fqbn "$FQBN" \
  --build-path "$BUILD_DIR" \
  --library "$PWD/BOB_V2/libraries/AsyncTCP" \
  --library "$PWD/BOB_V2/libraries/ESPAsyncWebServer" \
  "$SKETCH"
echo "  ✓ Compiled: $(du -sh "$BUILD_DIR/Otto_ESP32S3.ino.bin" | cut -f1) binary"
echo ""

# ── 2. Push over WiFi ─────────────────────────────────────────────────────────
echo "▶ Uploading to http://$ROBOT_IP/firmware …"
RESULT=$(curl -s -X POST "http://$ROBOT_IP/firmware" \
  -H "X-OTA-Password: $OTA_PASS" \
  -F "firmware=@$BUILD_DIR/Otto_ESP32S3.ino.bin" \
  --progress-bar 2>&1)
echo ""
echo "  Server replied: $RESULT"

if echo "$RESULT" | grep -q '"ok":true'; then
  echo "  ✓ Upload accepted — robot is rebooting"
else
  echo "  ✗ Upload failed. Check robot IP, password and partition scheme."
  echo "    If the robot is on huge_app, flash via USB once first."
  exit 1
fi

# ── 3. Wait for reboot ────────────────────────────────────────────────────────
echo ""
echo "▶ Waiting for robot to reboot (up to 20 s)…"
for i in $(seq 1 10); do
  sleep 2
  STATUS=$(curl -s --connect-timeout 3 "http://$ROBOT_IP/hw" 2>/dev/null || true)
  if echo "$STATUS" | grep -q '"ready"'; then
    echo "  ✓ Robot is back online"
    UPTIME=$(echo "$STATUS" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('uptime','?'))" 2>/dev/null || echo "?")
    echo "  Uptime: ${UPTIME}s"
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║  OTA complete. Open http://$ROBOT_IP to verify.      ║"
    echo "║                                                                  ║"
    echo "║  IMPORTANT: Go to the IMU tab and click Calibrate Now — the     ║"
    echo "║  IMU calibration does not survive a reboot.                     ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo ""
    exit 0
  fi
  echo "  … waiting ($((i*2))s)"
done

echo ""
echo "  Robot did not respond within 20 s. Check http://$ROBOT_IP manually."
exit 1
