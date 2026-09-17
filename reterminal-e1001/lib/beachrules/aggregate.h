// Turns raw Open-Meteo samples into rules inputs, following rules.json
// "inputAggregation" exactly — including the asymmetries (today's precip
// from active hours, tomorrow's from the daily max; AQI on a sunrise→sunset
// hour window that is NOT the active-hours window).
//
// Pure C++ so the host tests can pin the semantics. The firmware's
// weather.cpp only parses JSON into RawForecast; everything with a rule
// in it lives here.
#pragma once
#include "beachrules.h"

namespace beach {

constexpr int RAW_MAX_HOURS = 48;   // 2 days × 24

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
  int16_t precipProbMax = 0;
  int16_t sunriseMin = 0;   // local minutes, 0 = unavailable
  int16_t sunsetMin  = 0;
};

struct RawForecast {
  RawHourly  wx[RAW_MAX_HOURS];
  int        wxCount = 0;
  RawAqiHour aqi[RAW_MAX_HOURS];
  int        aqiCount = 0;
  RawDaily   daily[2];
};

// Hubitat rounds twice: the driver to one decimal (HALF_UP on the value's
// decimal representation), then the app to an integer. 74.45 therefore
// displays as 75, not 74. Reproduced on purpose; the epsilon absorbs binary
// representation error (74.45 is really 74.4500000000000028).
int roundDisplay(double v);

// Fills out.today / out.tomorrow / sunrise / sunset. Leaves out.nowMinutes alone.
void aggregate(const RawForecast& raw, Inputs& out);

} // namespace beach
