#ifdef BEACHDAY_DEV
#include "devmode.h"
#include <Arduino.h>
#include <cstring>
#include "pins.h"
#include "power.h"
#include "specstore.h"
#include "settings.h"
#include "portal.h"

#if __has_include("beachday_config.h")
#  include "beachday_config.h"
#else
#  include "beachday_config.example.h"
#endif
#ifndef CFG_DEV_REFRESH_MINUTES
#  define CFG_DEV_REFRESH_MINUTES 20
#endif

namespace {

// One set of numbers per outcome in the spec, run through the REAL engine so
// the copy on screen is the copy in the JSON. Order matches outcomes[].
struct Preset { const char* name; day::Inputs in; };

day::Inputs mk(double tmin, double tmax, double rain, double wind, double aqi, double cloud, int clearAt, const char* cond) {
  day::Inputs i;
  i.tempMinF = tmin; i.tempMaxF = tmax; i.tempSwingF = tmax - tmin;
  i.precipChanceMaxPct = rain; i.windMaxMph = wind; i.aqiMax = aqi; i.aqiMin = aqi > 20 ? aqi - 15 : 5;
  i.humidityMinPct = 49; i.humidityMaxPct = 83; i.cloudCoverAvgPct = cloud;
  i.hasFirstClearHour = clearAt >= 0; i.firstClearHour = clearAt;
  snprintf(i.conditionSummary, sizeof(i.conditionSummary), "%s", cond);
  snprintf(i.sunsetLocal, sizeof(i.sunsetLocal), "7:08 PM");
  snprintf(i.weekdayName, sizeof(i.weekdayName), "Wednesday");
  return i;
}

const Preset PRESETS[] = {
  { "rain boots",     mk(52, 58, 80, 12, 30, 95, -1, "Moderate rain") },
  { "big coat",       mk(38, 47,  5,  9, 25, 60, 10, "Overcast") },
  { "beach",          mk(74, 89,  2,  9, 61, 10,  6, "Clear sky") },
  { "sun hat (wind)", mk(70, 86,  5, 22, 40, 20,  7, "Mainly clear") },
  { "layers",         mk(50, 76,  5,  8, 35, 45, 11, "Partly cloudy") },
  { "t-shirt (near)", mk(60, 74,  8, 10, 30, 30,  9, "Partly cloudy") },
  { "jacket",         mk(54, 63, 15, 11, 45, 80, -1, "Overcast") },
};
constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

struct Button {
  int pin; bool last = true;
  explicit Button(int p) : pin(p) { pinMode(p, INPUT_PULLUP); }
  bool pressed() { bool now = digitalRead(pin); bool fell = (last && !now); last = now; if (fell) delay(40); return fell; }
};

void banner() {
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F(" Beach Day v3 - DEV MODE (stays awake)"));
  Serial.println(F(" Type a key here, or press a button on the board:"));
  Serial.println(F("   n / KEY2 (left)    next demo outcome"));
  Serial.println(F("   p / KEY1 (middle)  toggle parking alert"));
  Serial.println(F("   l / KEY0 (right)   back to the live forecast"));
  Serial.println(F("   d                  dump the current screen as text"));
  Serial.println(F("   w                  open the Wi-Fi setup portal now"));
  Serial.println(F("   R                  reboot and re-fetch"));
  Serial.println(F("   ?                  show this again"));
  Serial.printf(  "   (the live forecast refreshes itself every %d min)\n", CFG_DEV_REFRESH_MINUTES);
  Serial.printf(  " rules: %s v%s, %d outcomes\n", spec().name(), spec().version(), spec().outcomeCount());
  Serial.println(F("=================================================="));
}

void dump(const ViewModel& vm, const char* which) {
  const day::Screen& s = vm.screen;
  Serial.printf("[dev] --- %s ---\n", which);
  Serial.printf("  outcome   %s%s\n", s.outcome.id, s.tomorrow ? " (tomorrow)" : "");
  Serial.printf("  hero      %s | %s %s %s | %s\n", s.eyebrow, s.outcome.title[0], s.outcome.title[1], s.outcome.title[2], s.outcome.tagline);
  Serial.printf("  header    %s | %s | %s\n", s.weekday, s.corner1, s.corner2);
  Serial.printf("  subline   %s\n", s.subline);
  Serial.printf("  wear      "); for (int i = 0; i < s.outcome.wearCount; i++) Serial.printf("[%s:%s] ", s.outcome.wear[i].label, s.outcome.wear[i].icon); Serial.println();
  Serial.printf("  also      %s\n", s.outcome.alsoGrab);
  for (int i = 0; i < s.statCount; i++) Serial.printf("  stat      %-9s %-6s %-12s %s\n", s.stats[i].label, s.stats[i].icon, s.stats[i].value, s.stats[i].word);
  Serial.printf("  footer    %s\n", vm.parkingActive ? vm.parkingText : s.footer);
  Serial.printf("  status    %s\n", vm.statusText);
}

} // namespace

void devLoop(const ViewModel& liveView, bool liveValid) {
  banner();
  ViewModel live = liveView, demo;
  int demoIdx = -1;
  bool parking = false;
  const Settings& s = loadSettings();

  Button key2(PIN_KEY2), key1(PIN_KEY1), key0(PIN_KEY0);
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, HIGH);
  uint32_t lastBlink = 0; bool ledOn = false;
  const uint32_t refreshMs = (uint32_t)CFG_DEV_REFRESH_MINUTES * 60UL * 1000UL;
  const uint32_t startedAt = millis();

  for (;;) {
    if (demoIdx < 0 && refreshMs > 0 && millis() - startedAt > refreshMs) {
      Serial.println(F("[dev] refresh interval reached, restarting to re-fetch")); Serial.flush(); ESP.restart();
    }
    char cmd = 0;
    while (Serial.available()) { int c = Serial.read(); if (c > 0 && c != '\r' && c != '\n') cmd = (char)c; }
    if (cmd == '?') banner();
    if (cmd == 'R') { Serial.println(F("[dev] rebooting")); Serial.flush(); ESP.restart(); }
    if (cmd == 'w') { Serial.println(F("[dev] opening the setup portal")); runSetupPortal(loadSettings()); }
    if (cmd == 'd') dump((demoIdx >= 0) ? demo : live, (demoIdx >= 0) ? "demo" : "live");

    if (key2.pressed() || cmd == 'n') {
      demoIdx = (demoIdx + 1) % PRESET_COUNT;
      demo = ViewModel{};
      demo.valid = spec().evaluate(PRESETS[demoIdx].in, demo.screen, s.locationName, false);
      demo.parkingActive = parking;
      snprintf(demo.parkingText, sizeof(demo.parkingText), "NO PARKING LEFT SIDE: 8-10AM");
      snprintf(demo.statusText, sizeof(demo.statusText), "Demo %d/%d \xC2\xB7 %s", demoIdx + 1, PRESET_COUNT, PRESETS[demoIdx].name);
      Serial.printf("[dev] demo %d/%d '%s' -> %s\n", demoIdx + 1, PRESET_COUNT, PRESETS[demoIdx].name, demo.screen.outcome.id);
      renderScreen(demo);
      dump(demo, "demo");
    }
    if (key1.pressed() || cmd == 'p') {
      parking = !parking;
      ViewModel& t = (demoIdx >= 0) ? demo : live;
      t.parkingActive = parking;
      snprintf(t.parkingText, sizeof(t.parkingText), "NO PARKING LEFT SIDE: 8-10AM");
      Serial.printf("[dev] parking alert %s\n", parking ? "ON" : "OFF");
      if (demoIdx >= 0 || liveValid) renderScreen(t);
    }
    if (key0.pressed() || cmd == 'l') {
      if (!liveValid) Serial.println(F("[dev] no live view this boot - press R to re-fetch"));
      else { demoIdx = -1; Serial.println(F("[dev] showing the live forecast")); renderScreen(live); }
    }
    if (millis() - lastBlink > 2000) { lastBlink = millis(); ledOn = !ledOn; digitalWrite(PIN_LED, ledOn ? LOW : HIGH); }
    delay(20);
  }
}
#endif
