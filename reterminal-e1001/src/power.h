#pragma once
#include <cstdint>

float batteryVolts();
int   batteryPercent(float volts);          // rough single-cell LiPo estimate, 0..100
bool  wokeByButton();

// Which button woke the board. Each one has a single-press job, so nobody has
// to hold one button while pressing another.
enum class WakeButton : uint8_t { None, Right, Middle, Left };
WakeButton wakeButton();
bool  lastResetWasCrash();       // panic / watchdog / brownout - not power-on, flash or deep-sleep wake
const char* resetReasonText();
[[noreturn]] void deepSleepFor(uint32_t seconds);   // arms KEY0 as a wake button too
