// Host tests for the v3 engine. Loads the SAME TOML the device ships.
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
#define DAYRULES_SPEC_PATH "../shared/v3/day-outcomes.toml"
#endif

static day::Spec spec;
static std::string specText;

void setUp() {}
void tearDown() {}

// 2026-06-17, a Wednesday; epochDay 20621.
static day::Inputs base() {
  day::Inputs in;
  in.tempMinF = 62; in.tempMaxF = 84; in.tempSwingF = 21;
  in.precipChanceMaxPct = 5; in.windMaxMph = 8; in.aqiMax = 40; in.aqiMin = 20;
  in.humidityMinPct = 49; in.humidityMaxPct = 83; in.cloudCoverAvgPct = 10;
  in.hasFirstClearHour = true; in.firstClearHour = 7;
  in.sunRunHours = 8; in.sunRunStartHour = 9; in.sunRunEndHour = 16;
  snprintf(in.conditionSummary, sizeof(in.conditionSummary), "Clear sky");
  snprintf(in.sunsetLocal, sizeof(in.sunsetLocal), "7:08 PM");
  snprintf(in.weekdayName, sizeof(in.weekdayName), "Wednesday");
  in.year = 2026; in.month = 6; in.dayOfMonth = 17; in.weekday = 3; in.epochDay = 20621;
  return in;
}

static const day::Screen& run(const day::Inputs& in, bool tomorrow = false, const char* eyebrow = nullptr) {
  static day::Screen s;
  TEST_ASSERT_TRUE_MESSAGE(spec.evaluate(in, s, eyebrow, tomorrow), "evaluate failed");
  return s;
}

static std::string specWith(const std::string& find, const std::string& replace) {
  std::string t = R"(icons = ["tshirt", "sun"]

[wear]
a = { label = "A", icon = "tshirt" }

[[outcome]]
id = "only"
names = ["Only Day"]
tagline = "T"
icon = "tshirt"
wear = ["a", "a", "a", "a"]
also = "grab"

[[stat]]
id = "s1"
label = "S"
icon = "sun"
value_field = "tempMax"
value_format = "{value}"
word_field = "tempMax"
word_bands = [ { max = 200, text = "w" } ]
)";
  if (!find.empty()) {
    size_t at = t.find(find);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, "fixture anchor missing");
    t.replace(at, find.size(), replace);
  }
  return t;
}

static void rejects(const char* why, const std::string& toml) {
  day::Spec s; char err[200] = "";
  if (s.load(toml.c_str(), toml.size(), err, sizeof(err))) TEST_FAIL_MESSAGE(why);
  TEST_ASSERT_FALSE(s.valid());
}

void test_spec_loads() {
  char err[200];
  TEST_ASSERT_TRUE_MESSAGE(spec.load(specText.c_str(), specText.size(), err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_INT(7, spec.outcomeCount());
  TEST_ASSERT_EQUAL_STRING("jacket_day", spec.outcomeId(6));
  TEST_ASSERT_EQUAL_STRING("VENICE BEACH", spec.locationLabel());
}

void test_fixture_is_valid() {
  day::Spec s; char err[200];
  std::string t = specWith("", "");
  TEST_ASSERT_TRUE_MESSAGE(s.load(t.c_str(), t.size(), err, sizeof(err)), err);
}

void test_rejects_bad_specs() {
  rejects("not TOML at all", "[[[");
  rejects("unknown field", specWith("also = \"grab\"", "also = \"grab\"\n\n[outcome.when]\ntempMaxx = \">= 1\""));
  rejects("bad operator", specWith("also = \"grab\"", "also = \"grab\"\n\n[outcome.when]\ntempMax = \"=> 1\""));
  rejects("no number", specWith("also = \"grab\"", "also = \"grab\"\n\n[outcome.when]\ntempMax = \">=\""));
  rejects("last outcome must be a catch-all", specWith("also = \"grab\"", "also = \"grab\"\n\n[outcome.when]\ntempMax = \">= 99\""));
  rejects("icon not in icons", specWith("icon = \"tshirt\"\nwear", "icon = \"sombrero\"\nwear"));
  rejects("wear id not in catalogue", specWith("wear = [\"a\", \"a\", \"a\", \"a\"]", "wear = [\"a\", \"a\", \"a\", \"kilt\"]"));
  rejects("three wear items", specWith("wear = [\"a\", \"a\", \"a\", \"a\"]", "wear = [\"a\", \"a\", \"a\"]"));
  rejects("no names or variants", specWith("names = [\"Only Day\"]\n", ""));
  rejects("name too long", specWith("\"Only Day\"", "\"An Extremely Long Day Name That Will Never Fit On Any Panel\""));
  rejects("bad colour", specWith("tagline = \"T\"", "tagline = \"T\"\ncolor = \"orange\""));
  rejects("bad panel", specWith("tagline = \"T\"", "tagline = \"T\"\npanel = \"beige\""));
  rejects("catalogue icon unknown", specWith("a = { label = \"A\", icon = \"tshirt\" }", "a = { label = \"A\", icon = \"kilt\" }"));
  rejects("value_format and value_bands together",
          specWith("value_format = \"{value}\"", "value_format = \"{value}\"\nvalue_bands = [ { max = 1, text = \"x\" } ]"));
  rejects("bands backwards",
          specWith("word_bands = [ { max = 200, text = \"w\" } ]", "word_bands = [ { max = 50, text = \"a\" }, { max = 10, text = \"b\" } ]"));
  rejects("last band gated on an optional input",
          specWith("word_bands = [ { max = 200, text = \"w\" } ]", "word_bands = [ { max = 200, text = \"w\", requires = \"firstClearHour\" } ]"));
  rejects("holiday month 13", specWith("", "") + "\n[[holiday]]\nname = \"Nope\"\nmonth = 13\nday = 1\n");
  rejects("holiday with no date rule", specWith("", "") + "\n[[holiday]]\nname = \"Nope\"\nmonth = 5\n");
  // A variant is validated as thoroughly as the outcome it overrides.
  rejects("variant with an unknown icon",
          specWith("[[stat]]", "[[outcome.variant]]\nname = \"Other\"\nicon = \"sombrero\"\n\n[[stat]]"));
  rejects("variant with three wear items",
          specWith("[[stat]]", "[[outcome.variant]]\nname = \"Other\"\nwear = [\"a\", \"a\", \"a\"]\n\n[[stat]]"));
  rejects("variant with no name",
          specWith("[[stat]]", "[[outcome.variant]]\nicon = \"tshirt\"\n\n[[stat]]"));
}

void test_rejects_duplicate_ids() {
  rejects("duplicate outcome id", specWith("[[stat]]",
    "[[outcome]]\nid = \"only\"\nnames = [\"Second\"]\ntagline = \"T\"\nicon = \"tshirt\"\n"
    "wear = [\"a\", \"a\", \"a\", \"a\"]\nalso = \"grab\"\n\n[[stat]]"));
}

void test_beach_day() {
  const auto& s = run(base());
  TEST_ASSERT_EQUAL_STRING("beach_day", s.outcome.id);
  // Tagline, icon and wear vary by variant now, so pin what cannot.
  TEST_ASSERT_TRUE(s.outcome.tagline[0] != '\0');
  TEST_ASSERT_TRUE(s.outcome.heroIcon[0] != '\0');
  TEST_ASSERT_EQUAL_INT(4, s.outcome.wearCount);
  TEST_ASSERT_EQUAL_STRING("Sunset: 7:08 PM", s.footer);
  TEST_ASSERT_EQUAL_STRING("VENICE BEACH", s.eyebrow);
  TEST_ASSERT_EQUAL_STRING("Clear sky \xC2\xB7 62\xC2\xB0 this morning, 84\xC2\xB0 this afternoon", s.subline);
  TEST_ASSERT_EQUAL_STRING("", s.outcome.holiday);
}

void test_precedence() {
  auto in = base(); in.precipChanceMaxPct = 50;
  TEST_ASSERT_EQUAL_STRING("rain_day", run(in).outcome.id);
  in = base(); in.tempMaxF = 51; in.tempMinF = 40; in.tempSwingF = 11;
  TEST_ASSERT_EQUAL_STRING("big_coat_day", run(in).outcome.id);
  in.tempMaxF = 52;
  TEST_ASSERT_EQUAL_STRING("jacket_day", run(in).outcome.id);
  in = base(); in.tempMaxF = 74; in.tempMinF = 60; in.tempSwingF = 14;
  TEST_ASSERT_EQUAL_STRING("tshirt_day", run(in).outcome.id);
}

void test_sun_hat_explains_itself() {
  auto in = base(); in.windMaxMph = 16;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("sun_hat_day", s.outcome.id);
  TEST_ASSERT_EQUAL_STRING("wind", s.outcome.failedTest);
  TEST_ASSERT_EQUAL_STRING("Too windy at the water today \xE2\x80\x94 the shady park is the better pick.", s.outcome.alsoGrab);
  in = base(); in.cloudCoverAvgPct = 36;
  TEST_ASSERT_EQUAL_STRING("Grey overhead but still hot \xE2\x80\x94 sunscreen anyway, it burns through.", run(in).outcome.alsoGrab);
}

void test_near_threshold_copy() {
  auto in = base(); in.tempMaxF = 74; in.tempMinF = 60; in.tempSwingF = 14;
  TEST_ASSERT_EQUAL_STRING("4 degrees short of a beach day \xE2\x80\x94 lovely for the park, chilly in the water.", run(in).outcome.alsoGrab);
  in.tempMaxF = 71;
  TEST_ASSERT_EQUAL_STRING("Also grab: a light sweater for after dinner.", run(in).outcome.alsoGrab);
}

void test_stats() {
  auto s = run(base());
  TEST_ASSERT_EQUAL_INT(6, s.statCount);
  TEST_ASSERT_EQUAL_STRING("SUN", s.stats[0].label);
  TEST_ASSERT_EQUAL_STRING("9A-4P", s.stats[0].value);
  TEST_ASSERT_EQUAL_STRING("84\xC2\xB0" "F", s.stats[1].value);
  TEST_ASSERT_EQUAL_STRING("warm", s.stats[1].word);
  TEST_ASSERT_EQUAL_STRING("HUMIDITY", s.stats[3].label);
  TEST_ASSERT_EQUAL_STRING("49-83%", s.stats[3].value);
  TEST_ASSERT_EQUAL_STRING("AIR", s.stats[5].label);
  TEST_ASSERT_EQUAL_STRING("40 aqi", s.stats[5].value);
}

void test_rain_and_sun_windows() {
  auto in = base();
  in.precipChanceMaxPct = 60; in.rainStartHour = 10; in.rainEndHour = 15;
  TEST_ASSERT_EQUAL_STRING("10A-3P", run(in).stats[2].value);
  in.rainStartHour = 11; in.rainEndHour = 11;
  TEST_ASSERT_EQUAL_STRING("11A", run(in).stats[2].value);
  in = base(); in.sunRunHours = 4; in.sunRunStartHour = 13; in.sunRunEndHour = 16;
  TEST_ASSERT_EQUAL_STRING("1P-4P", run(in).stats[0].value);
  in.sunRunHours = 0; in.sunRunStartHour = -1;
  TEST_ASSERT_EQUAL_STRING("None", run(in).stats[0].value);
}

// --- variants ---------------------------------------------------------------

void test_variant_is_stable_within_a_day() {
  auto in = base();
  char held[48];
  snprintf(held, sizeof(held), "%s", run(in).outcome.title);
  // The board redraws hourly; the name must not churn under the reader.
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_STRING(held, run(in).outcome.title);
}

void test_variant_pool_gets_used_across_days() {
  char seen[8][48];
  int distinct = 0;
  for (int d = 0; d < 21; d++) {
    auto x = base(); x.epochDay = 20621 + d;
    const char* t = run(x).outcome.title;
    bool isNew = true;
    for (int k = 0; k < distinct; k++) if (!strcmp(seen[k], t)) isNew = false;
    if (isNew && distinct < 8) snprintf(seen[distinct++], 48, "%s", t);
  }
  TEST_ASSERT_TRUE_MESSAGE(distinct > 1, "a pool of names should not always pick the same one");
}

void test_full_variant_overrides_and_inherits() {
  // rain_day has three name-only variants sharing the outcome's icon, plus
  // two that replace the icon, tagline, wear and copy.
  bool sawInherited = false, sawOverridden = false;
  for (int d = 0; d < 30; d++) {
    auto in = base(); in.epochDay = 20621 + d; in.precipChanceMaxPct = 70;
    auto s = run(in);
    TEST_ASSERT_EQUAL_STRING("rain_day", s.outcome.id);
    TEST_ASSERT_EQUAL_INT(4, s.outcome.wearCount);
    if (!strcmp(s.outcome.heroIcon, "rain_boot")) {
      sawInherited = true;
      TEST_ASSERT_EQUAL_STRING("PUDDLES AHEAD", s.outcome.tagline);   // from the outcome
      TEST_ASSERT_EQUAL_STRING("blue", s.outcome.heroColor);
    }
    if (!strcmp(s.outcome.title, "Umbrella Day")) {
      sawOverridden = true;
      TEST_ASSERT_EQUAL_STRING("umbrella", s.outcome.heroIcon);       // from the variant
      TEST_ASSERT_EQUAL_STRING("KEEP IT OVERHEAD", s.outcome.tagline);
      TEST_ASSERT_EQUAL_STRING("Umbrella", s.outcome.wear[0].label);
      TEST_ASSERT_EQUAL_STRING("blue", s.outcome.heroColor);          // still inherited
    }
    if (!strcmp(s.outcome.title, "Raincoat Day")) {
      TEST_ASSERT_EQUAL_STRING("yellow", s.outcome.heroColor);        // variant overrides colour
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(sawInherited, "name-only variants should inherit the outcome's icon");
  TEST_ASSERT_TRUE_MESSAGE(sawOverridden, "a full variant should replace icon, tagline and wear");
}

// --- holidays ---------------------------------------------------------------

void test_holiday_fixed_date() {
  auto in = base();
  in.year = 2026; in.month = 12; in.dayOfMonth = 25; in.weekday = 5;
  in.tempMaxF = 40; in.tempMinF = 30; in.tempSwingF = 10;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("big_coat_day", s.outcome.id);
  TEST_ASSERT_EQUAL_STRING("Christmas", s.outcome.holiday);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(s.outcome.title, "Christmas ", 10), "title is prefixed");
  in.dayOfMonth = 26;
  TEST_ASSERT_EQUAL_STRING("", run(in).outcome.holiday);
}

void test_holiday_nth_weekday() {
  // Thanksgiving 2026 is Thursday 26 November.
  auto in = base();
  in.year = 2026; in.month = 11; in.dayOfMonth = 26; in.weekday = 4;
  TEST_ASSERT_EQUAL_STRING("Thanksgiving", run(in).outcome.holiday);
  in.dayOfMonth = 19;
  TEST_ASSERT_EQUAL_STRING("", run(in).outcome.holiday);
  in.dayOfMonth = 25; in.weekday = 3;
  TEST_ASSERT_EQUAL_STRING("", run(in).outcome.holiday);
}

void test_holiday_last_weekday() {
  // Memorial Day 2026 is the last Monday of May: 25 May.
  auto in = base();
  in.year = 2026; in.month = 5; in.dayOfMonth = 25; in.weekday = 1;
  TEST_ASSERT_EQUAL_STRING("Memorial", run(in).outcome.holiday);
  in.dayOfMonth = 18;
  TEST_ASSERT_EQUAL_STRING("", run(in).outcome.holiday);
}

void test_hero_panel_and_colour() {
  day::Spec sp; char err[200]; day::Screen sc; day::Inputs in = base();
  auto load = [&](const std::string& t) {
    TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
    TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
  };
  load(specWith("tagline = \"T\"", "tagline = \"T\"\npanel = \"light\"\ncolor = \"yellow\""));
  TEST_ASSERT_TRUE(sc.outcome.heroLight);
  TEST_ASSERT_EQUAL_STRING("yellow", sc.outcome.heroColor);
  load(specWith("tagline = \"T\"", "tagline = \"T\"\npanel = \"dark\""));
  TEST_ASSERT_FALSE(sc.outcome.heroLight);
  TEST_ASSERT_EQUAL_STRING("none", sc.outcome.heroColor);
  load(specWith("", "") + "\n[screen]\nhero_panel_default = \"light\"\n");
  TEST_ASSERT_TRUE_MESSAGE(sc.outcome.heroLight, "falls back to screen.hero_panel_default");
}

void test_eyebrow_and_tomorrow() {
  auto s = run(base(), true, "Cambridge");
  TEST_ASSERT_EQUAL_STRING("CAMBRIDGE", s.eyebrow);
  TEST_ASSERT_EQUAL_STRING("Clear sky \xC2\xB7 62\xC2\xB0 in the morning, 84\xC2\xB0 in the afternoon", s.subline);
}

void test_derive() {
  day::Raw raw;
  raw.day[0].valid = true; raw.day[0].sunriseMin = 6 * 60 + 38; raw.day[0].sunsetMin = 19 * 60 + 8; raw.day[0].dailyCode = 3;
  int n = 0;
  auto add = [&](int h, double t, double w, int hum, int pp, int cloud, int code) {
    raw.hours[n++] = day::HourRow{ 0, (int8_t)h, t, w, (int16_t)hum, (int16_t)pp, (int16_t)cloud, (int16_t)code };
  };
  add(3, 50, 30, 99, 90, 100, 61);
  add(6, 62.4, 3, 83, 0, 60, 3);
  add(9, 70, 6, 70, 2, 40, 2);
  add(11, 80, 8.6, 60, 2, 25, 1);
  add(14, 89.4, 12, 49, 0, 10, 0);
  add(19, 78, 9, 55, 1, 20, 0);
  add(21, 60, 20, 90, 60, 90, 61);
  raw.n = n;
  raw.aqi[0] = { 0, 5, 300 }; raw.aqi[1] = { 0, 6, 58 }; raw.aqi[2] = { 0, 15, 61 }; raw.aqi[3] = { 0, 20, 400 }; raw.na = 4;
  day::Inputs in;
  day::derive(raw, 0, 3, in);
  TEST_ASSERT_EQUAL_INT(62, (int)in.tempMinF);
  TEST_ASSERT_EQUAL_INT(89, (int)in.tempMaxF);
  TEST_ASSERT_EQUAL_INT(31, (int)in.cloudCoverAvgPct);
  TEST_ASSERT_EQUAL_INT(11, in.firstClearHour);
  TEST_ASSERT_EQUAL_INT(58, (int)in.aqiMin);
  TEST_ASSERT_EQUAL_STRING("Clear sky", in.conditionSummary);
  TEST_ASSERT_EQUAL_STRING("7:08 PM", in.sunsetLocal);
}

void test_derive_windows() {
  day::Raw raw;
  raw.day[0].valid = true; raw.day[0].sunriseMin = 6 * 60; raw.day[0].sunsetMin = 20 * 60;
  int n = 0;
  for (int h = 6; h <= 20; h++) {
    int cloud = ((h >= 7 && h <= 8) || (h >= 13 && h <= 16)) ? 10 : 80;
    int pp = (h == 10 || h == 12) ? 40 : 5;
    raw.hours[n++] = day::HourRow{ 0, (int8_t)h, 70, 5, 50, (int16_t)pp, (int16_t)cloud, 1 };
  }
  raw.n = n;
  day::Inputs in;
  day::derive(raw, 0, 3, in);
  TEST_ASSERT_EQUAL_INT(4, (int)in.sunRunHours);
  TEST_ASSERT_EQUAL_INT(13, in.sunRunStartHour);
  TEST_ASSERT_EQUAL_INT(16, in.sunRunEndHour);
  TEST_ASSERT_EQUAL_INT(10, in.rainStartHour);
  TEST_ASSERT_EQUAL_INT(12, in.rainEndHour);
}

int main(int, char**) {
  std::ifstream f(DAYRULES_SPEC_PATH);
  std::stringstream ss; ss << f.rdbuf(); specText = ss.str();
  UNITY_BEGIN();
  RUN_TEST(test_spec_loads);
  RUN_TEST(test_fixture_is_valid);
  RUN_TEST(test_rejects_bad_specs);
  RUN_TEST(test_rejects_duplicate_ids);
  RUN_TEST(test_beach_day);
  RUN_TEST(test_precedence);
  RUN_TEST(test_sun_hat_explains_itself);
  RUN_TEST(test_near_threshold_copy);
  RUN_TEST(test_stats);
  RUN_TEST(test_rain_and_sun_windows);
  RUN_TEST(test_variant_is_stable_within_a_day);
  RUN_TEST(test_variant_pool_gets_used_across_days);
  RUN_TEST(test_full_variant_overrides_and_inherits);
  RUN_TEST(test_holiday_fixed_date);
  RUN_TEST(test_holiday_nth_weekday);
  RUN_TEST(test_holiday_last_weekday);
  RUN_TEST(test_hero_panel_and_colour);
  RUN_TEST(test_eyebrow_and_tomorrow);
  RUN_TEST(test_derive);
  RUN_TEST(test_derive_windows);
  return UNITY_END();
}
