#include "parking.h"
#include <cstdio>
#include <cstring>

namespace beach {

int weekOfMonth(int dayOfMonth) { return (dayOfMonth - 1) / 7 + 1; }

// "8AM", "8:30AM" — no space, matching formatTimeShort() in the Hubitat app.
static void timeShort(int totalMinutes, char* out, int outLen) {
  int h = (totalMinutes / 60) % 24;
  int m = totalMinutes % 60;
  const char* ampm = h >= 12 ? "PM" : "AM";
  int displayH = h % 12;
  if (displayH == 0) displayH = 12;
  if (m == 0) snprintf(out, outLen, "%d%s", displayH, ampm);
  else        snprintf(out, outLen, "%d:%02d%s", displayH, m, ampm);
}

static bool endsWith(const char* s, const char* suffix) {
  size_t ls = strlen(s), lx = strlen(suffix);
  return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

void formatParkingText(const char* side, int startMin, int endMin, char* out, int outLen) {
  char start[12], end[12];
  timeShort(startMin, start, sizeof(start));
  timeShort(endMin, end, sizeof(end));
  // Drop the start meridiem when both share it: "8-10AM" rather than "8AM-10AM".
  if ((endsWith(start, "AM") && endsWith(end, "AM")) ||
      (endsWith(start, "PM") && endsWith(end, "PM"))) {
    start[strlen(start) - 2] = '\0';
  }
  snprintf(out, outLen, "NO PARKING %s SIDE: %s-%s", side, start, end);
}

ParkingAlert evaluateParking(const ParkingRule* rules, int ruleCount, const ParkingInput& in) {
  ParkingAlert a;
  int effectiveSunset = in.sunsetMinutes > 0 ? in.sunsetMinutes : PARKING_SUNSET_FALLBACK_MIN;

  for (int i = 0; i < ruleCount; i++) {
    const ParkingRule& r = rules[i];
    if (!r.enabled) continue;
    bool todayMatches    = (in.todayDow == r.dayOfWeek) &&
                           (r.weeksMask & (1u << (weekOfMonth(in.todayDom) - 1)));
    bool tomorrowMatches = (in.tomorrowDow == r.dayOfWeek) &&
                           (r.weeksMask & (1u << (weekOfMonth(in.tomorrowDom) - 1)));

    // Case 1: today is the restriction day — active from midnight until it ends.
    // Case 2: tomorrow is the restriction day — active from sunset tonight.
    if ((todayMatches && in.nowMinutes <= r.endMin) ||
        (tomorrowMatches && in.nowMinutes >= effectiveSunset)) {
      a.active = true;
      formatParkingText(r.side, r.startMin, r.endMin, a.text, sizeof(a.text));
      return a;
    }
  }
  return a;
}

} // namespace beach
