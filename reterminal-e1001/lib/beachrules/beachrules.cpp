#include "beachrules.h"
#include <cstdio>
#include <cstring>

namespace beach {

void formatHour12(int hour, char* out, int outLen) {
  const char* ampm = "AM";
  int display = hour;
  if (hour >= 12) {
    ampm = "PM";
    if (hour > 12) display = hour - 12;
  } else if (hour == 0) {
    display = 12;
  }
  snprintf(out, outLen, "%d %s", display, ampm);
}

// Walk the day's daylight-window rows in order, counting sunny hours and
// remembering the first. Rows are already filtered to the window by aggregate().
SunScan scanSun(const DayInputs& day) {
  SunScan s;
  s.windowHours = day.hourCount;
  bool firstChecked = false;
  for (int i = 0; i < day.hourCount; i++) {
    const HourRow& r = day.hours[i];
    bool sunny = r.code <= SUNNY_CODE_MAX;
    if (sunny) {
      s.sunHours++;
      if (s.firstSunnyHour == -1) s.firstSunnyHour = r.hour;
    }
    if (!firstChecked) {
      s.firstWindowHourSunny = sunny;
      firstChecked = true;
    }
  }
  if (s.sunHours > 0 && s.firstSunnyHour != -1) {
    formatHour12(s.firstSunnyHour, s.clearingTime, sizeof(s.clearingTime));
    if (!s.firstWindowHourSunny) s.clearingLater = true;
  }
  s.sunnyLater = (s.sunHours > 0 && s.firstSunnyHour <= CLEARING_DEADLINE_HOUR);
  s.enoughSun  = (s.sunHours >= MIN_SUN_HOURS);
  return s;
}

Verdict evaluate(const Inputs& in) {
  Verdict v;

  // Time condition: between sunrise and (sunset - buffer). Unknown sun -> pass.
  if (in.sunriseMinutes == 0 || in.sunsetMinutes == 0) {
    v.condTime = true;
  } else {
    int limit = in.sunsetMinutes - SUNSET_BUFFER_MIN;
    v.condTime = (in.nowMinutes >= in.sunriseMinutes && in.nowMinutes <= limit);
  }
  v.isNight = !v.condTime;

  // Night flips every displayed number and every condition to tomorrow.
  const DayInputs& d = v.isNight ? in.tomorrow : in.today;
  v.shown = &d;

  v.sun = scanSun(d);

  v.condTemp     = d.temp >= TEMP_BEACH_MIN;
  v.condPrecip   = d.precipProb < PRECIP_BEACH_MAX;
  v.condWind     = d.wind <= WIND_BEACH_MAX;
  v.condAqi      = d.aqi < AQI_BEACH_MAX;
  // Needs enough sun overall, and the sun must either dominate the day or
  // arrive by the clearing deadline.
  v.condForecast = v.sun.enoughSun &&
                   ((d.weatherCode <= SUNNY_CODE_MAX) || v.sun.sunnyLater);

  bool beach = v.condTemp && v.condPrecip && v.condWind &&
               v.condForecast && v.condTime && v.condAqi;

  // rules.json states.priority, first match wins.
  if (beach)                         v.state = State::BeachDay;
  else if (v.isNight)                v.state = State::NightTime;
  else if (!v.condAqi)               v.state = State::IndoorDay;
  else if (d.precipProb >= RAIN_DAY_PRECIP_MIN) v.state = State::RainDay;
  else if (!v.condWind)              v.state = State::WindDay;
  else if (d.temp < CHILLY_DAY_TEMP_MAX) v.state = State::ChillyDay;
  else if (d.temp >= CHILLY_DAY_TEMP_MAX && d.temp < TEMP_BEACH_MIN && v.condForecast)
                                     v.state = State::NiceDay;
  else if (!v.condForecast)          v.state = State::GreyDay;
  else                               v.state = State::JustADay;

  return v;
}

void sunRowText(const Verdict& v, char* out, int outLen) {
  if (v.sun.clearingLater && v.sun.clearingTime[0] != '\0') {
    snprintf(out, outLen, "At %s", v.sun.clearingTime);
  } else if (v.sun.windowHours > 0 && v.sun.sunHours >= v.sun.windowHours - 1) {
    // Sunny for all but at most one hour of the daylight window.
    snprintf(out, outLen, "All day");
  } else if (v.sun.sunHours > 0) {
    // Partial sun that didn't arrive later than it started: say how much there
    // is rather than overclaiming "All day".
    snprintf(out, outLen, "%d hr%s", v.sun.sunHours, v.sun.sunHours == 1 ? "" : "s");
  } else {
    snprintf(out, outLen, "Cloudy");
  }
}

const char* stateId(State s)         { return STATE_TEXT[static_cast<int>(s)].id; }
const char* stateTitleLine1(State s) { return STATE_TEXT[static_cast<int>(s)].line1; }
const char* stateTitleLine2(State s) { return STATE_TEXT[static_cast<int>(s)].line2; }
const char* stateSubtitle(State s)   { return STATE_TEXT[static_cast<int>(s)].subtitle; }

} // namespace beach
