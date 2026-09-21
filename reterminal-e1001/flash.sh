#!/usr/bin/env bash
# Build, flash and monitor the reTerminal E1001.
#
#   ./flash.sh [what] [panel]
#
#   what:  dev      stays awake, buttons cycle the outcomes   (default)
#          release  renders once, then deep-sleeps
#          rules    push only shared/v3/day-outcomes.json to the board
#          erase    wipe saved settings (Wi-Fi, location, parking) and reflash
#          test     run the rules tests on this Mac, no board needed
#
#   panel: e1001    7.5" monochrome   (default)
#          e1002    7.3" 6-colour
#
#   e.g.  ./flash.sh dev e1002      ./flash.sh release e1002
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

WHAT="${1:-dev}"
case "${2:-e1001}" in
  e1001) RELEASE_ENV=reterminal_e1001; DEV_ENV=dev ;;
  e1002) RELEASE_ENV=reterminal_e1002; DEV_ENV=dev_e1002 ;;
  *)     echo "unknown panel '${2}' - use e1001 or e1002"; exit 1 ;;
esac

case "$WHAT" in
  test)    exec "$PIO" test -e native ;;
  rules)   exec "$PIO" run -e "$RELEASE_ENV" -t uploadfs ;;
  erase)   echo "Erasing all saved settings..."; "$PIO" run -e "$RELEASE_ENV" -t erase; ENV="$RELEASE_ENV" ;;
  release) ENV="$RELEASE_ENV" ;;
  dev)     ENV="$DEV_ENV" ;;
  *)       echo "usage: $0 [dev|release|rules|erase|test] [e1001|e1002]"; exit 1 ;;
esac
echo "==> $ENV"

[ -f src/beachday_config.h ] || {
  echo "src/beachday_config.h is missing. Create it with:"
  echo "  cp src/beachday_config.example.h src/beachday_config.h"
  echo "then set your Wi-Fi and location."; exit 1
}

"$PIO" run -e "$ENV" -t upload
"$PIO" run -e "$ENV" -t uploadfs
exec "$PIO" device monitor -e "$ENV"
