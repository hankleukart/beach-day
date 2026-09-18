// Captive-portal setup: the board becomes a Wi-Fi hotspot, a phone joins it,
// and a small web page collects Wi-Fi, location, a label and the parking
// schedule. Everything is saved to NVS and the board restarts.
//
// Entered when nothing is configured, or when KEY1 (middle) is held while the
// board wakes. Never returns: it restarts after a save, or deep-sleeps after
// the timeout.
#pragma once
#include "settings.h"

[[noreturn]] void runSetupPortal(const Settings& current);
