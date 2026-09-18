#!/usr/bin/env bash
# Build, flash and monitor the reTerminal E1001.
#
#   ./flash.sh            dev build: stays awake, buttons run a self-test
#   ./flash.sh release    real build: renders once, then deep-sleeps
#   ./flash.sh test       run the rules tests on this Mac, no board needed
#
# If the upload can't find the board, press KEY0 (the right-hand button) to
# wake it and run this again.
set -euo pipefail
cd "$(dirname "$0")"
PIO=.venv/bin/pio
[ -x "$PIO" ] || { echo "Missing .venv - run: python3 -m venv .venv && .venv/bin/pip install platformio"; exit 1; }

case "${1:-dev}" in
  test)    exec "$PIO" test -e native ;;
  release) ENV=reterminal_e1001 ;;
  dev)     ENV=dev ;;
  *)       echo "usage: $0 [dev|release|test]"; exit 1 ;;
esac

[ -f src/beachday_config.h ] || {
  echo "src/beachday_config.h is missing. Create it with:"
  echo "  cp src/beachday_config.example.h src/beachday_config.h"
  echo "then set your Wi-Fi and location."; exit 1
}

"$PIO" run -e "$ENV" -t upload
exec "$PIO" device monitor
