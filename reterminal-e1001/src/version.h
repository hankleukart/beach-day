// Firmware identity. Bump MINOR for behaviour changes, PATCH for fixes.
// tools/release.sh reads this to name the release and build the manifest, and
// the device compares it against the manifest's "version" to decide on OTA.
#pragma once
#define FIRMWARE_VERSION "0.1.0"
