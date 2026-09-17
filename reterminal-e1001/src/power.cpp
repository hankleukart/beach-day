#include "power.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include "pins.h"

float batteryVolts() {
  pinMode(PIN_BAT_EN, OUTPUT);
  digitalWrite(PIN_BAT_EN, HIGH);
  delay(10);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogReadMilliVolts(PIN_BAT_ADC); delay(2); }
  digitalWrite(PIN_BAT_EN, LOW);
  return (sum / 16) / 1000.0f * 2.0f;   // 2:1 divider
}

int batteryPercent(float v) {
  static const struct { float v; int p; } curve[] = {
    { 4.15f, 100 }, { 4.05f, 90 }, { 3.97f, 80 }, { 3.90f, 70 }, { 3.84f, 60 },
    { 3.79f, 50 },  { 3.75f, 40 }, { 3.71f, 30 }, { 3.66f, 20 }, { 3.55f, 10 }, { 3.30f, 0 },
  };
  const int n = sizeof(curve) / sizeof(curve[0]);
  if (v >= curve[0].v) return 100;
  for (int i = 1; i < n; i++) {
    if (v >= curve[i].v) {
      float span = curve[i - 1].v - curve[i].v;
      float frac = (v - curve[i].v) / span;
      return curve[i].p + (int)(frac * (curve[i - 1].p - curve[i].p) + 0.5f);
    }
  }
  return 0;
}

bool wokeByButton() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1; }

void deepSleepFor(uint32_t seconds) {
  if (seconds < 60) seconds = 60;
  if (seconds > 6 * 3600) seconds = 6 * 3600;

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);   // off

  rtc_gpio_pullup_en((gpio_num_t)PIN_KEY0);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_KEY0);
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_KEY0, ESP_EXT1_WAKEUP_ALL_LOW);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);

  Serial.printf("[power] deep sleep for %u s\n", (unsigned)seconds);
  Serial.flush();
  esp_deep_sleep_start();
  for (;;) {}
}
