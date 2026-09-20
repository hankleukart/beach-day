#!/usr/bin/env bash
# Build, flash and monitor the reTerminal E1001.
#
#   ./flash.sh            dev build: stays awake, buttons cycle the outcomes
#   ./flash.sh release    real build: renders once, then deep-sleeps
#   ./flash.sh rules      push only shared/v3/day-outcomes.json to the board
#   ./flash.sh erase      wipe saved settings (Wi-Fi, location, parking) and reflash
#                         - use when you want beachday_config.h to win again
#   ./flash.sh test       run the rules tests on this Mac, no board needed
#
# Firmware uploads also upload the LittleFS image, which carries the rules
# JSON; the firmware falls back to a built-in copy if the image is missing.
#
# If the upload can't find the board, press KEY0 (the right-hand button) to
# wake it and run this again.
set -euo pipefail
cd "$(dirname "$0")"
PIO=.venv/bin/pio
[ -x "$PIO" ] || { echo "Missing .venv - run: python3 -m venv .venv && .venv/bin/pip install platformio"; exit 1; }

case "${1:-dev}" in
  test)    exec "$PIO" test -e native ;;
  rules)   exec "$PIO" run -e reterminal_e1001 -t uploadfs ;;
  erase)   echo "Erasing all saved settings..."; "$PIO" run -e reterminal_e1001 -t erase; ENV=reterminal_e1001 ;;
  release) ENV=reterminal_e1001 ;;
  dev)     ENV=dev ;;
  *)       echo "usage: $0 [dev|release|rules|erase|test]"; exit 1 ;;
esac

[ -f src/beachday_config.h ] || {
  echo "src/beachday_config.h is missing. Create it with:"
  echo "  cp src/beachday_config.example.h src/beachday_config.h"
  echo "then set your Wi-Fi and location."; exit 1
}

"$PIO" run -e "$ENV" -t upload
"$PIO" run -e "$ENV" -t uploadfs
exec "$PIO" device monitor -e "$ENV"
