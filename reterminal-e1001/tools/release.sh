#!/usr/bin/env bash
# Publish a firmware update that every device will pick up on its own.
#
#   tools/release.sh                      build, write the manifest, print steps
#   tools/release.sh --publish            also create the GitHub release (gh)
#   PANEL=e1002 tools/release.sh --publish   the colour panel
#
# Each panel gets its own tag and asset, because the binaries are not
# interchangeable - CFG_OTA_MANIFEST_URL must point a board at its own.
#
# Bump FIRMWARE_VERSION in src/version.h first. Devices compare that string
# against the manifest's "version" and update when they differ, so publishing
# without bumping it does nothing.
set -euo pipefail
cd "$(dirname "$0")/.."

PIO=.venv/bin/pio
[ -x "$PIO" ] || { echo "Missing .venv - see README"; exit 1; }

PANEL="${PANEL:-e1001}"
case "$PANEL" in
  e1001) BUILD_ENV=reterminal_e1001 ;;
  e1002) BUILD_ENV=reterminal_e1002 ;;
  *) echo "PANEL must be e1001 or e1002"; exit 1 ;;
esac

VERSION=$(sed -n 's/.*FIRMWARE_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/version.h)
[ -n "$VERSION" ] || { echo "Could not read FIRMWARE_VERSION from src/version.h"; exit 1; }

REPO=$(git config --get remote.origin.url | sed -E 's#.*github\.com[:/]([^/]+/[^/.]+)(\.git)?#\1#')
TAG="fw-$PANEL-v$VERSION"
ASSET="beachday-reterminal-$PANEL-$VERSION.bin"
OUT=dist
BIN=.pio/build/$BUILD_ENV/firmware.bin

echo "==> Version $VERSION  panel $PANEL  (repo $REPO, tag $TAG)"

# Refuse to ship firmware whose logic has not been checked.
echo "==> Running the rules tests"
"$PIO" test -e native

echo "==> Building the release firmware (rules are embedded as the fallback copy)"
"$PIO" run -e "$BUILD_ENV"

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
  echo "==> Publishing $TAG"
  # Devices fetch /releases/latest/download/manifest.json, so this must be the
  # latest non-draft, non-prerelease release.
  gh release create "$TAG" "$OUT/$ASSET" "$OUT/manifest.json" \
    --title "Firmware $VERSION" --notes "$NOTES" --latest
  echo "==> Published. Devices update within CFG_OTA_CHECK_HOURS,"
  echo "    or immediately if someone holds the left button (KEY2) at wake."
else
  echo
  echo "Not published. To publish:"
  echo "  tools/release.sh --publish"
  echo "or upload $OUT/$ASSET and $OUT/manifest.json to a '$TAG' release marked latest."
fi
