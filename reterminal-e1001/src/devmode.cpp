#ifdef BEACHDAY_DEV
#include "devmode.h"
#include <Arduino.h>
#include <cstring>
#include "pins.h"
#include "power.h"
#include "beachrules.h"

#if __has_include("beachday_config.h")
#  include "beachday_config.h"
#else
#  include "beachday_config.example.h"
#endif
#ifndef CFG_DEV_REFRESH_MINUTES
#  define CFG_DEV_REFRESH_MINUTES 20
#endif

namespace {

constexpr int DEMO_COUNT = 9;

// Numbers chosen so each state's checklist marks agree with why it fired.
void buildDemoView(int idx, ViewModel& vm) {
  auto st = static_cast<beach::State>(idx % DEMO_COUNT);
  vm = ViewModel{};
  vm.valid = true;
  vm.state = static_cast<uint8_t>(st);

  // Sensible baseline: a passing day, then break whatever this state needs.
  vm.temp = 78; vm.tempMin = 62; vm.tempMax = 78;
  vm.precip = 10; vm.wind = 9;
  vm.aqi = 42; vm.aqiMin = 18; vm.aqiMax = 42;
  vm.uv = 7; vm.humMin = 41; vm.humMax = 63;
  vm.condTemp = vm.condPrecip = vm.condWind = vm.condAqi = vm.condSun = true;
  snprintf(vm.conditionText, sizeof(vm.conditionText), "Mainly clear");
  snprintf(vm.sunText, sizeof(vm.sunText), "All day");
  snprintf(vm.dayLabel, sizeof(vm.dayLabel), "THURSDAY");
  snprintf(vm.footer, sizeof(vm.footer), "Sunset: 6:57 PM");
  snprintf(vm.locationName, sizeof(vm.locationName), "Santa Monica");
  snprintf(vm.updatedText, sizeof(vm.updatedText), "Demo %d/%d", idx + 1, DEMO_COUNT);
  vm.batteryPct = 76;

  switch (st) {
    case beach::State::BeachDay:
      break;
    case beach::State::NightTime:
      vm.isNight = true;
      snprintf(vm.dayLabel, sizeof(vm.dayLabel), "TOMORROW (FRIDAY)");
      snprintf(vm.footer, sizeof(vm.footer), "Sunrise: 6:39 AM");
      break;
    case beach::State::IndoorDay:
      vm.aqi = 168; vm.aqiMax = 168; vm.aqiMin = 121; vm.condAqi = false;
      snprintf(vm.conditionText, sizeof(vm.conditionText), "Fog");
      break;
    case beach::State::RainDay:
      vm.precip = 72; vm.condPrecip = false;
      snprintf(vm.conditionText, sizeof(vm.conditionText), "Moderate rain");
      snprintf(vm.sunText, sizeof(vm.sunText), "Cloudy");
      vm.condSun = false;
      break;
    case beach::State::WindDay:
      vm.wind = 24; vm.condWind = false;
      break;
    case beach::State::ChillyDay:
      vm.temp = 58; vm.tempMax = 58; vm.tempMin = 49; vm.condTemp = false;
      snprintf(vm.conditionText, sizeof(vm.conditionText), "Overcast");
      break;
    case beach::State::NiceDay:
      vm.temp = 71; vm.tempMax = 71; vm.condTemp = false;
      vm.clearingLater = true;
      snprintf(vm.sunText, sizeof(vm.sunText), "At 11 AM");
      snprintf(vm.conditionText, sizeof(vm.conditionText), "Partly cloudy");
      break;
    case beach::State::GreyDay:
      vm.condSun = false;
      snprintf(vm.sunText, sizeof(vm.sunText), "2 hrs");
      snprintf(vm.conditionText, sizeof(vm.conditionText), "Overcast");
      break;
    case beach::State::JustADay:
      vm.precip = 28; vm.condPrecip = false;
      snprintf(vm.conditionText, sizeof(vm.conditionText), "Partly cloudy");
      break;
  }
}

// Debounced falling edge on an active-low button.
struct Button {
  int pin;
  bool last = true;
  explicit Button(int p) : pin(p) { pinMode(p, INPUT_PULLUP); }
  bool pressed() {
    bool now = digitalRead(pin);
    bool fell = (last && !now);
    last = now;
    if (fell) delay(40);
    return fell;
  }
};

void banner() {
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F(" Beach Day - DEV MODE (stays awake)"));
  Serial.println(F(" Type a key here, or press a button on the board:"));
  Serial.println(F("   n / KEY2 (left)    next demo state"));
  Serial.println(F("   p / KEY1 (middle)  toggle parking alert"));
  Serial.println(F("   l / KEY0 (right)   back to the live forecast"));
  Serial.println(F("   d                  dump the current view as text"));
  Serial.println(F("   R                  reboot and re-fetch"));
  Serial.println(F("   ?                  show this again"));
  Serial.printf(  "   (the live forecast refreshes itself every %d min)\n", CFG_DEV_REFRESH_MINUTES);
  Serial.println(F(" Ctrl-C quits the monitor (the board keeps running)."));
  Serial.println(F("=================================================="));
}

void dumpView(const ViewModel& vm, const char* which) {
  Serial.printf("[dev] --- %s ---\n", which);
  Serial.printf("  state       %s%s\n", beach::stateId(static_cast<beach::State>(vm.state)),
                vm.isNight ? " (night: showing tomorrow)" : "");
  Serial.printf("  headline    %s %s / %s\n",
                beach::stateTitleLine1(static_cast<beach::State>(vm.state)),
                beach::stateTitleLine2(static_cast<beach::State>(vm.state)),
                beach::stateSubtitle(static_cast<beach::State>(vm.state)));
  Serial.printf("  conditions  sun[%d] temp[%d] rain[%d] wind[%d] aqi[%d]\n",
                vm.condSun, vm.condTemp, vm.condPrecip, vm.condWind, vm.condAqi);
  Serial.printf("  numbers     temp %d (%d-%d)  rain %d%%  wind %d  aqi %d (%d-%d)  uv %d  hum %d-%d%%\n",
                vm.temp, vm.tempMin, vm.tempMax, vm.precip, vm.wind,
                vm.aqi, vm.aqiMin, vm.aqiMax, vm.uv, vm.humMin, vm.humMax);
  Serial.printf("  text        day='%s' cond='%s' sun='%s' footer='%s'\n",
                vm.dayLabel, vm.conditionText, vm.sunText, vm.footer);
  Serial.printf("  parking     %s%s\n", vm.parkingActive ? "ACTIVE " : "off",
                vm.parkingActive ? vm.parkingText : "");
  Serial.printf("  status      %s  battery %d%%\n", vm.updatedText, vm.batteryPct);
}

} // namespace

void devLoop(const ViewModel& liveView, bool liveValid) {
  banner();

  ViewModel live = liveView;
  ViewModel demo;
  int demoIdx = -1;          // -1 = showing the live view
  bool parkingOverlay = false;

  Button key2(PIN_KEY2), key1(PIN_KEY1), key0(PIN_KEY0);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  uint32_t lastBlink = 0;
  bool ledOn = false;

  // The dev build never deep-sleeps, so nothing would otherwise re-run the
  // fetch: without this the panel shows whatever it got at boot, forever.
  // Restarting re-uses the whole boot path rather than duplicating it here.
  const uint32_t refreshMs = (uint32_t)CFG_DEV_REFRESH_MINUTES * 60UL * 1000UL;
  const uint32_t startedAt = millis();
  Serial.printf("[dev] next automatic forecast refresh in %d min\n", CFG_DEV_REFRESH_MINUTES);

  for (;;) {
    // Only when the live view is on screen - never interrupt demo browsing.
    if (demoIdx < 0 && refreshMs > 0 && millis() - startedAt > refreshMs) {
      Serial.println(F("[dev] refresh interval reached, restarting to re-fetch"));
      Serial.flush();
      ESP.restart();
    }

    // Serial keys mirror the buttons, so the board can be driven from the
    // monitor without reaching for it.
    char cmd = 0;
    while (Serial.available()) {
      int c = Serial.read();
      if (c > 0 && c != '\r' && c != '\n') cmd = (char)c;
    }
    if (cmd == '?') banner();
    if (cmd == 'R') { Serial.println(F("[dev] rebooting")); Serial.flush(); ESP.restart(); }
    if (cmd == 'd') dumpView((demoIdx >= 0) ? demo : live, (demoIdx >= 0) ? "demo view" : "live view");

    if (key2.pressed() || cmd == 'n') {
      demoIdx = (demoIdx + 1) % DEMO_COUNT;
      buildDemoView(demoIdx, demo);
      if (parkingOverlay) {
        demo.parkingActive = true;
        snprintf(demo.parkingText, sizeof(demo.parkingText), "NO PARKING LEFT SIDE: 8-10AM");
      }
      Serial.printf("[dev] demo %d/%d -> %s\n", demoIdx + 1, DEMO_COUNT,
                    beach::stateId(static_cast<beach::State>(demoIdx)));
      renderView(demo);
      dumpView(demo, "demo view");
    }

    if (key1.pressed() || cmd == 'p') {
      parkingOverlay = !parkingOverlay;
      Serial.printf("[dev] parking alert %s\n", parkingOverlay ? "ON" : "OFF");
      ViewModel& target = (demoIdx >= 0) ? demo : live;
      target.parkingActive = parkingOverlay;
      if (parkingOverlay) {
        snprintf(target.parkingText, sizeof(target.parkingText), "NO PARKING LEFT SIDE: 8-10AM");
      }
      if (demoIdx >= 0 || liveValid) renderView(target);
    }

    if (key0.pressed() || cmd == 'l') {
      if (!liveValid) {
        Serial.println(F("[dev] no live view captured this boot - reset to re-fetch"));
      } else {
        demoIdx = -1;
        Serial.println(F("[dev] showing the live forecast"));
        renderView(live);
      }
    }

    // Slow heartbeat so it's obvious the board is awake.
    if (millis() - lastBlink > 2000) {
      lastBlink = millis();
      ledOn = !ledOn;
      digitalWrite(PIN_LED, ledOn ? LOW : HIGH);   // LED is inverted
    }
    delay(20);
  }
}
#endif
