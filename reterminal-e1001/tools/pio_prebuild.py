# PlatformIO pre-build hook (extra_scripts). Compiles two files into the
# firmware, so each has one source of truth:
#   - shared/v3/day-outcomes.toml (the rules)  -> src/spec_embedded.h
#   - keys/release-signing.pub.pem             -> src/release_key.h
#     the key every update must be signed with (tools/release.sh)
Import("env")
import pathlib

root = pathlib.Path(env["PROJECT_DIR"])


def write_if_changed(path, body):
    if not path.exists() or path.read_text(encoding="utf-8") != body:
        path.write_text(body, encoding="utf-8")
        print(f"pio_prebuild: refreshed {path.relative_to(root)}")


rules = (root.parent / "shared" / "v3" / "day-outcomes.toml").read_text(encoding="utf-8")
write_if_changed(root / "src" / "spec_embedded.h",
    '// GENERATED at build time by tools/pio_prebuild.py from shared/v3/day-outcomes.toml.\n'
    '// The rules the firmware runs on; there is no other copy on the board.\n'
    '#pragma once\n#include <cstddef>\n'
    'static const char SPEC_EMBEDDED[] = R"SPECTOML(' + rules + ')SPECTOML";\n'
    'static const size_t SPEC_EMBEDDED_LEN = sizeof(SPEC_EMBEDDED) - 1;\n')

pub_path = root / "keys" / "release-signing.pub.pem"
if not pub_path.exists():
    raise SystemExit("pio_prebuild: keys/release-signing.pub.pem is missing. "
                     "Without it no update could ever be accepted; see tools/make-signing-key.sh.")
pub = pub_path.read_text(encoding="ascii")
if "-----BEGIN PUBLIC KEY-----" not in pub:
    raise SystemExit("pio_prebuild: keys/release-signing.pub.pem is not a PEM public key.")
write_if_changed(root / "src" / "release_key.h",
    '// GENERATED at build time by tools/pio_prebuild.py from keys/release-signing.pub.pem.\n'
    '// The public half of the release signing key. Not secret.\n'
    '#pragma once\n'
    'static const char RELEASE_PUBLIC_KEY_PEM[] = R"PEM(' + pub + ')PEM";\n')
