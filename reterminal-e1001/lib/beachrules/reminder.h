// A weekly reminder for the footer bar - trash night by default. It belongs to
// one stretch of the screen: a weekday's evening or its day, whenever those
// start (sunset and midnight by default). With a Day Mode start later than
// midnight, Wednesday evening still means Wednesday evening at 2 AM Thursday,
// as the screen says.
#pragma once
#include <cstdint>
#include "parking.h"

namespace beach {

struct Reminder {
  bool   enabled = false;
  int8_t dayOfWeek = 3;        // 0 = Sunday ... 6 = Saturday
  bool   atEvening = true;     // that weekday's evening, or false for its day
  char   text[40] = "";        // fits the bar's own text buffer
};

// `periodDow` is the weekday the screen is showing - "Wednesday" or
// "Wednesday evening" - and `evening` which of the two it is.
inline bool reminderActive(const Reminder& r, int periodDow, bool evening) {
  return r.enabled && r.text[0] && r.dayOfWeek == periodDow && r.atEvening == evening;
}

// The footer bar is one slot. Street cleaning has a morning deadline, so it
// goes first; the reminder takes the bar once that is done. Null: no bar.
inline const char* barText(const ParkingAlert& parking, const Reminder& r, int periodDow, bool evening) {
  if (parking.active) return parking.text;
  if (reminderActive(r, periodDow, evening)) return r.text;
  return nullptr;
}

} // namespace beach
