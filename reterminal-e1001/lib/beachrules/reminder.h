// A weekly reminder for the footer bar - trash night by default. It belongs to
// one stretch of the screen: a weekday's night or its day, whenever those start
// (sunset and 3 AM by default). Wednesday night still means Wednesday night at
// 2 AM Thursday, as the screen says.
#pragma once
#include <cstdint>
#include "parking.h"

namespace beach {

struct Reminder {
  bool   enabled = false;
  int8_t dayOfWeek = 3;        // 0 = Sunday ... 6 = Saturday
  bool   atNight = true;       // that weekday's night, or false for its day
  char   text[40] = "";        // fits the bar's own text buffer
};

// `periodDow` is the weekday the screen is showing - "Wednesday" or
// "Wednesday night" - and `night` which of the two it is.
inline bool reminderActive(const Reminder& r, int periodDow, bool night) {
  return r.enabled && r.text[0] && r.dayOfWeek == periodDow && r.atNight == night;
}

// The footer bar is one slot. Street cleaning has a morning deadline, so it
// goes first; the reminder takes the bar once that is done. Null: no bar.
inline const char* barText(const ParkingAlert& parking, const Reminder& r, int periodDow, bool night) {
  if (parking.active) return parking.text;
  if (reminderActive(r, periodDow, night)) return r.text;
  return nullptr;
}

} // namespace beach
