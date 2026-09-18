// 800x480 layout for the GDEY075T7 panel, following beach_day.liquid's
// proportions (hero panel flex 5, details flex 7, cqh/cqw sizing at 800x480).
#pragma once
#include <cstdint>

// Plain-old-data so main can park it in RTC memory and redraw it after a
// failed fetch.
struct ViewModel {
  bool    valid = false;
  uint8_t state = 0;             // beach::State
  bool    isNight = false;
  bool    condSun = false, condTemp = false, condPrecip = false, condWind = false, condAqi = false;
  int16_t temp = 0, tempMin = 0, tempMax = 0, precip = 0, wind = 0;
  int16_t aqi = 0, aqiMin = 0, aqiMax = 0, uv = 0, humMin = 0, humMax = 0;
  char    conditionText[32] = "";
  char    dayLabel[28] = "";      // "FRIDAY" or "TOMORROW (FRIDAY)"
  char    sunText[16] = "";       // "At 11 AM" / "All day" / "Cloudy"
  bool    clearingLater = false;  // nice_day picks the cloud-sun icon
  char    footer[32] = "";        // "Sunset: 7:45 PM" / "Sunrise: 6:30 AM" / "Always"
  bool    parkingActive = false;
  char    parkingText[40] = "";
  char    locationName[24] = "";
  char    updatedText[32] = "";   // "Updated 2:05 PM" / "OFFLINE since 2:05 PM"
  int8_t  batteryPct = -1;
  bool    stale = false;
};

void renderBegin();
void renderView(const ViewModel& vm);
void renderMessage(const char* title, const char* line1, const char* line2);
void renderSetup(const char* apName, const char* ip);   // the setup-portal instruction card
void renderEnd();   // hibernate the panel controller before deep sleep
