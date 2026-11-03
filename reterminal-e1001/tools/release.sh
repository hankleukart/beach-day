#!/usr/bin/env bash
# Publish a firmware update that every device will pick up on its own.
#
#   tools/release.sh                      build, write the manifest, print steps
#   tools/release.sh --publish            also create the GitHub release (gh)
#
# One binary for both panels: it carries both drivers and each board picks
# its own from the panel model in NVS (src/panelmodel.h). Built from the
# `reterminal` env, which sets no panel, so an update never changes what a
# board thinks it is. Before 0.11.3 there was one release per panel.
#
# Bump FIRMWARE_VERSION in src/version.h first. Devices compare that string
# against the manifest's "version" and update when they differ, so publishing
# without bumping it does nothing.
set -euo pipefail
cd "$(dirname "$0")/.."

PIO=.venv/bin/pio
[ -x "$PIO" ] || { echo "Missing .venv - see README"; exit 1; }

[ -z "${PANEL:-}" ] || { echo "PANEL is gone: one release now serves both panels."; exit 1; }
BUILD_ENV=reterminal

VERSION=$(sed -n 's/.*FIRMWARE_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/version.h)
[ -n "$VERSION" ] || { echo "Could not read FIRMWARE_VERSION from src/version.h"; exit 1; }

REPO=$(git config --get remote.origin.url | sed -E 's#.*github\.com[:/]([^/]+/[^/.]+)(\.git)?#\1#')
TAG="fw-v$VERSION"
ASSET="beachday-reterminal-$VERSION.bin"
OUT=dist
BIN=.pio/build/$BUILD_ENV/firmware.bin

echo "==> Version $VERSION, both panels  (repo $REPO, tag $TAG)"

# Refuse to ship firmware whose logic has not been checked.
echo "==> Running the rules tests"
"$PIO" test -e native

echo "==> Building the release firmware (rules are embedded as the fallback copy)"
"$PIO" run -e "$BUILD_ENV"

# This file goes on a public release. Refuse it if it holds the config file's
# Wi-Fi, and never print what was found.
if [ -f src/beachday_config.h ]; then
  for key in CFG_WIFI_SSID CFG_WIFI_PASSWORD; do
    val=$(sed -n "s/^#define[[:space:]]*$key[[:space:]]*\"\(.*\)\".*/\1/p" src/beachday_config.h)
    case "$val" in ""|YOUR_WIFI|YOUR_PASSWORD) continue ;; esac
    if LC_ALL=C grep -aqF -- "$val" "$BIN"; then
      echo "STOP: the firmware contains $key from beachday_config.h. Not releasing."
      exit 1
    fi
  done
  echo "==> No Wi-Fi from beachday_config.h in the firmware"
fi

mkdir -p "$OUT"
cp "$BIN" "$OUT/$ASSET"
SIZE=$(wc -c < "$OUT/$ASSET" | tr -d ' ')
MD5=$(md5 -q "$OUT/$ASSET" 2>/dev/null || md5sum "$OUT/$ASSET" | cut -d' ' -f1)

NOTES="${RELEASE_NOTES:-$(git log -1 --pretty=%s)}"

cat > "$OUT/manifest.json" <<JSON
{
  "version": "$VERSION",
  "url": "https://github.com/$REPO/releases/download/$TAG/$ASSET",
  "md5": "$MD5",
  "size": $SIZE,
  "notes": "$NOTES"
}
JSON

echo "==> $OUT/manifest.json"
cat "$OUT/manifest.json"

if [ "${1:-}" = "--publish" ]; then
  command -v gh >/dev/null || { echo "gh not installed: https://cli.github.com"; exit 1; }
  # The tag must name the commit this binary was built from. Without
  # --target, gh tags the default branch (main), not this branch.
  git diff --quiet HEAD -- || { echo "Uncommitted changes: commit before publishing"; exit 1; }
  COMMIT=$(git rev-parse HEAD)
  git merge-base --is-ancestor "$COMMIT" "@{u}" 2>/dev/null \
    || { echo "HEAD is not pushed: git push first, so the tag has a commit to point at"; exit 1; }
  echo "==> Publishing $TAG at ${COMMIT:0:7}"
  # Devices fetch /releases/latest/download/manifest.json, so this must be the
  # latest non-draft, non-prerelease release.
  gh release create "$TAG" "$OUT/$ASSET" "$OUT/manifest.json" \
    --title "Firmware $VERSION" --notes "$NOTES" --latest --target "$COMMIT"
  echo "==> Published. Devices update within CFG_OTA_CHECK_HOURS,"
  echo "    or at once on a long press of Right Nav (about 3 s, until the LED)."
else
  echo
  echo "Not published. To publish:"
  echo "  tools/release.sh --publish"
  echo "or upload $OUT/$ASSET and $OUT/manifest.json to a '$TAG' release marked latest."
fi
