#ifdef BEACHDAY_DEV
#include "devmode.h"
#include <Arduino.h>
#include <cstring>
#include "pins.h"
#include "power.h"
#include "beachrules.h"

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
  Serial.println(F("=============================================="));
  Serial.println(F(" Beach Day - DEV MODE (no deep sleep)"));
  Serial.println(F("   KEY2 (left)   next demo state"));
  Serial.println(F("   KEY1 (middle) toggle parking alert"));
  Serial.println(F("   KEY0 (right)  back to the live forecast"));
  Serial.println(F(" Serial stays up, so uploads work without"));
  Serial.println(F(" holding BOOT."));
  Serial.println(F("=============================================="));
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

  for (;;) {
    if (key2.pressed()) {
      demoIdx = (demoIdx + 1) % DEMO_COUNT;
      buildDemoView(demoIdx, demo);
      if (parkingOverlay) {
        demo.parkingActive = true;
        snprintf(demo.parkingText, sizeof(demo.parkingText), "NO PARKING LEFT SIDE: 8-10AM");
      }
      Serial.printf("[dev] demo %d/%d -> %s\n", demoIdx + 1, DEMO_COUNT,
                    beach::stateId(static_cast<beach::State>(demoIdx)));
      renderView(demo);
    }

    if (key1.pressed()) {
      parkingOverlay = !parkingOverlay;
      Serial.printf("[dev] parking alert %s\n", parkingOverlay ? "ON" : "OFF");
      ViewModel& target = (demoIdx >= 0) ? demo : live;
      target.parkingActive = parkingOverlay;
      if (parkingOverlay) {
        snprintf(target.parkingText, sizeof(target.parkingText), "NO PARKING LEFT SIDE: 8-10AM");
      }
      if (demoIdx >= 0 || liveValid) renderView(target);
    }

    if (key0.pressed()) {
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
