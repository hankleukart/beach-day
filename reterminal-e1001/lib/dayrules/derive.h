// Open-Meteo hourly rows -> day::Inputs, over a day's sunrise..sunset hours
// or an evening's hours from sunset until midnight.
// Pure C++ so the host tests pin the semantics.
#pragma once
#include <cstddef>
#include <cstdint>
#include "dayspec.h"

namespace day {

// Yesterday, today and tomorrow - an evening still on screen after midnight
// (a Day Mode start later than midnight) reads yesterday's hours - plus room
// for a 25-hour DST day.
constexpr int MAX_ROWS = 76;
constexpr int FALLBACK_START_HOUR = 6, FALLBACK_END_HOUR = 20;
constexpr int CLEAR_CLOUD_MAX_PCT = 30;    // "first clear hour" = first hour at or under this
// An hour counts as wet above this. day-outcomes.json decides whether to show
// the window or the bare percentage, and its band boundary must match.
constexpr int RAIN_WINDOW_MIN_PCT = 20;

struct HourRow {
  int8_t  day, hour;    // day: -1 yesterday, 0 today, 1 tomorrow
  double  tempF;
  double  windMph;
  int16_t humidity;     // -1 missing
  int16_t precipProb;   // -1 missing
  int16_t cloud;        // -1 missing
  int16_t code;         // WMO, -1 missing
};
struct AqiRow { int8_t day, hour; int16_t aqi; };
struct DayMeta { bool valid = false; int16_t sunriseMin = 0, sunsetMin = 0; int16_t dailyCode = 99; };

// When the screen turns to each mode, as picked on the setup page. A day start
// is minutes from sunrise, or DAY_START_MIDNIGHT; an evening start is minutes
// from sunset. This moves only the switch: the hours each mode reads stay
// daySpan and eveningSpan. A Day Mode start after midnight leaves the evening
// on screen until then, still reading sunset to midnight.
//
// The value 1 meant "3:00 AM" before 0.14.0. It is no longer a choice, so a
// board that saved it falls back to the default, midnight.
constexpr int16_t DAY_START_MIDNIGHT = 2;   // not a whole-30-minute offset, so never a real choice
struct ModeOption { int16_t value; const char* label; };
constexpr ModeOption DAY_START_OPTIONS[] = {
  { DAY_START_MIDNIGHT, "Midnight" }, { 0, "Sunrise" }, { -30, "Sunrise - 30m" },
  { -60, "Sunrise - 1h" }, { -120, "Sunrise - 2h" }, { -180, "Sunrise - 3h" },
};
constexpr ModeOption EVENING_START_OPTIONS[] = {
  { 0, "Sunset" }, { -30, "Sunset - 30m" }, { 30, "Sunset + 30m" },
};
template <size_t N>
inline bool isModeOption(const ModeOption (&options)[N], int value) {
  for (const ModeOption& o : options) if (o.value == value) return true;
  return false;
}
struct ModeStarts { int16_t day = DAY_START_MIDNIGHT; int16_t evening = 0; };

// Both clamped to the date, so an offset cannot push a start past midnight.
inline int dayStartMinute(const ModeStarts& s, const DayMeta& today) {
  if (s.day == DAY_START_MIDNIGHT) return 0;
  const int m = today.sunriseMin + s.day;
  return m < 0 ? 0 : m;
}
inline int eveningStartMinute(const ModeStarts& s, const DayMeta& today) {
  const int m = today.sunsetMin + s.evening;
  return m > 1439 ? 1439 : m;
}
// Without sun times there is no evening: the screen stays on the day.
inline bool isEvening(int now, const DayMeta& today, const ModeStarts& s) {
  if (today.sunriseMin <= 0 || today.sunsetMin <= 0) return false;
  return now >= eveningStartMinute(s, today) || now < dayStartMinute(s, today);
}
// The day's outcome is decided at sunrise, not when Day Mode starts: before
// then the screen follows the forecast as it changes. Without sun times, at
// FALLBACK_START_HOUR. An evening is decided at its first look.
inline bool dayPinnable(int now, const DayMeta& today) {
  return now >= (today.sunriseMin > 0 ? today.sunriseMin : FALLBACK_START_HOUR * 60);
}

struct Raw {
  HourRow hours[MAX_ROWS]; int n = 0;
  AqiRow  aqi[MAX_ROWS];   int na = 0;
  DayMeta day[2];          // today, tomorrow
  DayMeta prev;            // yesterday: where an evening seen after midnight began
  const DayMeta& meta(int d) const { return d < 0 ? prev : day[d > 1 ? 1 : d]; }
};

// The hours the rules aggregate over: the whole hour containing sunrise
// through the whole hour containing sunset, both ends included. Falls back to
// FALLBACK_START_HOUR..FALLBACK_END_HOUR when the day has no sun times.
void daylightHours(const Raw& raw, int d, int& h0, int& h1);

// A run of hours counted from local midnight at the start of day 0, both ends
// included: 19..23 is 7 PM to midnight tonight, and -5..-1 is 7 PM to
// midnight yesterday. Shared with the readings screen, whose job is to show
// these exact rows.
struct Span { int a0 = 0, a1 = -1; };
inline int spanDay(int a)  { return a >= 0 ? a / 24 : -((23 - a) / 24); }   // floor(a / 24)
inline int spanHour(int a) { return a - spanDay(a) * 24; }                  // 0..23
Span daySpan(const Raw& raw, int d);
// A whole evening, the same at every look: the hour holding sunset through
// the 11 PM hour. After noon that is tonight's; before noon (only with a Day
// Mode start later than midnight), yesterday's.
Span eveningSpan(const Raw& raw, int nowMinute);

// weekday: 0 = Sunday. Fills every Inputs field but `evening`, which is the
// caller's to say.
void derive(const Raw& raw, int d, int weekday, Inputs& out);
void derive(const Raw& raw, const Span& span, int weekday, Inputs& out);
void formatClock12(int minutes, char* out, size_t n);      // 1148 -> "7:08 PM"
const char* weekdayName(int weekday);

} // namespace day
