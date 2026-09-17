#pragma once
#include <cstdint>

float batteryVolts();
int   batteryPercent(float volts);          // rough single-cell LiPo estimate, 0..100
bool  wokeByButton();
[[noreturn]] void deepSleepFor(uint32_t seconds);   // arms KEY0 as a wake button too
