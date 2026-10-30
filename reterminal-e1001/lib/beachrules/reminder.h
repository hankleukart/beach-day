// A weekly reminder for the footer bar - trash night by default. Shown all day
// on its day, by the clock rather than the forecast, so it stays up through
// the evening even after the screen has moved on to planning tomorrow.
#pragma once
#include <cstdint>
#include "parking.h"

namespace beach {

struct Reminder {
  bool   enabled = false;
  int8_t dayOfWeek = 3;        // 0 = Sunday ... 6 = Saturday
  char   text[40] = "";        // fits the bar's own text buffer
};

inline bool reminderActive(const Reminder& r, int todayDow) {
  return r.enabled && r.text[0] && r.dayOfWeek == todayDow;
}

// The footer bar is one slot. Street cleaning has a morning deadline, so it
// goes first; the reminder takes the bar once that is done. Null: no bar.
inline const char* barText(const ParkingAlert& parking, const Reminder& r, int todayDow) {
  if (parking.active) return parking.text;
  if (reminderActive(r, todayDow)) return r.text;
  return nullptr;
}

} // namespace beach
