// Turns raw Open-Meteo samples into rules inputs, following rules.json
// "inputAggregation".
//
// One window, used by every time-narrowed aggregate: that day's own sunrise
// hour through that day's own sunset hour, inclusive. Today and tomorrow are
// treated identically, each with its own sun times.
//
// Pure C++ so the host tests can pin the semantics. The firmware's
// weather.cpp only parses JSON into RawForecast; everything with a rule
// in it lives here.
#pragma once
#include "beachrules.h"

namespace beach {

constexpr int RAW_MAX_HOURS = 48;   // 2 days x 24

struct RawHourly {
  int8_t  day;         // 0 = today, 1 = tomorrow (local date)
  int8_t  hour;        // local hour 0..23
  int16_t code;        // WMO code, -1 = missing
  int16_t precipProb;  // %, -1 = missing
  int16_t humidity;    // %, -1 = missing
};

struct RawAqiHour {
  int8_t  day;
  int8_t  hour;
  int16_t aqi;         // us_aqi, -1 = missing
};

struct RawDaily {
  bool    valid = false;
  double  tempMax = 0, tempMin = 0, windMax = 0, uvMax = 0;
  int16_t code = 99;
  int16_t precipProbMax = 0;   // API daily max; only a fallback now
  int16_t sunriseMin = 0;      // local minutes, 0 = unavailable
  int16_t sunsetMin  = 0;
};

struct RawForecast {
  RawHourly  wx[RAW_MAX_HOURS];
  int        wxCount = 0;
  RawAqiHour aqi[RAW_MAX_HOURS];
  int        aqiCount = 0;
  RawDaily   daily[2];
};

// Single half-up round straight to an integer (rules.json "rounding").
int roundDisplay(double v);

// The daylight window for one day, as inclusive integer hours.
struct Window { int startHour; int endHour; };
Window daylightWindow(const RawDaily& d);

// Fills out.today / out.tomorrow / sunrise / sunset. Leaves out.nowMinutes alone.
void aggregate(const RawForecast& raw, Inputs& out);

} // namespace beach
