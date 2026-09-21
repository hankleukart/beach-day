// Open-Meteo hourly rows -> day::Inputs, over that day's sunrise..sunset hours.
// Pure C++ so the host tests pin the semantics.
#pragma once
#include <cstdint>
#include "dayspec.h"

namespace day {

constexpr int MAX_ROWS = 48;
constexpr int FALLBACK_START_HOUR = 6, FALLBACK_END_HOUR = 20;
constexpr int CLEAR_CLOUD_MAX_PCT = 30;    // "first clear hour" = first hour at or under this
// An hour counts as wet above this. day-outcomes.json decides whether to show
// the window or the bare percentage, and its band boundary must match.
constexpr int RAIN_WINDOW_MIN_PCT = 20;

struct HourRow {
  int8_t  day, hour;
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
  DayMeta day[2];
};

// weekday: 0 = Sunday. Fills every Inputs field.
void derive(const Raw& raw, int d, int weekday, Inputs& out);
void formatClock12(int minutes, char* out, size_t n);      // 1148 -> "7:08 PM"
const char* weekdayName(int weekday);

} // namespace day
