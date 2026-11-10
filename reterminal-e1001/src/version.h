// Firmware identity. 1.0 is the version that shipped; each release after it
// goes 1.01, 1.02, ... 1.99 (then 2.0).
// tools/release.sh reads this to name the release and build the manifest, and
// the device compares it against the manifest's "version" to decide on OTA.
//
// BUMP THIS IN THE SAME COMMIT as any change you intend to ship. A device
// compares strings only, so shipping new code under an unchanged version means
// every board reports "up to date" and keeps running the old image.
#pragma once
#define FIRMWARE_VERSION "1.01"
