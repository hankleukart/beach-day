// beachrules — the Beach Day decision logic, transcribed from ../../shared/rules.json.
//
// Pure C++: no Arduino, no heap, no I/O. The firmware feeds it aggregated
// inputs; test/test_rules feeds it fixtures generated from shared/fixtures.
// If you change a threshold here, change rules.json first and add a fixture.
#pragma once
#include <cstdint>
#include "rules_config.h"   // generated from ../../shared/rules.json by tools/gen.py

namespace beach {

// A daylight window can span at most every hour of the day.
constexpr int   MAX_DAYLIGHT_HOURS = 24;

enum class State : uint8_t {
  BeachDay, NightTime, IndoorDay, RainDay, WindDay, ChillyDay, NiceDay, GreyDay, JustADay
};

struct HourRow {
  int8_t  hour;   // local hour, inside that day's daylight window
  int16_t code;   // WMO weather code
};

// Everything the rules need about one target day, already aggregated and
// rounded to integers per rules.json "rounding" and "inputAggregation".
struct DayInputs {
  int16_t temp        = 0;   // daily high °F
  int16_t tempMin     = 0;
  int16_t precipProb  = 0;   // %
  int16_t wind        = 0;   // mph
  int16_t aqi         = 0;   // daylight-window max
  int16_t aqiMin      = 0;   // daylight-window min
  int16_t uv          = 0;
  int16_t humidityMin = 0;
  int16_t humidityMax = 0;
  int16_t weatherCode = 99;  // activeWeatherCode (mode over the daylight window)
  char    conditionText[32] = "Unknown";
  int16_t sunriseMin  = 0;   // this day's own sun, 0 = unavailable
  int16_t sunsetMin   = 0;
  HourRow hours[MAX_DAYLIGHT_HOURS] = {};
  int8_t  hourCount   = 0;
  bool    valid       = false;
};

struct Inputs {
  int16_t   nowMinutes     = 0;   // local minutes from midnight
  int16_t   sunriseMinutes = 0;   // TODAY's sun; drives the time condition. 0 = unavailable
  int16_t   sunsetMinutes  = 0;
  DayInputs today;
  DayInputs tomorrow;
};

struct SunScan {
  int8_t sunHours            = 0;
  int8_t firstSunnyHour      = -1;
  bool   firstWindowHourSunny = false;   // the first row of the window (sunrise hour) was sunny
  bool   clearingLater       = false;    // sun later but not at 9 AM
  bool   sunnyLater          = false;    // first sunny hour <= 2 PM
  bool   enoughSun           = false;    // sunHours >= MIN_SUN_HOURS
  int8_t windowHours         = 0;        // rows in the daylight window
  char   clearingTime[8]     = "";       // "11 AM", "1 PM"
};

struct Verdict {
  State state       = State::JustADay;
  bool  isNight     = false;
  bool  condTemp    = false;
  bool  condPrecip  = false;
  bool  condWind    = false;
  bool  condAqi     = false;
  bool  condForecast = false;
  bool  condTime    = false;
  SunScan sun;
  const DayInputs* shown = nullptr;  // the day whose numbers are displayed
};

Verdict evaluate(const Inputs& in);
SunScan scanSun(const DayInputs& day);

// Display strings from rules.json states.priority
const char* stateId(State s);
const char* stateTitleLine1(State s);   // "Beach"
const char* stateTitleLine2(State s);   // "Day!"
const char* stateSubtitle(State s);     // "Pack the car"

// "At 11 AM" / "All day" / "2 hrs" / "Cloudy" — the Sun row's value cell.
void sunRowText(const Verdict& v, char* out, int outLen);

// 14 -> "2 PM", 9 -> "9 AM", 0 -> "12 AM"
void formatHour12(int hour, char* out, int outLen);

} // namespace beach
