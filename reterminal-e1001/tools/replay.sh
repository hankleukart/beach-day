#!/usr/bin/env bash
# How often would the board have changed its mind? Replays archived forecast
# runs through the real engine and counts every outcome change.
#
#     tools/replay.sh                      # the last 60 days, Venice Beach
#     tools/replay.sh --days 30 --sensitivity
#     tools/replay.sh --lat 34.02 --lon -118.49
#
# Fetches are cached in .pio/replay/cache, so a second run is offline and
# instant — and a rules change can be replayed against the same weather.
# Caveats (6-hourly runs only, AQI held fixed) are at the top of
# tools/replay/replay.py.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=.pio/replay
mkdir -p "$BUILD"
SRC=(tools/replay/replay.cpp lib/beachrules/wmo.cpp
     lib/dayrules/dayspec.cpp lib/dayrules/dayvalidate.cpp lib/dayrules/derive.cpp)

if [ ! -x "$BUILD/replay" ] || [ -n "$(find "${SRC[@]}" lib -newer "$BUILD/replay" 2>/dev/null)" ]; then
  echo "building replay..." >&2
  cc -std=c11 -O1 -c -o "$BUILD/toml.o" -I lib/toml lib/toml/toml.c
  c++ -std=gnu++17 -O1 -o "$BUILD/replay" \
      -I lib/dayrules -I lib/beachrules -I lib/toml \
      -DBEACHRULES_NATIVE -DDAYRULES_SPEC_PATH='"../shared/v3/day-outcomes.toml"' \
      -Wall -Wno-unused-function \
      "${SRC[@]}" "$BUILD/toml.o"
fi

.venv/bin/python tools/replay/replay.py "$BUILD/replay" ../shared/v3/day-outcomes.toml "$@"
