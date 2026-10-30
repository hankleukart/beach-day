// Host tests for the v3 engine. Loads the SAME TOML the device ships.
//   pio test -e native
#include <unity.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include "dayspec.h"
#include "derive.h"
#include "reminder.h"

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

// A second edit on a fixture that has already had one.
static std::string also(std::string t, const std::string& find, const std::string& replace) {
  size_t at = t.find(find);
  TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, "fixture anchor missing");
  t.replace(at, find.size(), replace);
  return t;
}

// Quiet weather: nothing but the temperature to go on, so every band lands
// on its plain "clear" square.
static day::Inputs quiet(double tempMax) {
  auto in = base();
  in.tempMaxF = tempMax; in.tempMinF = tempMax - 10; in.tempSwingF = 10;
  in.precipChanceMaxPct = 5; in.windMaxMph = 5; in.cloudCoverAvgPct = 50; in.snowHours = 0;
  return in;
}

static void rejects(const char* why, const std::string& toml) {
  day::Spec s; char err[200] = "";
  if (s.load(toml.c_str(), toml.size(), err, sizeof(err))) TEST_FAIL_MESSAGE(why);
  TEST_ASSERT_FALSE(s.valid());
}

void test_spec_loads() {
  char err[200];
  TEST_ASSERT_TRUE_MESSAGE(spec.load(specText.c_str(), specText.size(), err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_INT(27, spec.outcomeCount());     // 5 bands x 6 conditions, less 3 too warm to snow
  TEST_ASSERT_EQUAL_STRING("freezing_clear", spec.outcomeId(26));
  TEST_ASSERT_EQUAL_STRING("VENICE BEACH", spec.locationLabel());
}

void test_fixture_is_valid() {
  day::Spec s; char err[200];
  std::string t = specWith("", "");
  TEST_ASSERT_TRUE_MESSAGE(s.load(t.c_str(), t.size(), err, sizeof(err)), err);
}

void test_rejects_bad_specs() {
  rejects("not TOML at all", "[[[");
  rejects("unknown field", specWith("also = \"grab\"", "also = \"grab\"\nwhen = [\"tempMaxx >= 1\"]"));
  rejects("bad operator", specWith("also = \"grab\"", "also = \"grab\"\nwhen = [\"tempMax => 1\"]"));
  rejects("no number", specWith("also = \"grab\"", "also = \"grab\"\nwhen = [\"tempMax >=\"]"));
  rejects("no operator", specWith("also = \"grab\"", "also = \"grab\"\nwhen = [\"tempMax 78\"]"));
  rejects("last outcome must be a catch-all", specWith("also = \"grab\"", "also = \"grab\"\nwhen = [\"tempMax >= 99\"]"));
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
  // Whimsy shares the tile, so it is held to the tile's limits.
  rejects("whimsy label too long", specWith("icon = \"tshirt\" }", "icon = \"tshirt\", whimsy = [\"A Label Far Too Long For Any Tile\"] }"));
  rejects("whimsy not a list", specWith("icon = \"tshirt\" }", "icon = \"tshirt\", whimsy = \"Silly A\" }"));
  rejects("whimsy list empty", specWith("icon = \"tshirt\" }", "icon = \"tshirt\", whimsy = [] }"));
  rejects("whimsy never", specWith("", "") + "\n[whimsy]\none_in = 0\n");
}

// --- day names --------------------------------------------------------------

// The fixture's one outcome, renamed by its wear: no `names`, one starred item.
static std::string namedByWear(const std::string& catalogue, const std::string& wear) {
  std::string t = specWith("names = [\"Only Day\"]\n", "");
  t = also(t, "wear = [\"a\", \"a\", \"a\", \"a\"]", "wear = " + wear);
  return also(t, "a = { label = \"A\", icon = \"tshirt\" }", "a = { label = \"A\", icon = \"tshirt\" }\n" + catalogue);
}

void test_day_named_after_starred_wear() {
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = namedByWear("\"Rain boots\" = { icon = \"sun\" }", "[\"*Rain boots\", \"a\", \"a\", \"a\"]");
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  TEST_ASSERT_TRUE(sp.evaluate(base(), sc, nullptr, false));
  TEST_ASSERT_EQUAL_STRING("Rain Boots Day", sc.outcome.title);
  TEST_ASSERT_EQUAL_STRING("sun", sc.outcome.heroIcon);               // the tile's picture, not the outcome's
  TEST_ASSERT_EQUAL_STRING("Rain boots", sc.outcome.wear[0].label);    // the key is the label
  TEST_ASSERT_EQUAL_STRING("A", sc.outcome.wear[1].label);             // an explicit label still wins
}

// On a whimsy day the starred tile can be the one that swaps, and the name
// says what the tile says.
void test_day_name_follows_whimsy() {
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = namedByWear("\"Umbrella\" = { icon = \"tshirt\", whimsy = [\"Duck umbrella\"] }",
                              "[\"*Umbrella\", \"a\", \"a\", \"a\"]") + "\n[whimsy]\none_in = 1\n";
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  TEST_ASSERT_TRUE(sp.evaluate(base(), sc, nullptr, false));
  TEST_ASSERT_EQUAL_STRING("Duck umbrella", sc.outcome.wear[0].label);
  TEST_ASSERT_EQUAL_STRING("Duck Umbrella Day", sc.outcome.title);
}

void test_day_name_title_case() {
  const char* cases[][2] = {
    { "T-shirt", "T-Shirt Day" }, { "Cap with strap", "Cap with Strap Day" },
    { "Captain's slicker", "Captain's Slicker Day" }, { "80s windbreaker", "80s Windbreaker Day" },
    { "Hat over ears", "Hat over Ears Day" },
  };
  for (auto& c : cases) {
    day::Spec sp; day::Screen sc; char err[200];
    std::string q = std::string("\"") + c[0] + "\"";
    std::string t = namedByWear(q + " = { icon = \"tshirt\" }", "[\"*" + std::string(c[0]) + "\", \"a\", \"a\", \"a\"]");
    TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
    TEST_ASSERT_TRUE(sp.evaluate(base(), sc, nullptr, false));
    TEST_ASSERT_EQUAL_STRING(c[1], sc.outcome.title);
  }
}

void test_day_named_after_places_and_things() {
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = specWith("names = [\"Only Day\"]", "places = [\"Beach\"]\nthings = [\"Kite\"]");
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  bool beach = false, kite = false;
  for (int d = 0; d < 6; d++) {
    auto in = base(); in.epochDay = 20621 + d;
    TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
    if (!strcmp(sc.outcome.title, "Beach Day")) beach = true;
    else if (!strcmp(sc.outcome.title, "Kite Day")) kite = true;
    else TEST_FAIL_MESSAGE(sc.outcome.title);
    TEST_ASSERT_EQUAL_STRING("tshirt", sc.outcome.heroIcon);         // places and things keep the outcome's icon
  }
  TEST_ASSERT_TRUE(beach && kite);
}

// A place or thing shows its own icon from [day_icon]; one without an entry
// keeps the outcome's own.
void test_place_and_thing_icons() {
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = specWith("names = [\"Only Day\"]", "places = [\"Beach\"]\nthings = [\"Kite\"]") +
                  "\n[day_icon]\n\"Beach\" = \"sun\"\n";
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  bool beach = false, kite = false;
  for (int d = 0; d < 6; d++) {
    auto in = base(); in.epochDay = 20621 + d;
    TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
    if (!strcmp(sc.outcome.title, "Beach Day")) { beach = true; TEST_ASSERT_EQUAL_STRING("sun", sc.outcome.heroIcon); }
    if (!strcmp(sc.outcome.title, "Kite Day"))  { kite = true;  TEST_ASSERT_EQUAL_STRING("tshirt", sc.outcome.heroIcon); }
  }
  TEST_ASSERT_TRUE(beach && kite);
  rejects("day icon that does not exist", specWith("", "") + "\n[day_icon]\n\"Beach\" = \"sombrero\"\n");
}

// In the real rules, whatever names the day is what the hero shows: across a
// year of dates on every square, a place or thing shows its own icon and a
// clothing name shows its tile's.
void test_real_rules_hero_matches_the_name() {
  const double temps[] = { 90, 72, 57, 40, 20 };
  int checked = 0;
  for (int b = 0; b < 5; b++) for (int c = 0; c < 6; c++) for (int d = 0; d < 12; d++) {
    auto in = quiet(temps[b]); in.epochDay = 20621 + d;
    if (c == 0) in.snowHours = 3;
    if (c == 1) in.precipChanceMaxPct = 70;
    if (c == 2) in.windMaxMph = 24;
    if (c == 3) { in.tempMinF = temps[b] - 24; in.tempSwingF = 24; }
    if (c == 4) in.cloudCoverAvgPct = 5;
    static day::Screen sc;
    TEST_ASSERT_TRUE(spec.evaluate(in, sc, nullptr, false));
    bool fromTile = false;
    for (int i = 0; i < sc.outcome.wearCount; i++) {
      if (!strcmp(sc.outcome.wear[i].icon, sc.outcome.heroIcon)) fromTile = true;
    }
    static const char* const NOUN_ICONS[][2] = {
      { "Beach Day", "beach" }, { "Pool Day", "pool" }, { "Splash Pad Day", "splash_pad" },
      { "Sledding Hill Day", "sledding_hill" }, { "Ice Rink Day", "ice_rink" }, { "Paper Boat Day", "paper_boat" },
      { "Kite Day", "kite" }, { "Sailboat Day", "sailboat" }, { "Sprinkler Day", "sprinkler" },
      { "Water Balloon Day", "water_balloon" }, { "Pinwheel Day", "pinwheel" }, { "Frisbee Day", "frisbee" },
      { "Picnic Blanket Day", "picnic_blanket" }, { "Sidewalk Chalk Day", "sidewalk_chalk" },
      { "Sled Day", "sled" }, { "Ice Skates Day", "ice_skate" },
    };
    bool noun = false;
    for (auto& ni : NOUN_ICONS) if (!strcmp(sc.outcome.title, ni[0])) { noun = true; TEST_ASSERT_EQUAL_STRING_MESSAGE(ni[1], sc.outcome.heroIcon, ni[0]); }
    if (!noun) TEST_ASSERT_TRUE_MESSAGE(fromTile, sc.outcome.title);   // clothing: the tile's icon
    checked++;
  }
  TEST_ASSERT_TRUE(checked > 300);
}

void test_rejects_bad_day_names() {
  rejects("Day written twice", specWith("names = [\"Only Day\"]", "places = [\"Beach Day\"]"));
  rejects("star on an item not in the catalogue", namedByWear("", "[\"*Kilt\", \"a\", \"a\", \"a\"]"));
  rejects("nothing to name the day", namedByWear("", "[\"a\", \"a\", \"a\", \"a\"]"));
  rejects("catalogue name too long for a tile",
          namedByWear("\"An Extremely Long Item\" = { icon = \"tshirt\" }", "[\"*An Extremely Long Item\", \"a\", \"a\", \"a\"]"));
}

void test_rejects_duplicate_ids() {
  rejects("duplicate outcome id", specWith("[[stat]]",
    "[[outcome]]\nid = \"only\"\nnames = [\"Second\"]\ntagline = \"T\"\nicon = \"tshirt\"\n"
    "wear = [\"a\", \"a\", \"a\", \"a\"]\nalso = \"grab\"\n\n[[stat]]"));
}

void test_beach_day() {
  // Hot, near-cloudless, dry, calm, and no big morning chill to dress for.
  auto in = base(); in.tempMinF = 70; in.tempSwingF = 14;
  const auto& s = run(in);
  TEST_ASSERT_EQUAL_STRING("hot_sunny", s.outcome.id);
  TEST_ASSERT_TRUE(s.outcome.isBeachDay);
  TEST_ASSERT_TRUE(!strcmp(s.outcome.title, "Beach Day") || !strcmp(s.outcome.title, "Pool Day") ||
                   !strcmp(s.outcome.title, "Sun Hat Day"));
  TEST_ASSERT_EQUAL_INT(4, s.outcome.wearCount);
  TEST_ASSERT_EQUAL_STRING("Sunset 7:08 PM", s.footer);
  TEST_ASSERT_EQUAL_STRING("VENICE BEACH", s.eyebrow);
  TEST_ASSERT_EQUAL_STRING("Clear sky \xC2\xB7 70\xC2\xB0 this morning, 84\xC2\xB0 this afternoon", s.subline);
  TEST_ASSERT_EQUAL_STRING("", s.outcome.holiday);
}

// Two conditions on one field make a band - the reason `when` is a list and
// not a table, since a table cannot hold the same key twice.
void test_band_condition() {
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = specWith("[[outcome]]", R"([[outcome]]
id = "band"
names = ["Band Day"]
tagline = "T"
icon = "tshirt"
wear = ["a", "a", "a", "a"]
also = "grab"
when = ["tempMax >= 65", "tempMax < 75"]

[[outcome]])");
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  auto in = base();
  in.tempMaxF = 70;
  TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
  TEST_ASSERT_EQUAL_STRING("band", sc.outcome.id);
  in.tempMaxF = 75;                       // above the band
  TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
  TEST_ASSERT_EQUAL_STRING("only", sc.outcome.id);
  in.tempMaxF = 64;                       // below it
  TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
  TEST_ASSERT_EQUAL_STRING("only", sc.outcome.id);
}

// --- the grid ---------------------------------------------------------------

// tempMax alone picks the band. Each edge belongs to the warmer side.
void test_grid_bands() {
  TEST_ASSERT_EQUAL_STRING("hot_clear",      run(quiet(78)).outcome.id);
  TEST_ASSERT_EQUAL_STRING("warm_clear",     run(quiet(77)).outcome.id);
  TEST_ASSERT_EQUAL_STRING("warm_clear",     run(quiet(65)).outcome.id);
  TEST_ASSERT_EQUAL_STRING("cool_clear",     run(quiet(64)).outcome.id);
  TEST_ASSERT_EQUAL_STRING("cool_clear",     run(quiet(50)).outcome.id);
  TEST_ASSERT_EQUAL_STRING("cold_clear",     run(quiet(49)).outcome.id);
  TEST_ASSERT_EQUAL_STRING("cold_clear",     run(quiet(32)).outcome.id);
  TEST_ASSERT_EQUAL_STRING("freezing_clear", run(quiet(31)).outcome.id);
  TEST_ASSERT_EQUAL_STRING("freezing_clear", run(quiet(-5)).outcome.id);
}

// Inside a band the first condition that holds decides the extras:
// snow, then rain, wind, swing, sun - peeled away one at a time here.
void test_grid_precedence() {
  const double temps[] = { 90, 72, 57, 40, 20 };
  const char* bands[] = { "hot", "warm", "cool", "cold", "freezing" };
  for (int b = 0; b < 5; b++) {
    const bool snows = b >= 3;          // the warm three have no snow square
    char want[24];
    auto expect = [&](const char* cond, const day::Inputs& in) {
      snprintf(want, sizeof(want), "%s_%s", bands[b], cond);
      TEST_ASSERT_EQUAL_STRING(want, run(in).outcome.id);
    };
    auto in = quiet(temps[b]);
    in.snowHours = 2; in.precipChanceMaxPct = 60; in.windMaxMph = 25;
    in.tempMinF = temps[b] - 25; in.tempSwingF = 25; in.cloudCoverAvgPct = 10;
    if (snows) expect("snow", in);
    in.snowHours = 0;           expect("rain", in);
    in.precipChanceMaxPct = 10; expect("windy", in);
    in.windMaxMph = 5;          expect("swing", in);
    in.tempMinF = temps[b] - 10; in.tempSwingF = 10;
                                expect("sunny", in);
    in.cloudCoverAvgPct = 50;   expect("clear", in);
  }
}

// Every square of the grid can actually be reached, and lands in its own
// band. An outcome shadowed by an earlier one would be dead and silent.
void test_every_square_reachable() {
  const double temps[] = { 90, 72, 57, 40, 20 };
  const char* bands[] = { "hot_", "warm_", "cool_", "cold_", "freezing_" };
  char seen[40][24]; int n = 0;
  for (int b = 0; b < 5; b++) {
    for (int c = 0; c < 6; c++) {
      auto in = quiet(temps[b]);
      if (c == 0) in.snowHours = 3;
      if (c == 1) in.precipChanceMaxPct = 70;
      if (c == 2) in.windMaxMph = 24;
      if (c == 3) { in.tempMinF = temps[b] - 24; in.tempSwingF = 24; }
      if (c == 4) in.cloudCoverAvgPct = 5;
      const char* id = run(in).outcome.id;
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(id, bands[b], strlen(bands[b])), id);
      bool isNew = true;
      for (int k = 0; k < n; k++) if (!strcmp(seen[k], id)) isNew = false;
      if (isNew) snprintf(seen[n++], sizeof(seen[0]), "%s", id);
    }
  }
  TEST_ASSERT_EQUAL_INT(spec.outcomeCount(), n);
}

void test_too_warm_to_snow() {
  // A snow code at 70 degrees is a forecast glitch, not a snow day.
  auto in = quiet(70); in.snowHours = 3;
  TEST_ASSERT_EQUAL_STRING("warm_clear", run(in).outcome.id);
}

// The near-miss copy counts down to the beach day's own temperature bar.
void test_near_threshold_copy() {
  auto in = quiet(74);
  TEST_ASSERT_EQUAL_STRING("warm_clear", run(in).outcome.id);
  TEST_ASSERT_EQUAL_STRING("4\xC2\xB0 short of a beach day. Lovely for the park, chilly in the water.", run(in).outcome.alsoGrab);
  in = quiet(72);
  TEST_ASSERT_EQUAL_STRING("Also grab: light pants instead, if the morning is chilly.", run(in).outcome.alsoGrab);
}

// When the beach day fails on one condition, an outcome can say which.
void test_also_when_failed() {
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = specWith("also = \"grab\"", "also = \"grab\"\nalso_when_failed = { wind = \"Too windy at the water.\" }");
  t = also(t, "[[outcome]]", "[[outcome]]\nid = \"beach\"\nis_beach_day = true\nwhen = [\"wind <= 15\"]\n"
                              "names = [\"Beach Day\"]\ntagline = \"T\"\nicon = \"tshirt\"\n"
                              "wear = [\"a\", \"a\", \"a\", \"a\"]\nalso = \"grab\"\n\n[[outcome]]");
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  auto in = base(); in.windMaxMph = 20;
  TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
  TEST_ASSERT_EQUAL_STRING("only", sc.outcome.id);
  TEST_ASSERT_EQUAL_STRING("wind", sc.outcome.failedTest);
  TEST_ASSERT_EQUAL_STRING("Too windy at the water.", sc.outcome.alsoGrab);
}

void test_stats() {
  auto s = run(base());
  TEST_ASSERT_EQUAL_INT(6, s.statCount);
  TEST_ASSERT_EQUAL_STRING("SUN", s.stats[0].label);
  TEST_ASSERT_EQUAL_STRING("9a-4p", s.stats[0].value);
  TEST_ASSERT_EQUAL_STRING("10% cloudy", s.stats[0].sub);
  TEST_ASSERT_EQUAL_STRING("Clear", s.stats[0].word);

  TEST_ASSERT_EQUAL_STRING("TEMP", s.stats[1].label);
  TEST_ASSERT_EQUAL_STRING("62\xE2\x80\x93" "84\xC2\xB0", s.stats[1].value);
  TEST_ASSERT_EQUAL_STRING("", s.stats[1].sub);
  TEST_ASSERT_EQUAL_STRING("Hot", s.stats[1].word);

  TEST_ASSERT_EQUAL_STRING("RAIN", s.stats[2].label);
  TEST_ASSERT_EQUAL_STRING("5%", s.stats[2].value);
  TEST_ASSERT_EQUAL_STRING("5% chance", s.stats[2].sub);
  TEST_ASSERT_EQUAL_STRING("Dry", s.stats[2].word);

  TEST_ASSERT_EQUAL_STRING("HUMIDITY", s.stats[3].label);
  TEST_ASSERT_EQUAL_STRING("49\xE2\x80\x93" "83%", s.stats[3].value);
  TEST_ASSERT_EQUAL_STRING("Muggy", s.stats[3].word);

  TEST_ASSERT_EQUAL_STRING("WIND", s.stats[4].label);
  TEST_ASSERT_EQUAL_STRING("8 mph", s.stats[4].value);
  TEST_ASSERT_EQUAL_STRING("Calm", s.stats[4].word);

  TEST_ASSERT_EQUAL_STRING("AIR QUALITY", s.stats[5].label);
  TEST_ASSERT_EQUAL_STRING("40 aqi", s.stats[5].value);
  TEST_ASSERT_EQUAL_STRING("Good", s.stats[5].word);
}

void test_rain_and_sun_windows() {
  auto in = base();
  in.precipChanceMaxPct = 60; in.rainStartHour = 10; in.rainEndHour = 15;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("10a-3p", s.stats[2].value);
  TEST_ASSERT_EQUAL_STRING("60% chance", s.stats[2].sub);   // window and odds together
  in.rainStartHour = 11; in.rainEndHour = 11;
  TEST_ASSERT_EQUAL_STRING("11a", run(in).stats[2].value);
  in = base(); in.sunRunHours = 4; in.sunRunStartHour = 13; in.sunRunEndHour = 16;
  TEST_ASSERT_EQUAL_STRING("1p-4p", run(in).stats[0].value);
  in.sunRunHours = 0; in.sunRunStartHour = -1;
  TEST_ASSERT_EQUAL_STRING("None", run(in).stats[0].value);
}

void test_air_word_takes_the_worse_reading() {
  auto in = base();
  in.windMaxMph = 5; in.aqiMax = 30;
  TEST_ASSERT_EQUAL_STRING("Calm", run(in).stats[4].word);
  TEST_ASSERT_EQUAL_STRING("Good", run(in).stats[5].word);
  in.aqiMax = 160;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("5 mph", s.stats[4].value);
  TEST_ASSERT_EQUAL_STRING("160 aqi", s.stats[5].value);
  TEST_ASSERT_EQUAL_STRING("Stay in", s.stats[5].word);
}

void test_sub_line_is_validated() {
  rejects("sub_field unknown",
          specWith("word_field = \"tempMax\"", "sub_field = \"nope\"\nsub_format = \"x\"\nword_field = \"tempMax\""));
  rejects("sub_format without a sub_field",
          specWith("word_field = \"tempMax\"", "sub_format = \"x\"\nword_field = \"tempMax\""));
  rejects("sub_format and sub_bands together",
          specWith("word_field = \"tempMax\"",
                   "sub_field = \"tempMax\"\nsub_format = \"x\"\nsub_bands = [ { max = 1, text = \"y\" } ]\nword_field = \"tempMax\""));
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
    // The beach day: a pool of three, where some squares have only one.
    auto x = base(); x.tempMinF = 70; x.tempSwingF = 14; x.epochDay = 20621 + d;
    const char* t = run(x).outcome.title;
    bool isNew = true;
    for (int k = 0; k < distinct; k++) if (!strcmp(seen[k], t)) isNew = false;
    if (isNew && distinct < 8) snprintf(seen[distinct++], 48, "%s", t);
  }
  TEST_ASSERT_TRUE_MESSAGE(distinct > 1, "a pool of names should not always pick the same one");
}

void test_full_variant_overrides_and_inherits() {
  // A name-only entry shares the outcome's icon; a full variant replaces
  // what it sets and inherits the rest.
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = specWith("tagline = \"T\"", "tagline = \"T\"\ncolor = \"blue\"");
  t = also(t, "[[stat]]", "[[outcome.variant]]\nname = \"Other Day\"\nicon = \"sun\"\ntagline = \"V\"\n"
                           "wear = [\"b\", \"a\", \"a\", \"a\"]\n\n[[stat]]");
  t = also(t, "a = { label = \"A\", icon = \"tshirt\" }", "a = { label = \"A\", icon = \"tshirt\" }\nb = { label = \"B\", icon = \"sun\" }");
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  bool sawInherited = false, sawOverridden = false;
  for (int d = 0; d < 10; d++) {
    auto in = base(); in.epochDay = 20621 + d;
    TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
    TEST_ASSERT_EQUAL_STRING("blue", sc.outcome.heroColor);         // inherited either way
    if (!strcmp(sc.outcome.title, "Only Day")) {
      sawInherited = true;
      TEST_ASSERT_EQUAL_STRING("tshirt", sc.outcome.heroIcon);
      TEST_ASSERT_EQUAL_STRING("T", sc.outcome.tagline);
      TEST_ASSERT_EQUAL_STRING("A", sc.outcome.wear[0].label);
    } else {
      sawOverridden = true;
      TEST_ASSERT_EQUAL_STRING("Other Day", sc.outcome.title);
      TEST_ASSERT_EQUAL_STRING("sun", sc.outcome.heroIcon);
      TEST_ASSERT_EQUAL_STRING("V", sc.outcome.tagline);
      TEST_ASSERT_EQUAL_STRING("B", sc.outcome.wear[0].label);
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(sawInherited, "the name-only entry should come up");
  TEST_ASSERT_TRUE_MESSAGE(sawOverridden, "the full variant should come up");
}

// --- whimsy -----------------------------------------------------------------

void test_whimsy_swaps_one_tile() {
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = specWith("a = { label = \"A\", icon = \"tshirt\" }",
                           "a = { label = \"A\", icon = \"tshirt\", whimsy = [\"Silly A\"] }") + "\n[whimsy]\none_in = 1\n";
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  for (int d = 0; d < 20; d++) {
    auto in = base(); in.epochDay = 20621 + d;
    TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
    int silly = 0;
    for (int i = 0; i < 4; i++) {
      TEST_ASSERT_EQUAL_STRING("tshirt", sc.outcome.wear[i].icon);   // the picture stays true
      if (!strcmp(sc.outcome.wear[i].label, "Silly A")) silly++;
      else TEST_ASSERT_EQUAL_STRING("A", sc.outcome.wear[i].label);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, silly, "one tile a day, never more");
  }
}

void test_whimsy_leaves_plain_items_alone() {
  day::Spec sp; day::Screen sc; char err[200];
  std::string t = specWith("", "") + "\n[whimsy]\none_in = 1\n";
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
  for (int d = 0; d < 10; d++) {
    auto in = base(); in.epochDay = 20621 + d;
    TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false));
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_STRING("A", sc.outcome.wear[i].label);
  }
}

// Against the real file: about one day in `one_in`, one tile, the same all day.
// The rate is read from the file, so retuning it there needs no test change.
void test_whimsy_in_the_real_rules() {
  std::string plainText = specText;
  const std::string key = "[whimsy]\none_in = ";
  size_t at = plainText.find(key);
  TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, "rules file has no [whimsy] one_in");
  const int oneIn = atoi(plainText.c_str() + at + key.size());
  TEST_ASSERT_TRUE_MESSAGE(oneIn >= 1, "one_in must be a positive number");
  size_t end = plainText.find('\n', at + key.size());
  plainText.replace(at, (end == std::string::npos ? plainText.size() : end) - at, "");
  static day::Spec plain; char err[200];
  TEST_ASSERT_TRUE_MESSAGE(plain.load(plainText.c_str(), plainText.size(), err, sizeof(err)), err);

  int whimsical = 0;
  const int days = 400;
  for (int d = 0; d < days; d++) {
    auto in = base(); in.epochDay = 20000 + d;
    static day::Screen a, b, p;
    TEST_ASSERT_TRUE(spec.evaluate(in, a, nullptr, false));
    TEST_ASSERT_TRUE(spec.evaluate(in, b, nullptr, false));
    TEST_ASSERT_TRUE(plain.evaluate(in, p, nullptr, false));
    int changed = 0;
    for (int i = 0; i < 4; i++) {
      TEST_ASSERT_EQUAL_STRING(a.outcome.wear[i].label, b.outcome.wear[i].label);   // holds all day
      TEST_ASSERT_EQUAL_STRING(p.outcome.wear[i].icon, a.outcome.wear[i].icon);
      if (strcmp(p.outcome.wear[i].label, a.outcome.wear[i].label)) changed++;
    }
    TEST_ASSERT_TRUE_MESSAGE(changed <= 1, "never more than one whimsical tile");
    whimsical += changed;
  }
  // A fair hash lands within a few percent of days / one_in; this allows 40%.
  const int expect = days / oneIn;
  TEST_ASSERT_TRUE_MESSAGE(whimsical > expect * 6 / 10 && whimsical < expect * 14 / 10, "about one day in one_in");
}

// --- the morning pin --------------------------------------------------------

// Three outcomes: "wet" breaks a pin, "warm" and the catch-all "only" do not.
static void pinSpec(day::Spec& sp) {
  char err[200];
  std::string t = specWith("[[outcome]]", R"([[outcome]]
id = "wet"
when = ["rain >= 50"]
breaks_pin = true
names = ["Wet Day"]
tagline = "T"
icon = "tshirt"
wear = ["a", "a", "a", "a"]
also = "grab"

[[outcome]]
id = "warm"
when = ["tempMax >= 78"]
names = ["Warm Day"]
tagline = "T"
icon = "tshirt"
wear = ["a", "a", "a", "a"]
also = "grab"

[[outcome]])");
  TEST_ASSERT_TRUE_MESSAGE(sp.load(t.c_str(), t.size(), err, sizeof(err)), err);
}

void test_pin_holds_the_morning_call() {
  static day::Spec sp; pinSpec(sp);
  day::DayPin pin; char show[24];
  TEST_ASSERT_TRUE(day::resolvePin(sp, pin, 100, true, "warm", show, sizeof(show)));   // first look pins
  TEST_ASSERT_EQUAL_STRING("warm", show);
  TEST_ASSERT_FALSE(day::resolvePin(sp, pin, 100, true, "only", show, sizeof(show)));  // a wobble is held
  TEST_ASSERT_EQUAL_STRING("warm", show);
  TEST_ASSERT_FALSE(day::resolvePin(sp, pin, 100, true, "warm", show, sizeof(show)));  // agreeing: nothing to save
  TEST_ASSERT_EQUAL_STRING("warm", show);
}

void test_pin_breaks_for_rain_and_stays_broken() {
  static day::Spec sp; pinSpec(sp);
  day::DayPin pin; char show[24];
  day::resolvePin(sp, pin, 100, true, "warm", show, sizeof(show));
  TEST_ASSERT_TRUE(day::resolvePin(sp, pin, 100, true, "wet", show, sizeof(show)));
  TEST_ASSERT_EQUAL_STRING("wet", show);
  // Drying out later is not a reason to un-say "take the raincoat".
  TEST_ASSERT_FALSE(day::resolvePin(sp, pin, 100, true, "warm", show, sizeof(show)));
  TEST_ASSERT_EQUAL_STRING("wet", show);
}

void test_pin_leaves_previews_and_new_days_alone() {
  static day::Spec sp; pinSpec(sp);
  day::DayPin pin; char show[24];
  day::resolvePin(sp, pin, 100, true, "warm", show, sizeof(show));
  // Not pinnable - tomorrow's evening preview, or before dawn: live, pin untouched.
  TEST_ASSERT_FALSE(day::resolvePin(sp, pin, 101, false, "only", show, sizeof(show)));
  TEST_ASSERT_EQUAL_STRING("only", show);
  TEST_ASSERT_EQUAL_INT(100, (int)pin.epochDay);
  // The next morning decides afresh.
  TEST_ASSERT_TRUE(day::resolvePin(sp, pin, 101, true, "only", show, sizeof(show)));
  TEST_ASSERT_EQUAL_STRING("only", show);
  TEST_ASSERT_EQUAL_INT(101, (int)pin.epochDay);
  // A pin the rules no longer have (they changed mid-day) is re-decided.
  snprintf(pin.id, sizeof(pin.id), "gone");
  TEST_ASSERT_TRUE(day::resolvePin(sp, pin, 101, true, "warm", show, sizeof(show)));
  TEST_ASSERT_EQUAL_STRING("warm", show);
}

void test_forced_outcome_is_drawn() {
  static day::Spec sp; pinSpec(sp);
  day::Screen sc;
  auto in = quiet(60);                                   // matches only the catch-all
  TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false, "warm"));
  TEST_ASSERT_EQUAL_STRING("warm", sc.outcome.id);
  TEST_ASSERT_EQUAL_STRING("Warm Day", sc.outcome.title);
  TEST_ASSERT_EQUAL_STRING("60", sc.stats[0].value);    // the fixture's one tile: numbers stay live
  TEST_ASSERT_TRUE(sp.evaluate(in, sc, nullptr, false, "no_such"));   // unknown: matched as usual
  TEST_ASSERT_EQUAL_STRING("only", sc.outcome.id);
}

void test_real_rules_break_pins_for_rain_and_snow_only() {
  for (int i = 0; i < spec.outcomeCount(); i++) {
    char id[24]; snprintf(id, sizeof(id), "%s", spec.outcomeId(i));
    const size_t n = strlen(id);
    const bool wet = (n > 5 && !strcmp(id + n - 5, "_rain")) || (n > 5 && !strcmp(id + n - 5, "_snow"));
    TEST_ASSERT_EQUAL_MESSAGE(wet, spec.breaksPin(id), id);
  }
}

// --- the weekly reminder ----------------------------------------------------

void test_reminder_shows_on_its_day() {
  beach::Reminder r;
  r.enabled = true; r.dayOfWeek = 3;
  snprintf(r.text, sizeof(r.text), "Take your trash to the street tonight");
  TEST_ASSERT_TRUE(beach::reminderActive(r, 3));        // Wednesday
  TEST_ASSERT_FALSE(beach::reminderActive(r, 4));
  r.enabled = false;
  TEST_ASSERT_FALSE(beach::reminderActive(r, 3));
  r.enabled = true; r.text[0] = '\0';
  TEST_ASSERT_FALSE_MESSAGE(beach::reminderActive(r, 3), "no words, no bar");
}

void test_street_cleaning_takes_the_bar_first() {
  beach::Reminder r;
  r.enabled = true; r.dayOfWeek = 3;
  snprintf(r.text, sizeof(r.text), "Trash tonight");
  beach::ParkingAlert pa;
  TEST_ASSERT_EQUAL_STRING("Trash tonight", beach::barText(pa, r, 3));
  pa.active = true; snprintf(pa.text, sizeof(pa.text), "NO PARKING LEFT SIDE: 8-10AM");
  TEST_ASSERT_EQUAL_STRING("NO PARKING LEFT SIDE: 8-10AM", beach::barText(pa, r, 3));
  pa.active = false;
  TEST_ASSERT_NULL(beach::barText(pa, r, 4));
}

// --- holidays ---------------------------------------------------------------

void test_holiday_fixed_date() {
  auto in = base();
  in.year = 2026; in.month = 12; in.dayOfMonth = 25; in.weekday = 5;
  in.tempMaxF = 40; in.tempMinF = 30; in.tempSwingF = 10;
  auto s = run(in);
  TEST_ASSERT_EQUAL_STRING("cold_sunny", s.outcome.id);
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

// The readings screen and the rules share this window, so the boundary
// rounding is pinned here rather than left to whichever caller runs first.
void test_daylight_hours() {
  day::Raw raw;
  int h0 = -1, h1 = -1;

  // A partial hour at each end still counts as a whole row.
  raw.day[0].valid = true; raw.day[0].sunriseMin = 6 * 60 + 42; raw.day[0].sunsetMin = 19 * 60 + 8;
  day::daylightHours(raw, 0, h0, h1);
  TEST_ASSERT_EQUAL_INT(6, h0);
  TEST_ASSERT_EQUAL_INT(19, h1);

  // Exactly on the hour must not pull in the hour before or after.
  raw.day[0].sunriseMin = 7 * 60; raw.day[0].sunsetMin = 20 * 60;
  day::daylightHours(raw, 0, h0, h1);
  TEST_ASSERT_EQUAL_INT(7, h0);
  TEST_ASSERT_EQUAL_INT(20, h1);

  // No sun times: the fallback, not a zero-length window.
  raw.day[1].valid = false; raw.day[1].sunriseMin = 0; raw.day[1].sunsetMin = 0;
  day::daylightHours(raw, 1, h0, h1);
  TEST_ASSERT_EQUAL_INT(day::FALLBACK_START_HOUR, h0);
  TEST_ASSERT_EQUAL_INT(day::FALLBACK_END_HOUR, h1);
}

// Snow is counted from the weather code, inside daylight only. Freezing rain
// is rain: it wants a raincoat, not snow boots.
void test_derive_snow() {
  day::Raw raw;
  raw.day[0].valid = true; raw.day[0].sunriseMin = 7 * 60; raw.day[0].sunsetMin = 17 * 60;
  int n = 0;
  auto add = [&](int h, int code) {
    raw.hours[n++] = day::HourRow{ 0, (int8_t)h, 30, 5, 80, 60, 90, (int16_t)code };
  };
  add(5, 73);      // before sunrise: not counted
  add(8, 71);      // slight snow
  add(9, 85);      // snow showers
  add(10, 66);     // freezing rain: not snow
  add(11, 61);     // rain
  add(12, 3);
  raw.n = n;
  day::Inputs in;
  day::derive(raw, 0, 3, in);
  TEST_ASSERT_EQUAL_INT(2, (int)in.snowHours);
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
  RUN_TEST(test_day_named_after_starred_wear);
  RUN_TEST(test_day_name_follows_whimsy);
  RUN_TEST(test_day_name_title_case);
  RUN_TEST(test_day_named_after_places_and_things);
  RUN_TEST(test_place_and_thing_icons);
  RUN_TEST(test_real_rules_hero_matches_the_name);
  RUN_TEST(test_rejects_bad_day_names);
  RUN_TEST(test_beach_day);
  RUN_TEST(test_band_condition);
  RUN_TEST(test_grid_bands);
  RUN_TEST(test_grid_precedence);
  RUN_TEST(test_every_square_reachable);
  RUN_TEST(test_too_warm_to_snow);
  RUN_TEST(test_near_threshold_copy);
  RUN_TEST(test_also_when_failed);
  RUN_TEST(test_stats);
  RUN_TEST(test_rain_and_sun_windows);
  RUN_TEST(test_air_word_takes_the_worse_reading);
  RUN_TEST(test_sub_line_is_validated);
  RUN_TEST(test_variant_is_stable_within_a_day);
  RUN_TEST(test_variant_pool_gets_used_across_days);
  RUN_TEST(test_full_variant_overrides_and_inherits);
  RUN_TEST(test_whimsy_swaps_one_tile);
  RUN_TEST(test_whimsy_leaves_plain_items_alone);
  RUN_TEST(test_whimsy_in_the_real_rules);
  RUN_TEST(test_pin_holds_the_morning_call);
  RUN_TEST(test_pin_breaks_for_rain_and_stays_broken);
  RUN_TEST(test_pin_leaves_previews_and_new_days_alone);
  RUN_TEST(test_forced_outcome_is_drawn);
  RUN_TEST(test_real_rules_break_pins_for_rain_and_snow_only);
  RUN_TEST(test_reminder_shows_on_its_day);
  RUN_TEST(test_street_cleaning_takes_the_bar_first);
  RUN_TEST(test_holiday_fixed_date);
  RUN_TEST(test_holiday_nth_weekday);
  RUN_TEST(test_holiday_last_weekday);
  RUN_TEST(test_hero_panel_and_colour);
  RUN_TEST(test_eyebrow_and_tomorrow);
  RUN_TEST(test_derive);
  RUN_TEST(test_derive_snow);
  RUN_TEST(test_derive_windows);
  RUN_TEST(test_daylight_hours);
  return UNITY_END();
}
