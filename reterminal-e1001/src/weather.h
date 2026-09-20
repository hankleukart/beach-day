// Open-Meteo fetch + JSON -> day::Raw. No rules live here; see lib/dayrules.
#pragma once
#include <ctime>
#include "derive.h"
#include "settings.h"

struct Fetched {
  day::Raw raw;
  int32_t utcOffsetSec = 0;    // location's offset, from the API (handles DST for us)
  time_t  modelNowUtc  = 0;    // Open-Meteo "current.time" - clock fallback if NTP failed
  time_t  todayStartUtc = 0;   // hourly[0] = local midnight today in the location's zone
  bool    aqiOk = false;
};

bool fetchWeather(const Settings& s, Fetched& out, char* err, size_t errLen);

long localDays(time_t utc, int32_t off);
int  localMinuteOfDay(time_t utc, int32_t off);
int  localHour(time_t utc, int32_t off);
