// Street-cleaning parking alerts (rules.json "parkingAlert"), ported from
// evaluateParkingAlert() in the Hubitat integrator app.
#pragma once
#include <cstdint>
#include <initializer_list>
#include "rules_config.h"

namespace beach {

struct ParkingRule {
  bool        enabled;
  const char* side;        // "LEFT" / "RIGHT" — appears in the alert text
  uint8_t     weeksMask;   // bit (n-1) set => nth week of month, e.g. 0b00101 = weeks 1 and 3
  int8_t      dayOfWeek;   // 0 = Sunday ... 6 = Saturday
  int16_t     startMin;    // restriction start, minutes from midnight (display only)
  int16_t     endMin;      // restriction end — alert clears after this
};

struct ParkingInput {
  int16_t nowMinutes;
  int8_t  todayDow;        // 0 = Sunday
  int8_t  todayDom;        // 1..31
  int8_t  tomorrowDow;
  int8_t  tomorrowDom;
  int16_t sunsetMinutes;   // 0 = unavailable
};

struct ParkingAlert {
  bool active = false;
  char text[40] = "";
};

constexpr uint8_t weeks(std::initializer_list<int> ws) {
  uint8_t m = 0;
  for (int w : ws) if (w >= 1 && w <= 5) m |= (1u << (w - 1));
  return m;
}

int weekOfMonth(int dayOfMonth);                       // floor((dom-1)/7)+1
void formatParkingText(const char* side, int startMin, int endMin, char* out, int outLen);
ParkingAlert evaluateParking(const ParkingRule* rules, int ruleCount, const ParkingInput& in);

} // namespace beach
