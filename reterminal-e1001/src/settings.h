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
  bool   haveCoords   = false;      // lat/lon are usable
  char   locationName[24] = "";     // label shown in the hero panel
  char   locationQuery[48] = "";    // what the person typed in the portal
  char   countryCode[4] = "";       // optional 2-letter hint for geocoding
  bool   fromPortal   = false;      // NVS (portal) values are in effect
  uint16_t wakeDayMinutes   = 60;
  uint16_t wakeNightMinutes = 240;
  uint16_t retryMinutes     = 10;
  bool   useTls = true;
  // OTA (src/ota.cpp)
  bool     otaEnabled = true;
  char     otaManifestUrl[160] = "";
  uint16_t otaCheckHours  = 24;
  uint8_t  otaMinBatteryPct = 30;
  // Runtime rules (src/specstore.cpp)
  char     rulesUrl[160] = "";
  uint16_t rulesCheckHours = 24;
  beach::ParkingRule parking[2] = {};
  int    parkingCount = 0;

  bool configured() const;    // false while ssid is empty or the placeholder
  bool needsGeocode() const;  // a place was typed but never resolved
};

const Settings& loadSettings();

// What the setup portal collects. Saving it replaces everything the portal
// owns and clears any previously resolved coordinates.
struct PortalInput {
  char ssid[33] = "";
  char password[65] = "";
  char locationQuery[48] = "";
  char countryCode[4] = "";
  char label[24] = "";
  beach::ParkingRule parking[2] = {};
};
bool savePortalInput(const PortalInput& in);
void saveResolvedLocation(double lat, double lon, const char* shortName);
void eraseAllSettings();
