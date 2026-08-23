#!/usr/bin/env bash
# Build and flash Noisedeck Nano onto a Waveshare ESP32-C6-Touch-AMOLED-2.16.
#
#   bin/flash.sh                 build + flash to the first /dev/cu.usbmodem*
#   bin/flash.sh build           build only
#   PORT=/dev/cu.usbmodem101 bin/flash.sh
#
# Two hard-won rules are encoded here:
#  1. The bootloader header MUST be written as DIO/40MHz. The ROM loader always
#     reads the second-stage bootloader in DIO; a QIO header (what a naive
#     `esptool --flash-mode qio` produces) boot-loops at "ets_loader.c 67".
#     Arduino IDE's "QIO" option quietly does the same thing (qio bootloader
#     binary, dio header), so this matches it.
#  2. Large writes over the C6's USB-JTAG port are flaky on macOS (the stub
#     stops answering mid-stream at random offsets, roughly 1 in 3 runs).
#     esptool verifies every write by hash, so we simply retry until it passes.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH="$ROOT/noisedeck_nano"
BUILD="$ROOT/build"
FQBN="esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB"
CORE="esp32:esp32@3.3.11"
CORE_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
XPOWERS="XPowersLib@0.3.3"

export PATH="/opt/homebrew/bin:$PATH"
export ARDUINO_DIRECTORIES_USER="$ROOT/.arduino-user"

command -v arduino-cli >/dev/null || { echo "arduino-cli missing: brew install arduino-cli" >&2; exit 1; }

PY="$ROOT/.venv/bin/python"
if [ ! -x "$PY" ]; then
  python3 -m venv "$ROOT/.venv"
  "$ROOT/.venv/bin/pip" -q install "esptool>=5.3" pyserial
fi

if ! arduino-cli core list 2>/dev/null | grep -q "^esp32:esp32 *3.3.11"; then
  arduino-cli core update-index --additional-urls "$CORE_URL"
  arduino-cli core install "$CORE" --additional-urls "$CORE_URL"
fi
if ! arduino-cli lib list 2>/dev/null | grep -q "^XPowersLib *0.3.3"; then
  arduino-cli lib install "$XPOWERS"
fi

echo "== build"
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD" --warnings default "$SKETCH" | grep -E "Sketch uses|Global variables"
[ "${1:-}" = "build" ] && exit 0

PORT="${PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)}"
[ -n "$PORT" ] || { echo "no /dev/cu.usbmodem* port found; is the board plugged in?" >&2; exit 1; }
BOOT_APP0="$(ls ~/Library/Arduino15/packages/esp32/hardware/esp32/3.3.11/tools/partitions/boot_app0.bin)"

echo "== flash via $PORT"
for attempt in 1 2 3 4 5 6; do
  if "$PY" -m esptool --chip esp32c6 --port "$PORT" --before default-reset --after hard-reset \
      write-flash --flash-mode dio --flash-freq 40m --flash-size 16MB \
      0x0 "$BUILD/noisedeck_nano.ino.bootloader.bin" \
      0x8000 "$BUILD/noisedeck_nano.ino.partitions.bin" \
      0xe000 "$BOOT_APP0" \
      0x10000 "$BUILD/noisedeck_nano.ino.bin" 2>&1 | grep -E "^Wrote|verified|fatal error"; then
    echo "== flashed and hash-verified (attempt $attempt)"
    exit 0
  fi
  echo "== attempt $attempt failed (USB-JTAG stall), retrying"
  sleep 1
done
echo "flash failed after 6 attempts" >&2
exit 1
