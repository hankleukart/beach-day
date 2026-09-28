// The v3 runtime rules engine, reading shared/v3/day-outcomes.toml.
//
// The TOML IS the rules - the board parses it directly, there is no generated
// copy and no build step. Pure C++ apart from tomlc99, so the host tests load
// the very same file the device ships.
#pragma once
#include <cstddef>
#include <cstdint>

struct toml_table_t;

namespace day {

struct Inputs {
  double tempMinF = 0, tempMaxF = 0, tempSwingF = 0;
  double precipChanceMaxPct = 0, windMaxMph = 0;
  double aqiMax = 0, aqiMin = 0;
  double humidityMinPct = 0, humidityMaxPct = 0;
  double cloudCoverAvgPct = 0;
  bool   hasFirstClearHour = false;
  int    firstClearHour = -1;
  double sunRunHours = 0;
  int    sunRunStartHour = -1, sunRunEndHour = -1;
  int    rainStartHour = -1, rainEndHour = -1;
  char   conditionSummary[40] = "";
  char   sunsetLocal[12] = "";
  char   weekdayName[12] = "";
  // Calendar, for holidays and for picking a day name that is stable all day.
  int    year = 0, month = 0, dayOfMonth = 0, weekday = 0;   // weekday 0 = Sunday
  long   epochDay = 0;
};

struct Wear { char label[20] = ""; char icon[20] = ""; };
// A tile carries a primary number, a supporting one under it, and a plain
// word. Combining two related readings per tile is what lets four tiles do
// the work six were doing badly.
struct Stat {
  char id[16] = ""; char label[16] = ""; char icon[20] = "";
  char value[20] = "";   // the big one
  char sub[20] = "";     // the supporting one; empty if the stat has none
  char word[16] = "";
};

struct Outcome {
  char id[24] = "";
  char title[64] = "";          // holiday prefix + day name; the renderer wraps it
  char holiday[20] = "";        // the prefix that was applied, if any
  char tagline[32] = "";
  char heroIcon[20] = "";
  bool isBeachDay = false;
  bool heroLight = false;
  char heroColor[8] = "none";
  Wear wear[4];
  int  wearCount = 0;
  char alsoGrab[180] = "";
  char failedTest[24] = "";
  bool hasFooterSuffix = false;
  char footerSuffix[48] = "";
};

struct Screen {
  Outcome outcome;
  Stat    stats[6];
  int     statCount = 0;
  char    eyebrow[24] = "";
  char    weekday[12] = "";
  char    subline[110] = "";
  char    footer[110] = "";
  bool    tomorrow = false;
};

// Lets the firmware check icon names against the art it can actually draw.
using IconChecker = bool (*)(const char* name);
void setIconChecker(IconChecker fn);
IconChecker g_iconCheckerAccessor();   // internal: for the validator

class Spec {
 public:
  ~Spec();
  Spec() = default;
  Spec(const Spec&) = delete;
  Spec& operator=(const Spec&) = delete;

  // Parses and fully validates. A failure leaves any previous spec in place.
  bool load(const char* text, size_t len, char* err, size_t errLen);
  bool valid() const { return valid_; }
  const char* name() const;
  const char* version() const;
  const char* locationLabel() const;
  int outcomeCount() const;
  const char* outcomeId(int i) const;

  bool evaluate(const Inputs& in, Screen& out, const char* eyebrowOverride, bool tomorrow) const;

 private:
  toml_table_t* root_ = nullptr;
  char nameBuf_[48] = "", versionBuf_[16] = "", locationBuf_[24] = "";
  mutable char idBuf_[24] = "";
  bool valid_ = false;

  bool validate(toml_table_t* root, char* err, size_t errLen) const;
  void resolveStat(toml_table_t* st, const Inputs& in, Stat& out) const;
  bool holidayFor(const Inputs& in, char* out, size_t n) const;
};

// {token} interpolation. Tokens are input names (short or canonical) plus
// value, degreesShort, sunWindow, rainWindow, footerSuffix.
void interpolate(const char* tmpl, const Inputs& in, double value, int degreesShort,
                 char* out, size_t n, const char* footerSuffix = nullptr);
bool fieldValue(const Inputs& in, const char* field, double& v);

} // namespace day
