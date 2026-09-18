// Runtime settings: compile-time defaults from beachday_config.h, overridable by
// values stored in NVS (Preferences namespace "beachday"). The NVS layer is
// what a captive-portal setup flow would write to.
#pragma once
#include <cstdint>
#include "parking.h"

struct Settings {
  char   ssid[33]     = "";
  char   password[65] = "";
  double latitude     = 0;
  double longitude    = 0;
  char   locationName[24] = "";
  uint16_t wakeDayMinutes   = 60;
  uint16_t wakeNightMinutes = 240;
  uint16_t retryMinutes     = 10;
  bool   useTls = true;
  // OTA (src/ota.cpp)
  bool     otaEnabled = true;
  char     otaManifestUrl[160] = "";
  uint16_t otaCheckHours  = 24;
  uint8_t  otaMinBatteryPct = 30;
  beach::ParkingRule parking[2] = {};
  int    parkingCount = 0;

  bool configured() const;   // false while ssid is still the placeholder
};

const Settings& loadSettings();
