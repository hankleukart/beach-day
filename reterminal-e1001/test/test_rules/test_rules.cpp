// Host-side tests: replay the shared fixtures through lib/beachrules.
//   pio test -e native
#include <unity.h>
#include <cstring>
#include <cstdio>
#include "beachrules.h"
#include "parking.h"
#include "aggregate.h"
#include "wmo.h"
#include "fixtures.h"

static char msg[160];

void setUp() {}
void tearDown() {}

static void check_flag(const char* name, const char* flag, int expect, bool actual) {
  if (expect < 0) return;
  snprintf(msg, sizeof(msg), "%s: cond %s", name, flag);
  TEST_ASSERT_EQUAL_MESSAGE(expect != 0, actual, msg);
}

void test_rules_fixtures() {
  for (int i = 0; i < fixtures::RULES_COUNT; i++) {
    const fixtures::RulesCase& c = fixtures::RULES[i];
    beach::Verdict v = beach::evaluate(c.in);

    if (c.checkState) {
      snprintf(msg, sizeof(msg), "%s: state expected %s got %s", c.name,
               beach::stateId(c.expectState), beach::stateId(v.state));
      TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(c.expectState), static_cast<int>(v.state), msg);
    }
    if (c.expectNight >= 0) {
      snprintf(msg, sizeof(msg), "%s: isNight", c.name);
      TEST_ASSERT_EQUAL_MESSAGE(c.expectNight != 0, v.isNight, msg);
    }
    check_flag(c.name, "temp",     c.cond[0], v.condTemp);
    check_flag(c.name, "precip",   c.cond[1], v.condPrecip);
    check_flag(c.name, "wind",     c.cond[2], v.condWind);
    check_flag(c.name, "aqi",      c.cond[3], v.condAqi);
    check_flag(c.name, "forecast", c.cond[4], v.condForecast);
    check_flag(c.name, "time",     c.cond[5], v.condTime);

    if (c.sunText) {
      char buf[24];
      beach::sunRowText(v, buf, sizeof(buf));
      snprintf(msg, sizeof(msg), "%s: sun row text", c.name);
      TEST_ASSERT_EQUAL_STRING_MESSAGE(c.sunText, buf, msg);
    }
    if (c.clearingTime) {
      snprintf(msg, sizeof(msg), "%s: clearingTime", c.name);
      TEST_ASSERT_EQUAL_STRING_MESSAGE(c.clearingTime, v.sun.clearingTime, msg);
    }
    if (c.clearingLater >= 0) {
      snprintf(msg, sizeof(msg), "%s: clearingLater", c.name);
      TEST_ASSERT_EQUAL_MESSAGE(c.clearingLater != 0, v.sun.clearingLater, msg);
    }
    if (c.shownTomorrow >= 0) {
      snprintf(msg, sizeof(msg), "%s: shown day", c.name);
      TEST_ASSERT_EQUAL_PTR_MESSAGE(c.shownTomorrow ? &c.in.tomorrow : &c.in.today, v.shown, msg);
    }
  }
}

void test_parking_fixtures() {
  for (int i = 0; i < fixtures::PARKING_COUNT; i++) {
    const fixtures::ParkingCase& c = fixtures::PARKING[i];
    beach::ParkingAlert a = beach::evaluateParking(c.rules, c.ruleCount, c.in);
    snprintf(msg, sizeof(msg), "%s: active", c.name);
    TEST_ASSERT_EQUAL_MESSAGE(c.expectActive, a.active, msg);
    if (c.expectText) {
      snprintf(msg, sizeof(msg), "%s: text", c.name);
      TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expectText, a.text, msg);
    }
  }
}

static void check_day(const char* name, const char* which, const fixtures::DayExpect& e, const beach::DayInputs& d) {
  struct { const char* f; int expect; int actual; } fields[] = {
    { "temp", e.temp, d.temp }, { "tempMin", e.tempMin, d.tempMin },
    { "precipProb", e.precipProb, d.precipProb }, { "wind", e.wind, d.wind },
    { "aqi", e.aqi, d.aqi }, { "aqiMin", e.aqiMin, d.aqiMin }, { "uv", e.uv, d.uv },
    { "humidityMin", e.humidityMin, d.humidityMin }, { "humidityMax", e.humidityMax, d.humidityMax },
    { "weatherCode", e.weatherCode, d.weatherCode }, { "hourCount", e.hourCount, d.hourCount },
  };
  for (auto& f : fields) {
    if (f.expect < 0) continue;
    snprintf(msg, sizeof(msg), "%s: %s.%s", name, which, f.f);
    TEST_ASSERT_EQUAL_INT_MESSAGE(f.expect, f.actual, msg);
  }
  if (e.conditionText) {
    snprintf(msg, sizeof(msg), "%s: %s.conditionText", name, which);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(e.conditionText, d.conditionText, msg);
  }
  if (e.firstHour >= 0) {
    snprintf(msg, sizeof(msg), "%s: %s first hour row", name, which);
    TEST_ASSERT_TRUE_MESSAGE(d.hourCount > 0, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(e.firstHour, d.hours[0].hour, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(e.firstHourCode, d.hours[0].code, msg);
  }
  if (e.lastHour >= 0) {
    snprintf(msg, sizeof(msg), "%s: %s last hour row", name, which);
    TEST_ASSERT_EQUAL_INT_MESSAGE(e.lastHour, d.hours[d.hourCount - 1].hour, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(e.lastHourCode, d.hours[d.hourCount - 1].code, msg);
  }
}

void test_aggregation_fixtures() {
  for (int i = 0; i < fixtures::AGG_COUNT; i++) {
    const fixtures::AggCase& c = fixtures::AGG[i];
    beach::Inputs in;
    beach::aggregate(c.raw, in);
    if (c.sunriseMinutes >= 0) {
      snprintf(msg, sizeof(msg), "%s: sunriseMinutes", c.name);
      TEST_ASSERT_EQUAL_INT_MESSAGE(c.sunriseMinutes, in.sunriseMinutes, msg);
    }
    if (c.sunsetMinutes >= 0) {
      snprintf(msg, sizeof(msg), "%s: sunsetMinutes", c.name);
      TEST_ASSERT_EQUAL_INT_MESSAGE(c.sunsetMinutes, in.sunsetMinutes, msg);
    }
    check_day(c.name, "today", c.today, in.today);
    check_day(c.name, "tomorrow", c.tomorrow, in.tomorrow);
  }
}

void test_wmo_table() {
  TEST_ASSERT_EQUAL_STRING("Clear sky", beach::wmoDescription(0));
  TEST_ASSERT_EQUAL_STRING("Partly cloudy", beach::wmoDescription(2));
  TEST_ASSERT_EQUAL_STRING("Thunderstorm with heavy hail", beach::wmoDescription(99));
  TEST_ASSERT_EQUAL_STRING("Unknown", beach::wmoDescription(42));
}

void test_hour_formatting() {
  char b[8];
  beach::formatHour12(0, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("12 AM", b);
  beach::formatHour12(9, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("9 AM", b);
  beach::formatHour12(12, b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("12 PM", b);
  beach::formatHour12(15, b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("3 PM", b);
}

void test_week_of_month() {
  TEST_ASSERT_EQUAL_INT(1, beach::weekOfMonth(1));
  TEST_ASSERT_EQUAL_INT(1, beach::weekOfMonth(7));
  TEST_ASSERT_EQUAL_INT(2, beach::weekOfMonth(8));
  TEST_ASSERT_EQUAL_INT(5, beach::weekOfMonth(29));
  TEST_ASSERT_EQUAL_INT(5, beach::weekOfMonth(31));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rules_fixtures);
  RUN_TEST(test_parking_fixtures);
  RUN_TEST(test_aggregation_fixtures);
  RUN_TEST(test_wmo_table);
  RUN_TEST(test_hour_formatting);
  RUN_TEST(test_week_of_month);
  return UNITY_END();
}
