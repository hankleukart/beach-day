#!/usr/bin/env bash
# Render the layout to PNGs on this Mac, no board involved.
#
#     tools/preview.sh                # every scenario, both panels -> docs-design/preview/
#     tools/preview.sh rain           # one scenario, both panels
#     tools/preview.sh rain mono      # one scenario, one panel
#
# Scenarios live in tools/preview/preview.cpp: beach, rain, winter, smoke,
# stress (the worst-case copy that squeezes the stat band), snow, and whimsy
# (a date that swaps one tile).
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=docs-design/preview
BUILD=.pio/preview
mkdir -p "$OUT" "$BUILD"

SRC=(tools/preview/preview.cpp tools/preview/panel_preview.cpp
     src/render.cpp src/paint.cpp src/text.cpp src/icons.cpp
     lib/beachrules/wmo.cpp
     lib/dayrules/dayspec.cpp lib/dayrules/dayvalidate.cpp lib/dayrules/derive.cpp)

if [ ! -x "$BUILD/preview" ] || [ -n "$(find "${SRC[@]}" src/*.h lib -newer "$BUILD/preview" 2>/dev/null)" ]; then
  echo "building preview..."
  # tomlc99 is C; the rest is C++. Keeping them apart is what the PlatformIO
  # envs do too, they just do it invisibly.
  cc -std=c11 -O1 -g -c -o "$BUILD/toml.o" -I lib/toml lib/toml/toml.c
  c++ -std=gnu++17 -O1 -g -o "$BUILD/preview" \
      -I tools/preview/shim -I src -I lib/dayrules -I lib/beachrules -I lib/stb -I lib/toml \
      -DBEACHRULES_NATIVE -DDAYRULES_SPEC_PATH='"../shared/v3/day-outcomes.toml"' \
      -Wall -Wno-unused-function \
      "${SRC[@]}" "$BUILD/toml.o"
fi

scenarios=("${1:-all}")
[ "${scenarios[0]}" = "all" ] && scenarios=(beach rain winter smoke stress snow whimsy trash kite)
panels=("${2:-all}")
[ "${panels[0]}" = "all" ] && panels=(mono color)

for s in "${scenarios[@]}"; do
  for p in "${panels[@]}"; do
    "$BUILD/preview" "$s" "$p" "$BUILD/d.ppm" "$BUILD/r.ppm"
    .venv/bin/python - "$BUILD/d.ppm" "$OUT/$s-$p.png" "$BUILD/r.ppm" "$OUT/$s-$p-readings.png" <<'PY'
import sys
from PIL import Image
for src, dst in ((sys.argv[1], sys.argv[2]), (sys.argv[3], sys.argv[4])):
    Image.open(src).save(dst)
PY
  done
done
echo "-> $OUT"
