// Open-Meteo fetch + JSON -> RawForecast. No rules live here; see lib/beachrules.
#pragma once
#include <ctime>
#include "aggregate.h"
#include "settings.h"

struct Fetched {
  beach::RawForecast raw;
  int32_t utcOffsetSec = 0;    // location's offset, from the API (handles DST for us)
  time_t  modelNowUtc  = 0;    // Open-Meteo "current.time" — clock fallback if NTP failed
  time_t  todayStartUtc = 0;   // hourly[0] = local midnight today in the location's zone
  bool    aqiOk = false;       // air-quality request succeeded (forecast alone is still usable)
};

bool fetchWeather(const Settings& s, Fetched& out, char* err, size_t errLen);

// Local-time helpers shared with main (all take UTC epoch + offset seconds)
long localDays(time_t utc, int32_t off);          // days since 1970-01-01 in local time
int  localMinuteOfDay(time_t utc, int32_t off);
int  localHour(time_t utc, int32_t off);
