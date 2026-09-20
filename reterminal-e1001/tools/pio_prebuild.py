# PlatformIO pre-build hook (extra_scripts). Keeps shared/v3/day-outcomes.json
# the single source of truth:
#   - copies it to data/ so `pio run -t uploadfs` puts it on LittleFS
#   - embeds it in src/spec_embedded.h as the boot-time fallback
Import("env")
import pathlib, shutil

root = pathlib.Path(env["PROJECT_DIR"])
src = root.parent / "shared" / "v3" / "day-outcomes.json"
data = root / "data" / "day-outcomes.json"
hdr = root / "src" / "spec_embedded.h"

text = src.read_text(encoding="utf-8")
data.parent.mkdir(exist_ok=True)
if not data.exists() or data.read_text(encoding="utf-8") != text:
    shutil.copyfile(src, data)

body = ('// GENERATED at build time by tools/pio_prebuild.py from shared/v3/day-outcomes.json.\n'
        '// Fallback copy of the rules, used when LittleFS has none (or a broken one).\n'
        '#pragma once\n#include <pgmspace.h>\n'
        'static const char SPEC_EMBEDDED[] PROGMEM = R"SPECJSON(' + text + ')SPECJSON";\n'
        'static const size_t SPEC_EMBEDDED_LEN = sizeof(SPEC_EMBEDDED) - 1;\n')
if not hdr.exists() or hdr.read_text(encoding="utf-8") != body:
    hdr.write_text(body, encoding="utf-8")
    print("pio_prebuild: refreshed src/spec_embedded.h")
