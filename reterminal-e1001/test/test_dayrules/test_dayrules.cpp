// Host tests for the v3 engine. Loads the SAME JSON the device ships.
//   pio test -e native
#include <unity.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include "dayspec.h"
#include "derive.h"

#ifndef DAYRULES_SPEC_PATH
#define DAYRULES_SPEC_PATH "../shared/v3/day-outcomes.json"
#endif

static day::Spec spec;
static std::string specText;

void setUp() {}
void tearDown() {}

static day::Inputs base() {
  day::Inputs in;
  in.tempMinF = 62; in.tempMaxF = 84; in.tempSwingF = 22 - 1;  // 21: below the layers threshold
  in.precipChanceMaxPct = 5; in.windMaxMph = 8; in.aqiMax = 40; in.aqiMin = 20;
  in.humidityMinPct = 49; in.humidityMaxPct = 83; in.cloudCoverAvgPct = 10;
  in.hasFirstClearHour = true; in.firstClearHour = 7;
  snprintf(in.conditionSummary, sizeof(in.conditionSummary), "Clear sky");
  snprintf(in.sunsetLocal, sizeof(in.sunsetLocal), "7:08 PM");
  snprintf(in.weekdayName, sizeof(in.weekdayName), "Wednesday");
  return in;
}

static const day::Screen& run(const day::Inputs& in, bool tomorrow = false, const char* eyebrow = nullptr) {
  static day::Screen s;
  TEST_ASSERT_TRUE_MESSAGE(spec.evaluate(in, s, eyebrow, tomorrow), "evaluate failed");
  return s;
}

void test_spec_loads() {
  char err[96];
  TEST_ASSERT_TRUE_MESSAGE(spec.load(specText.c_str(), specText.size(), err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_INT(7, spec.outcomeCount());
  TEST_ASSERT_EQUAL_STRING("jacket_day", spec.outcomeId(6));
}

// Builds a minimal-but-valid spec, then lets a test break one thing in it.
static std::string specWith(const std::string& find, const std::string& replace) {
  std::string base = R"({
    "icons": ["tshirt","sun"],
    "outcomes": [
      { "id": "only", "title": ["ONLY"], "tagline": "T", "heroIcon": "tshirt",
        "when": { "all": [] },
        "wear": [ {"label":"A","icon":"tshirt"},{"label":"B","icon":"tshirt"},{"label":"C","icon":"tshirt"},{"label":"D","icon":"tshirt"} ],
        "alsoGrab": "grab" }
    ],
    "stats": [
      { "id": "s1", "label": "S", "icon": "sun",
        "value": { "field": "tempMaxF", "format": "{value}" },
        "word": { "field": "tempMaxF", "bands": [ { "max": 200, "text": "w" } ] } }
    ]
  })";
  if (!find.empty()) {
    size_t at = base.find(find);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, "fixture anchor missing");
    base.replace(at, find.size(), replace);
  }
  return base;
}

static void rejects(const char* why, const std::string& json) {
  day::Spec s; char err[128] = "";
  bool loaded = s.load(json.c_str(), json.size(), err, sizeof(err));
  if (loaded) { TEST_FAIL_MESSAGE(why); }
  TEST_ASSERT_FALSE(s.valid());
  printf("      rejected (%s): %s\n", why, err);
}

void test_rejects_bad_specs() {
  day::Spec s; char err[128];
  TEST_ASSERT_FALSE(s.load("{ nope", 6, err, sizeof(err)));
  // the fixture itself must be good, or every case below passes for free
  TEST_ASSERT_TRUE_MESSAGE(s.load(specWith("", "").c_str(), specWith("", "").size(), err, sizeof(err)), err);

  rejects("unknown field in a condition",
          specWith(R"("when": { "all": [] })", R"("when": { "all": [{"field":"tempMaxx","op":">=","value":1}] })"));
  rejects("unknown operator",
          specWith(R"("when": { "all": [] })", R"("when": { "all": [{"field":"tempMaxF","op":"=>","value":1}] })"));
  rejects("condition with no numeric value",
          specWith(R"("when": { "all": [] })", R"("when": { "all": [{"field":"tempMaxF","op":">="}] })"));
  rejects("hero icon not in icons[]",     specWith(R"("heroIcon": "tshirt")", R"("heroIcon": "sombrero")"));
  rejects("wear icon not in icons[]",     specWith(R"({"label":"A","icon":"tshirt"},)", R"({"label":"A","icon":"kilt"},)"));
  rejects("last outcome not a catch-all", specWith(R"("when": { "all": [] })", R"("when": { "all": [{"field":"tempMaxF","op":">=","value":99}] })"));
  rejects("only three wear items",        specWith(R"(,{"label":"D","icon":"tshirt"} ])", R"( ])"));
  rejects("title line too long for the buffer",
          specWith(R"("title": ["ONLY"])", R"("title": ["ABSOLUTELY ENORMOUS"])"));
  rejects("heroPanel that is neither light nor dark",
          specWith(R"("heroIcon": "tshirt")", R"("heroIcon": "tshirt", "heroPanel": "beige")"));
  rejects("too many title lines",
          specWith(R"("title": ["ONLY"])", R"("title": ["A","B","C","D"])"));
  rejects("byFailedTest keyed on a non-field",
          specWith(R"("alsoGrab": "grab")", R"("alsoGrab": { "byFailedTest": { "windy": "x" }, "default": "y" })"));
  rejects("alsoGrab object with no default",
          specWith(R"("alsoGrab": "grab")", R"("alsoGrab": { "byFailedTest": { "windMaxMph": "x" } })"));
  rejects("stat value field unknown",
          specWith(R"("value": { "field": "tempMaxF", "format": "{value}" })", R"("value": { "field": "nope", "format": "{value}" })"));
  rejects("stat value with both format and bands",
          specWith(R"("value": { "field": "tempMaxF", "format": "{value}" })",
                   R"("value": { "field": "tempMaxF", "format": "{value}", "bands": [{"max":1,"text":"a"}] })"));
  rejects("bands that go backwards",
          specWith(R"("bands": [ { "max": 200, "text": "w" } ])", R"("bands": [ { "max": 50, "text": "a" }, { "max": 10, "text": "b" } ])"));
  rejects("last band gated on an optional input",
          specWith(R"("bands": [ { "max": 200, "text": "w" } ])",
                   R"("bands": [ { "max": 200, "text": "w", "requires": "firstClearHour" } ])"));
  rejects("band requiring an unknown field",
          specWith(R"("bands": [ { "max": 200, "text": "w" } ])",
                   R"("bands": [ { "max": 10, "text": "a", "requires": "nope" }, { "max": 200, "text": "w" } ])"));
}

void test_rejects_duplicate_ids() {
  // two outcomes sharing an id, the second still a catch-all
  std::string j = R"({
    "icons": ["tshirt"],
    "outcomes": [
      { "id": "same", "title": ["A"], "tagline": "T", "heroIcon": "tshirt", "when": { "all": [] },
        "wear": [ {"label":"A","icon":"tshirt"},{"label":"A","icon":"tshirt"},{"label":"A","icon":"tshirt"},{"label":"A","icon":"tshirt"} ],
        "alsoGrab": "x" },
      { "id": "same", "title": ["B"], "tagline": "T", "heroIcon": "tshirt", "when": { "all": [] },
        "wear": [ {"label":"A","icon":"tshirt"},{"label":"A","icon":"tshirt"},{"label":"A","icon":"tshirt"},{"label":"A","icon":"tshirt"} ],
        "alsoGrab": "x" }
    ],
    "stats": [ { "id": "s", "label": "S", "icon": "tshirt",
                 "value": { "field": "tempMaxF", "format": "{value}" },
                 "word": { "field": "tempMaxF", "bands": [ { "max": 200, "text": "w" } ] } } ] })";
  rejects("duplicate outcome id", j);
}

void test_icon_checker_catches_missing_art() {
  // The spec lists an icon the renderer has no art for.
  day::setIconChecker([](const char* n) { return strcmp(n, "tshirt") == 0; });
  rejects("icon listed but no art compiled in", specWith(R"("heroIcon": "tshirt")", R"("heroIcon": "sun")"));
  day::setIconChecker(nullptr);
  // ...and with no checker it is accepted again, since icons[] does list it.
  day::Spec s; char err[128];
  std::string j = specWith(R"("heroIcon": "tshirt")", R"("heroIcon": "sun")");
  TEST_ASSERT_TRUE_MESSAGE(s.load(j.c_str(), j.size(), err, sizeof(err)), err);
}

void test_beach_day() {
  const auto& s = run(base());
  TEST_ASSERT_EQUAL_STRING("beach_day", s.outcome.id);
  TEST_ASSERT_EQUAL_INT(2, s.outcome.titleLines);
  TEST_ASSERT_EQUAL_STRING("BEACH", s.outcome.title[0]);
  TEST_ASSERT_EQUAL_STRING("DAY!", s.outcome.title[1]);
  TEST_ASSERT_EQUAL_STRING("PACK THE CAR", s.outcome.tagline);
  TEST_ASSERT_EQUAL_STRING("sun_and_waves", s.outcome.heroIcon);
  TEST_ASSERT_TRUE_MESSAGE(s.outcome.heroLight, "beach day gets the bright panel");
  TEST_ASSERT_EQUAL_INT(4, s.outcome.wearCount);
  TEST_ASSERT_EQUAL_STRING("Swimsuit", s.outcome.wear[0].label);
  TEST_ASSERT_EQUAL_STRING("swim_trunks", s.outcome.wear[0].icon);
  TEST_ASSERT_EQUAL_STRING("Also grab: water bottles and something for shade.", s.outcome.alsoGrab);
  TEST_ASSERT_EQUAL_STRING("Sun goes down at 7:08 PM \xE2\x80\x94 home before dark!", s.footer);
  TEST_ASSERT_EQUAL_STRING("VENICE BEACH", s.eyebrow);
  TEST_ASSERT_EQUAL_STRING("Wednesday", s.weekday);
  TEST_ASSERT_EQUAL_STRING("Humidity 49\xE2\x80\x93" "83%", s.corner1);
  TEST_ASSERT_EQUAL_STRING("Air quality 20\xE2\x80\x93" "40", s.corner2);
  TEST_ASSERT_EQUAL_STRING("Clear sky \xC2\xB7 62\xC2\xB0 this morning, 84\xC2\xB0 this afternoon", s.subline);
}

void test_rain_beats_everything() {
  auto in = base(); in.precipChanceMaxPct = 50;           // hot AND wet
  TEST_ASSERT_EQUAL_STRING("rain_boots_day", run(in).outcome.id);
  in.precipChanceMaxPct = 49; in.tempMaxF = 84; in.cloudCoverAvgPct = 10;
  TEST_ASSERT_EQUAL_STRING("sun_hat_day", run(in).outcome.id);   // 49% fails beach's <=20 but not rain's >=50
}

void test_cold_beats_hot_paths() {
  auto in = base(); in.tempMaxF = 51; in.tempMinF = 40; in.tempSwingF = 11;
  TEST_ASSERT_EQUAL_STRING("big_coat_day", run(in).outcome.id);
  in.tempMaxF = 52;
  TEST_ASSERT_EQUAL_STRING("jacket_day", run(in).outcome.id);    // 52 is not < 52; not hot, not swingy
}

void test_sun_hat_explains_which_test_failed() {
  auto in = base(); in.windMaxMph = 16;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("sun_hat_day", s.outcome.id);
  TEST_ASSERT_EQUAL_STRING("windMaxMph", s.outcome.failedTest);
  TEST_ASSERT_EQUAL_STRING("Too windy at the water today \xE2\x80\x94 the shady park is the better pick.", s.outcome.alsoGrab);

  in = base(); in.aqiMax = 100;
  TEST_ASSERT_EQUAL_STRING("The air is hazy today \xE2\x80\x94 keep it short outside and shady.", run(in).outcome.alsoGrab);
  in = base(); in.cloudCoverAvgPct = 36;
  TEST_ASSERT_EQUAL_STRING("Grey overhead but still hot \xE2\x80\x94 sunscreen anyway, it burns through.", run(in).outcome.alsoGrab);
  in = base(); in.precipChanceMaxPct = 21;
  TEST_ASSERT_EQUAL_STRING("Hot with a shower possible \xE2\x80\x94 shade now, umbrella nearby.", run(in).outcome.alsoGrab);
  // two failures: the first in beach_day's list wins (precip is listed before wind)
  in = base(); in.windMaxMph = 30; in.precipChanceMaxPct = 30;
  TEST_ASSERT_EQUAL_STRING("Hot with a shower possible \xE2\x80\x94 shade now, umbrella nearby.", run(in).outcome.alsoGrab);
}

void test_layers_day() {
  auto in = base(); in.tempMaxF = 76; in.tempMinF = 52; in.tempSwingF = 24;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("layers_day", s.outcome.id);
  TEST_ASSERT_EQUAL_STRING("It warms up 24 degrees today \xE2\x80\x94 pack somewhere to stash the hoodie.", s.outcome.alsoGrab);
  // "swingy" overrides the WARMEST word
  bool found = false;
  for (int i = 0; i < s.statCount; i++) if (!strcmp(s.stats[i].id, "warmest")) { found = true; TEST_ASSERT_EQUAL_STRING("swingy", s.stats[i].word); }
  TEST_ASSERT_TRUE(found);
  // hot + swingy: outcome order puts the hot outcomes first, so layers never wins a hot day
  in.tempMaxF = 80; in.tempMinF = 56;   // swing 24, but 80 and clear is a beach day
  TEST_ASSERT_EQUAL_STRING("beach_day", run(in).outcome.id);
  in.cloudCoverAvgPct = 60;              // still hot, still swingy: sun hat, not layers
  TEST_ASSERT_EQUAL_STRING("sun_hat_day", run(in).outcome.id);
}

void test_tshirt_near_threshold_copy() {
  auto in = base(); in.tempMaxF = 74; in.tempMinF = 60; in.tempSwingF = 14;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("tshirt_day", s.outcome.id);
  TEST_ASSERT_EQUAL_STRING("4 degrees short of a beach day \xE2\x80\x94 lovely for the park, chilly in the water.", s.outcome.alsoGrab);
  in.tempMaxF = 71;   // 7 short: outside the 6-degree window
  TEST_ASSERT_EQUAL_STRING("Also grab: a light sweater for after dinner.", run(in).outcome.alsoGrab);
  in.tempMaxF = 67;
  TEST_ASSERT_EQUAL_STRING("jacket_day", run(in).outcome.id);
}

void test_jacket_footer_has_no_suffix() {
  auto in = base(); in.tempMaxF = 60; in.tempMinF = 50; in.tempSwingF = 10;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("jacket_day", s.outcome.id);
  TEST_ASSERT_FALSE_MESSAGE(s.outcome.heroLight, "everything else stays dark");
  TEST_ASSERT_FALSE(s.outcome.hasFooterSuffix);
  TEST_ASSERT_EQUAL_STRING("Sun goes down at 7:08 PM.", s.footer);
}

void test_stats() {
  auto s = run(base());
  TEST_ASSERT_EQUAL_INT(5, s.statCount);
  TEST_ASSERT_EQUAL_STRING("SUN", s.stats[0].label);
  TEST_ASSERT_EQUAL_STRING("sun", s.stats[0].icon);
  TEST_ASSERT_EQUAL_STRING("84\xC2\xB0" "F", s.stats[1].value);
  TEST_ASSERT_EQUAL_STRING("warm", s.stats[1].word);
  TEST_ASSERT_EQUAL_STRING("5%", s.stats[2].value);
  TEST_ASSERT_EQUAL_STRING("dry", s.stats[2].word);
  TEST_ASSERT_EQUAL_STRING("8 mph", s.stats[3].value);
  TEST_ASSERT_EQUAL_STRING("calm", s.stats[3].word);
  TEST_ASSERT_EQUAL_STRING("HIGH TEMP", s.stats[1].label);
  TEST_ASSERT_EQUAL_STRING("WIND", s.stats[3].label);
  // AIR QUALITY shows the AQI itself; the word carries the meaning.
  TEST_ASSERT_EQUAL_STRING("AIR", s.stats[4].label);
  TEST_ASSERT_EQUAL_STRING("40 aqi", s.stats[4].value);
  TEST_ASSERT_EQUAL_STRING("clean", s.stats[4].word);
}

void test_sun_interval() {
  auto in = base();
  in.sunRunHours = 4; in.sunRunStartHour = 13; in.sunRunEndHour = 16;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("1P-4P", s.stats[0].value);
  TEST_ASSERT_EQUAL_STRING("a stretch", s.stats[0].word);   // 3-4 hours
  in.sunRunHours = 6; in.sunRunStartHour = 11; in.sunRunEndHour = 16;
  TEST_ASSERT_EQUAL_STRING("clear", run(in).stats[0].word); // 5+ hours

  in.sunRunHours = 3; in.sunRunStartHour = 9; in.sunRunEndHour = 11;
  TEST_ASSERT_EQUAL_STRING("9A-11A", run(in).stats[0].value);
  in.sunRunHours = 3;
  in.sunRunHours = 3; in.sunRunStartHour = 11; in.sunRunEndHour = 13;
  TEST_ASSERT_EQUAL_STRING("11A-1P", run(in).stats[0].value);
  TEST_ASSERT_EQUAL_STRING("a stretch", run(in).stats[0].word);

  // Under three hours is not worth an interval.
  in.sunRunHours = 2; in.sunRunStartHour = 13; in.sunRunEndHour = 14;
  TEST_ASSERT_EQUAL_STRING("Brief", run(in).stats[0].value);
  in.sunRunHours = 0; in.sunRunStartHour = -1; in.sunRunEndHour = -1;
  TEST_ASSERT_EQUAL_STRING("None", run(in).stats[0].value);
  TEST_ASSERT_EQUAL_STRING("none", run(in).stats[0].word);

  // The icon still tracks cloud cover.
  in.cloudCoverAvgPct = 90;
  TEST_ASSERT_EQUAL_STRING("rain_cloud", run(in).stats[0].icon);
}

void test_tomorrow_and_eyebrow_override() {
  auto s = run(base(), true, "Santa Monica");
  TEST_ASSERT_EQUAL_STRING("Santa Monica", s.eyebrow);
  TEST_ASSERT_EQUAL_STRING("Clear sky \xC2\xB7 62\xC2\xB0 in the morning, 84\xC2\xB0 in the afternoon", s.subline);
  TEST_ASSERT_TRUE(s.tomorrow);
}

void test_derive() {
  day::Raw raw;
  raw.day[0].valid = true; raw.day[0].sunriseMin = 6 * 60 + 38; raw.day[0].sunsetMin = 19 * 60 + 8; raw.day[0].dailyCode = 3;
  // window is hours 6..19
  int n = 0;
  auto add = [&](int h, double t, double w, int hum, int pp, int cloud, int code) {
    raw.hours[n++] = day::HourRow{ 0, (int8_t)h, t, w, (int16_t)hum, (int16_t)pp, (int16_t)cloud, (int16_t)code };
  };
  add(3, 50, 30, 99, 90, 100, 61);     // outside window: must not count
  add(6, 62.4, 3, 83, 0, 60, 3);
  add(9, 70, 6, 70, 2, 40, 2);
  add(11, 80, 8.6, 60, 2, 25, 1);      // first hour at or under 30% cloud
  add(14, 89.4, 12, 49, 0, 10, 0);
  add(19, 78, 9, 55, 1, 20, 0);
  add(21, 60, 20, 90, 60, 90, 61);     // outside window
  raw.n = n;
  raw.aqi[0] = { 0, 5, 300 }; raw.aqi[1] = { 0, 6, 58 }; raw.aqi[2] = { 0, 15, 61 }; raw.aqi[3] = { 0, 20, 400 }; raw.na = 4;

  day::Inputs in;
  day::derive(raw, 0, 3, in);
  TEST_ASSERT_EQUAL_INT(62, (int)in.tempMinF);
  TEST_ASSERT_EQUAL_INT(89, (int)in.tempMaxF);
  TEST_ASSERT_EQUAL_INT(27, (int)in.tempSwingF);
  TEST_ASSERT_EQUAL_INT(12, (int)in.windMaxMph);
  TEST_ASSERT_EQUAL_INT(2, (int)in.precipChanceMaxPct);
  TEST_ASSERT_EQUAL_INT(49, (int)in.humidityMinPct);
  TEST_ASSERT_EQUAL_INT(83, (int)in.humidityMaxPct);
  TEST_ASSERT_EQUAL_INT(31, (int)in.cloudCoverAvgPct);   // (60+40+25+10+20)/5
  TEST_ASSERT_TRUE(in.hasFirstClearHour);
  TEST_ASSERT_EQUAL_INT(11, in.firstClearHour);
  // hours 11, 14 and 19 are clear but not contiguous, so the longest run is 1
  TEST_ASSERT_EQUAL_INT(1, (int)in.sunRunHours);
  TEST_ASSERT_EQUAL_INT(58, (int)in.aqiMin);
  TEST_ASSERT_EQUAL_INT(61, (int)in.aqiMax);
  TEST_ASSERT_EQUAL_STRING("Clear sky", in.conditionSummary);   // mode: 0 twice
  TEST_ASSERT_EQUAL_STRING("7:08 PM", in.sunsetLocal);
  TEST_ASSERT_EQUAL_STRING("Wednesday", in.weekdayName);
}

void test_derive_longest_sun_run() {
  day::Raw raw;
  raw.day[0].valid = true; raw.day[0].sunriseMin = 6 * 60; raw.day[0].sunsetMin = 20 * 60; raw.day[0].dailyCode = 1;
  int n = 0;
  auto add = [&](int h, int cloud) {
    raw.hours[n++] = day::HourRow{ 0, (int8_t)h, 70, 5, 50, 0, (int16_t)cloud, 1 };
  };
  // clear 7-8, cloudy 9-12, clear 13-16, cloudy 17-20  -> longest run 13..16
  for (int h = 6; h <= 20; h++) add(h, (h >= 7 && h <= 8) || (h >= 13 && h <= 16) ? 10 : 80);
  raw.n = n;
  day::Inputs in;
  day::derive(raw, 0, 3, in);
  TEST_ASSERT_EQUAL_INT(7, in.firstClearHour);
  TEST_ASSERT_EQUAL_INT(4, (int)in.sunRunHours);
  TEST_ASSERT_EQUAL_INT(13, in.sunRunStartHour);
  TEST_ASSERT_EQUAL_INT(16, in.sunRunEndHour);

  // A gap of one hour breaks the run: 7-8, 13-14 and 16 remain. The two
  // two-hour runs tie, and the earlier one wins - more of the day is left.
  for (int i = 0; i < raw.n; i++) if (raw.hours[i].hour == 15) raw.hours[i].cloud = 90;
  day::derive(raw, 0, 3, in);
  TEST_ASSERT_EQUAL_INT(2, (int)in.sunRunHours);
  TEST_ASSERT_EQUAL_INT(7, in.sunRunStartHour);
  TEST_ASSERT_EQUAL_INT(8, in.sunRunEndHour);
}

int main(int, char**) {
  std::ifstream f(DAYRULES_SPEC_PATH);
  std::stringstream ss; ss << f.rdbuf(); specText = ss.str();
  UNITY_BEGIN();
  RUN_TEST(test_spec_loads);
  RUN_TEST(test_rejects_bad_specs);
  RUN_TEST(test_rejects_duplicate_ids);
  RUN_TEST(test_icon_checker_catches_missing_art);
  RUN_TEST(test_beach_day);
  RUN_TEST(test_rain_beats_everything);
  RUN_TEST(test_cold_beats_hot_paths);
  RUN_TEST(test_sun_hat_explains_which_test_failed);
  RUN_TEST(test_layers_day);
  RUN_TEST(test_tshirt_near_threshold_copy);
  RUN_TEST(test_jacket_footer_has_no_suffix);
  RUN_TEST(test_stats);
  RUN_TEST(test_sun_interval);
  RUN_TEST(test_tomorrow_and_eyebrow_override);
  RUN_TEST(test_derive);
  RUN_TEST(test_derive_longest_sun_run);
  return UNITY_END();
}
