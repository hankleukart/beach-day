// Interactive development mode. Compiled only into [env:dev] (-DBEACHDAY_DEV).
//
// The release build deep-sleeps at the end of setup(), which tears down the
// USB-CDC serial port a few seconds after boot. That makes monitoring and
// re-flashing painful, so the dev build stays awake here instead and lets the
// three front buttons drive the display.
#pragma once
#ifdef BEACHDAY_DEV
#include "render.h"

// Never returns: replaces deep sleep in the dev build.
[[noreturn]] void devLoop(const ViewModel& liveView, bool liveValid);
#endif
