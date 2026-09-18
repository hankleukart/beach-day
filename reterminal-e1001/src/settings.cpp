#include "settings.h"
#include <Preferences.h>
#include <cstring>

#if __has_include("beachday_config.h")
#  include "beachday_config.h"
#else
#  include "beachday_config.example.h"
#  warning "src/beachday_config.h not found - building with beachday_config.example.h placeholders"
#endif

// An existing beachday_config.h predates these, so default them here rather
// than forcing everyone to re-copy the example.
#ifndef CFG_OTA_ENABLED
#  define CFG_OTA_ENABLED true
#endif
#ifndef CFG_OTA_MANIFEST_URL
#  define CFG_OTA_MANIFEST_URL "https://github.com/hankleukart/beach-day/releases/latest/download/manifest.json"
#endif
#ifndef CFG_OTA_CHECK_HOURS
#  define CFG_OTA_CHECK_HOURS 24
#endif
#ifndef CFG_OTA_MIN_BATTERY_PCT
#  define CFG_OTA_MIN_BATTERY_PCT 30
#endif

static Settings g;
static bool loaded = false;

bool Settings::configured() const {
  return ssid[0] != '\0' && strcmp(ssid, "YOUR_WIFI") != 0;
}

static void copyStr(char* dst, size_t n, const char* src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

const Settings& loadSettings() {
  if (loaded) return g;

  // 1. compile-time defaults
  copyStr(g.ssid, sizeof(g.ssid), CFG_WIFI_SSID);
  copyStr(g.password, sizeof(g.password), CFG_WIFI_PASSWORD);
  g.latitude  = CFG_LATITUDE;
  g.longitude = CFG_LONGITUDE;
  copyStr(g.locationName, sizeof(g.locationName), CFG_LOCATION_NAME);
  g.wakeDayMinutes   = CFG_WAKE_DAY_MINUTES;
  g.wakeNightMinutes = CFG_WAKE_NIGHT_MINUTES;
  g.retryMinutes     = CFG_RETRY_MINUTES;
  g.useTls           = CFG_USE_TLS;
  g.otaEnabled       = CFG_OTA_ENABLED;
  copyStr(g.otaManifestUrl, sizeof(g.otaManifestUrl), CFG_OTA_MANIFEST_URL);
  g.otaCheckHours    = CFG_OTA_CHECK_HOURS;
  g.otaMinBatteryPct = CFG_OTA_MIN_BATTERY_PCT;

  g.parking[0] = beach::ParkingRule{ CFG_PARKING_LEFT_ENABLED, "LEFT",
                                     beach::weeks(CFG_PARKING_LEFT_WEEKS), CFG_PARKING_LEFT_DAY,
                                     CFG_PARKING_LEFT_START, CFG_PARKING_LEFT_END };
  g.parking[1] = beach::ParkingRule{ CFG_PARKING_RIGHT_ENABLED, "RIGHT",
                                     beach::weeks(CFG_PARKING_RIGHT_WEEKS), CFG_PARKING_RIGHT_DAY,
                                     CFG_PARKING_RIGHT_START, CFG_PARKING_RIGHT_END };
  g.parkingCount = 2;

  // 2. NVS overrides, if a setup flow has stored any
  Preferences prefs;
  if (prefs.begin("beachday", /*readOnly=*/true)) {
    if (prefs.isKey("ssid")) prefs.getString("ssid", g.ssid, sizeof(g.ssid));
    if (prefs.isKey("pass")) prefs.getString("pass", g.password, sizeof(g.password));
    if (prefs.isKey("lat"))  g.latitude  = prefs.getDouble("lat", g.latitude);
    if (prefs.isKey("lon"))  g.longitude = prefs.getDouble("lon", g.longitude);
    if (prefs.isKey("name")) prefs.getString("name", g.locationName, sizeof(g.locationName));
    if (prefs.isKey("otaUrl")) prefs.getString("otaUrl", g.otaManifestUrl, sizeof(g.otaManifestUrl));
    prefs.end();
  }

  loaded = true;
  return g;
}
