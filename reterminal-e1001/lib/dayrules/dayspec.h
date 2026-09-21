// The v3 runtime rules engine. Loads the day-outcomes JSON at run time and
// turns a day's numbers into everything the screen shows: one outcome (title,
// tagline, hero icon, four wear tiles, the "also grab" line), five stats
// (icon, label, value, plain word) and the composed header/footer strings.
//
// Pure C++ + ArduinoJson; no Arduino dependency, so the host tests load the
// very same JSON file the device ships.
#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

namespace day {

struct Inputs {
  double tempMinF = 0, tempMaxF = 0, tempSwingF = 0;
  double precipChanceMaxPct = 0, windMaxMph = 0;
  double aqiMax = 0, aqiMin = 0;
  double humidityMinPct = 0, humidityMaxPct = 0;
  double cloudCoverAvgPct = 0;
  bool   hasFirstClearHour = false;
  int    firstClearHour = -1;           // local hour 0..23
  // Longest unbroken run of clear hours in the daylight window. The SUN stat
  // shows this as an interval ("1P - 4P") rather than a single hour, because
  // "sunny from 1 to 4" is the thing you actually plan around.
  double sunRunHours = 0;               // 0 when nothing is clear
  int    sunRunStartHour = -1, sunRunEndHour = -1;
  char   conditionSummary[40] = "";
  char   sunsetLocal[12] = "";          // "7:08 PM"
  char   weekdayName[12] = "";
};

struct Wear  { char label[20] = ""; char icon[20] = ""; };
struct Stat  { char id[16] = ""; char label[16] = ""; char icon[20] = ""; char value[20] = ""; char word[16] = ""; };

struct Outcome {
  char id[24] = "";
  char title[3][14] = { "", "", "" };
  int  titleLines = 0;
  char tagline[32] = "";
  char heroIcon[20] = "";
  bool isBeachDay = false;
  bool heroLight = false;      // black type on white, instead of white on black
  Wear wear[4];
  int  wearCount = 0;
  char alsoGrab[180] = "";
  char footerSuffix[48] = "";
  bool hasFooterSuffix = false;
  char failedTest[24] = "";             // beach_day test that failed, if any (for the log)
};

// Everything the renderer needs. Plain data so it can live in RTC memory.
struct Screen {
  Outcome outcome;
  Stat    stats[6];
  int     statCount = 0;
  char    eyebrow[24] = "";
  char    weekday[12] = "";
  char    corner1[40] = "";             // "Humidity 49–83%"
  char    corner2[40] = "";             // "Air quality 58–61"
  char    subline[110] = "";
  char    wearHeading[16] = "";
  char    footer[110] = "";
  bool    tomorrow = false;
};

class Spec {
 public:
  // Parses and sanity-checks. On failure the previous spec (if any) is kept.
  bool load(const char* json, size_t len, char* err, size_t errLen);
  bool valid() const { return valid_; }
  const char* name() const;
  const char* version() const;
  const char* locationLabel() const;
  int outcomeCount() const;
  const char* outcomeId(int i) const;

  // eyebrowOverride: the per-install label (portal / config); nullptr = spec's.
  // tomorrow: adjusts copy ("this morning" -> "in the morning").
  bool evaluate(const Inputs& in, Screen& out, const char* eyebrowOverride, bool tomorrow) const;

 private:
  JsonDocument doc_;
  bool valid_ = false;
  bool cond(JsonObjectConst c, const Inputs& in) const;
  bool validate(JsonObjectConst root, char* err, size_t errLen) const;
  bool when(JsonObjectConst w, const Inputs& in, char* failed, size_t failedLen) const;
  double beachTempThreshold() const;
  void resolveAlsoGrab(JsonVariantConst v, const Inputs& in, const char* failedTest, char* out, size_t n) const;
  void resolveStat(JsonObjectConst st, const Inputs& in, Stat& out) const;
};

// Lets the firmware also check names against the icons it can actually draw;
// without one, icon names are only checked against the spec's own list.
using IconChecker = bool (*)(const char* name);
void setIconChecker(IconChecker fn);

// {token} interpolation over the inputs plus value/degreesShort/firstClearHour.
void interpolate(const char* tmpl, const Inputs& in, double value, int degreesShort, char* out, size_t n);
bool fieldValue(const Inputs& in, const char* field, double& v);

} // namespace day
