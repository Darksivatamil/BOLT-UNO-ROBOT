#!/usr/bin/env bash
# ============================================================================
#  BOLT UNO ROBOT - build + upload helper
#
#  Usage:
#      ./upload.sh                 build + upload the main firmware
#      ./upload.sh --test          build + upload the hardware diagnostic
#      ./upload.sh --build         compile only (no board needed)
#      ./upload.sh /dev/ttyACM0    use a specific port
#      ./upload.sh --test --build  compile the diagnostic only
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FQBN="arduino:avr:uno"
SKETCH="$HERE/BOLT_UNO_ROBOT"
ACTION="upload"
PORT=""

usage() {
  echo "Usage: $0 [--test] [--build] [/dev/ttyACM0]"
  echo "  (no options)  build + upload the main firmware"
  echo "  --test        build + upload BOLT_TEST diagnostic instead"
  echo "  --build       compile only, do not upload"
}

for arg in "$@"; do
  case "$arg" in
    --build)   ACTION="build" ;;
    --test)    SKETCH="$HERE/BOLT_TEST" ;;
    -h|--help) usage; exit 0 ;;
    /dev/*)    PORT="$arg" ;;
    *)         echo "ERROR: unknown argument: $arg"; usage; exit 1 ;;
  esac
done

command -v arduino-cli >/dev/null 2>&1 || {
  echo "ERROR: arduino-cli is not installed or not in PATH."
  exit 1
}

echo "==> Compiling $SKETCH for $FQBN"
arduino-cli compile --fqbn "$FQBN" --warnings all "$SKETCH"

if [[ "$ACTION" == "build" ]]; then
  echo "==> Build only requested. Done."
  exit 0
fi

if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/ttyACM* 2>/dev/null | head -n1 || true)"
fi
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/ttyUSB* 2>/dev/null | head -n1 || true)"
fi

if [[ -z "$PORT" ]]; then
  echo
  echo "ERROR: no Arduino serial port found."
  echo "  - is the USB cable connected (data cable, not charge-only)?"
  echo "  - check:  arduino-cli board list"
  echo "  - permissions:  sudo usermod -aG dialout \$USER   (then log out/in)"
  exit 1
fi

echo
echo "=============================================================="
echo " BEFORE UPLOADING:"
echo "   1. switch the battery OFF and unplug the boost output"
echo "   2. unplug the HC-05/06 TX and RX wires (they are on pins 0/1)"
echo "   USB and the 5 V boost must never power the board together."
echo "=============================================================="
echo
echo "==> Uploading to $PORT"
arduino-cli upload --fqbn "$FQBN" -p "$PORT" --verify "$SKETCH"

echo
echo "==> Upload complete and verified."
echo "    Switch off, reconnect the Bluetooth TX/RX, switch on."
if [[ "$SKETCH" == *BOLT_TEST ]]; then
  echo "    Open Serial Monitor at 9600 and follow the checklist."
else
  echo "    Hold the button while powering on to run the self test."
fi
