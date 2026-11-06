// Over-the-air updates, pull-based.
//
// The device is battery powered, asleep most of the time, and behind someone
// else's home NAT, so nothing can push to it. Instead it wakes, fetches a small
// JSON manifest, and updates itself only if the manifest names a version other
// than the one it is running.
//
//   { "version": "0.2.0",
//     "url": "https://.../beachday-reterminal-e1001.bin",
//     "md5": "<32 hex chars>",
//     "size": 964577,
//     "sig": "<base64 ECDSA P-256 signature of the .bin>",
//     "notes": "what changed" }
//
// Publish that with tools/release.sh. Nothing needs to be hosted beyond a
// static file, so GitHub Releases is enough. An image without a valid "sig"
// from the release key (lib/otasig) is never installed: the manifest and the
// download are fetched without checking certificates, so the signature is
// what stops anyone between the board and GitHub from replacing the firmware.
#pragma once
#include <cstddef>
#include <cstdint>
#include "settings.h"

enum class OtaResult : uint8_t {
  Skipped,        // not due, disabled, or battery too low
  UpToDate,
  Failed,
  Updated         // never actually returned: the device reboots into the new image
};

// Fetches the manifest and updates if it names a different version.
// `force` ignores the check interval (used by the button-held update path).
OtaResult otaCheck(const Settings& s, bool force, char* status, size_t statusLen);

// True when at least CFG_OTA_CHECK_HOURS have passed since the last check.
bool otaDue(const Settings& s);
