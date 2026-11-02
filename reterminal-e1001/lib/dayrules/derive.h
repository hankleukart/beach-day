// Open-Meteo hourly rows -> day::Inputs, over a day's sunrise..sunset hours
// or a night's hours from nightfall until sunrise.
// Pure C++ so the host tests pin the semantics.
#pragma once
#include <cstdint>
#include "dayspec.h"

namespace day {

// Yesterday, today and tomorrow - a night that began yesterday evening still
// needs yesterday's evening hours at 2 AM - plus room for a 25-hour DST day.
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

struct Raw {
  HourRow hours[MAX_ROWS]; int n = 0;
  AqiRow  aqi[MAX_ROWS];   int na = 0;
  DayMeta day[2];          // today, tomorrow
  DayMeta prev;            // yesterday: where a night seen before dawn began
  const DayMeta& meta(int d) const { return d < 0 ? prev : day[d > 1 ? 1 : d]; }
};

// The hours the rules aggregate over: the whole hour containing sunrise
// through the whole hour containing sunset, both ends included. Falls back to
// FALLBACK_START_HOUR..FALLBACK_END_HOUR when the day has no sun times.
void daylightHours(const Raw& raw, int d, int& h0, int& h1);

// A run of hours counted from local midnight at the start of day 0, both ends
// included, so a night can cross midnight: 19..30 is 7 PM to 6 AM tomorrow,
// and -5..6 is 7 PM yesterday to 6 AM today. Shared with the readings screen,
// whose job is to show these exact rows.
struct Span { int a0 = 0, a1 = -1; };
inline int spanDay(int a)  { return a >= 0 ? a / 24 : -((23 - a) / 24); }   // floor(a / 24)
inline int spanHour(int a) { return a - spanDay(a) * 24; }                  // 0..23
Span daySpan(const Raw& raw, int d);
// A whole night, the same at every look: the hour holding nightfall (half an
// hour after sunset) through the hour holding the next sunrise. After noon
// that is tonight's; before noon, the one that began yesterday evening.
Span nightSpan(const Raw& raw, int nowMinute);

// weekday: 0 = Sunday. Fills every Inputs field but `night`, which is the
// caller's to say.
void derive(const Raw& raw, int d, int weekday, Inputs& out);
void derive(const Raw& raw, const Span& span, int weekday, Inputs& out);
void formatClock12(int minutes, char* out, size_t n);      // 1148 -> "7:08 PM"
const char* weekdayName(int weekday);

} // namespace day
