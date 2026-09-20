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
#ifndef CFG_RULES_URL
#  define CFG_RULES_URL "https://raw.githubusercontent.com/hankleukart/beach-day/v3-redesign/shared/v3/day-outcomes.json"
#endif
#ifndef CFG_RULES_CHECK_HOURS
#  define CFG_RULES_CHECK_HOURS 24
#endif

static Settings g;
static bool loaded = false;

bool Settings::configured() const {
  return ssid[0] != '\0' && strcmp(ssid, "YOUR_WIFI") != 0;
}

bool Settings::needsGeocode() const {
  return locationQuery[0] != '\0' && !haveCoords;
}

static const char* NVS_NS = "beachday";

// Parking rule <-> NVS. Keys are <= 15 chars as NVS requires.
static void loadParkingRule(Preferences& p, const char* side, beach::ParkingRule& r) {
  char k[16];
  snprintf(k, sizeof(k), "pk%son", side);  if (p.isKey(k)) r.enabled   = p.getBool(k, r.enabled);
  snprintf(k, sizeof(k), "pk%swk", side);  if (p.isKey(k)) r.weeksMask = p.getUChar(k, r.weeksMask);
  snprintf(k, sizeof(k), "pk%sday", side); if (p.isKey(k)) r.dayOfWeek = (int8_t)p.getChar(k, r.dayOfWeek);
  snprintf(k, sizeof(k), "pk%sst", side);  if (p.isKey(k)) r.startMin  = (int16_t)p.getShort(k, r.startMin);
  snprintf(k, sizeof(k), "pk%send", side); if (p.isKey(k)) r.endMin    = (int16_t)p.getShort(k, r.endMin);
}

static void storeParkingRule(Preferences& p, const char* side, const beach::ParkingRule& r) {
  char k[16];
  snprintf(k, sizeof(k), "pk%son", side);  p.putBool(k, r.enabled);
  snprintf(k, sizeof(k), "pk%swk", side);  p.putUChar(k, r.weeksMask);
  snprintf(k, sizeof(k), "pk%sday", side); p.putChar(k, (int8_t)r.dayOfWeek);
  snprintf(k, sizeof(k), "pk%sst", side);  p.putShort(k, r.startMin);
  snprintf(k, sizeof(k), "pk%send", side); p.putShort(k, r.endMin);
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
  g.haveCoords = true;
  copyStr(g.locationName, sizeof(g.locationName), CFG_LOCATION_NAME);
  g.wakeDayMinutes   = CFG_WAKE_DAY_MINUTES;
  g.wakeNightMinutes = CFG_WAKE_NIGHT_MINUTES;
  g.retryMinutes     = CFG_RETRY_MINUTES;
  g.useTls           = CFG_USE_TLS;
  g.otaEnabled       = CFG_OTA_ENABLED;
  copyStr(g.otaManifestUrl, sizeof(g.otaManifestUrl), CFG_OTA_MANIFEST_URL);
  g.otaCheckHours    = CFG_OTA_CHECK_HOURS;
  g.otaMinBatteryPct = CFG_OTA_MIN_BATTERY_PCT;
  copyStr(g.rulesUrl, sizeof(g.rulesUrl), CFG_RULES_URL);
  g.rulesCheckHours  = CFG_RULES_CHECK_HOURS;

  g.parking[0] = beach::ParkingRule{ CFG_PARKING_LEFT_ENABLED, "LEFT",
                                     beach::weeks(CFG_PARKING_LEFT_WEEKS), CFG_PARKING_LEFT_DAY,
                                     CFG_PARKING_LEFT_START, CFG_PARKING_LEFT_END };
  g.parking[1] = beach::ParkingRule{ CFG_PARKING_RIGHT_ENABLED, "RIGHT",
                                     beach::weeks(CFG_PARKING_RIGHT_WEEKS), CFG_PARKING_RIGHT_DAY,
                                     CFG_PARKING_RIGHT_START, CFG_PARKING_RIGHT_END };
  g.parkingCount = 2;

  // 2. NVS overrides written by the setup portal. If the portal has ever
  //    saved a Wi-Fi network, its view of the world wins over beachday_config.h.
  Preferences prefs;
  if (prefs.begin(NVS_NS, /*readOnly=*/true)) {
    if (prefs.isKey("ssid")) {
      g.fromPortal = true;
      prefs.getString("ssid", g.ssid, sizeof(g.ssid));
      prefs.getString("pass", g.password, sizeof(g.password));
      prefs.getString("locq", g.locationQuery, sizeof(g.locationQuery));
      prefs.getString("cc",   g.countryCode,   sizeof(g.countryCode));
      if (prefs.isKey("label")) prefs.getString("label", g.locationName, sizeof(g.locationName));

      if (prefs.isKey("lat") && prefs.isKey("lon")) {
        g.latitude  = prefs.getDouble("lat", 0);
        g.longitude = prefs.getDouble("lon", 0);
        g.haveCoords = true;
        // No label typed: fall back to the geocoded place name.
        if (g.locationName[0] == '\0' && prefs.isKey("geoName")) {
          prefs.getString("geoName", g.locationName, sizeof(g.locationName));
        }
      } else if (g.locationQuery[0] != '\0') {
        g.haveCoords = false;      // resolve on the next connected boot
      }
      // else: portal left location blank, keep the compile-time coordinates
      // (only sensible on a board that also has a real beachday_config.h).

      loadParkingRule(prefs, "L", g.parking[0]);
      loadParkingRule(prefs, "R", g.parking[1]);
    }
    if (prefs.isKey("otaUrl")) prefs.getString("otaUrl", g.otaManifestUrl, sizeof(g.otaManifestUrl));
    if (prefs.isKey("rulesUrl")) prefs.getString("rulesUrl", g.rulesUrl, sizeof(g.rulesUrl));
    prefs.end();
  }

  loaded = true;
  return g;
}


bool savePortalInput(const PortalInput& in) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, /*readOnly=*/false)) return false;
  prefs.putString("ssid",  in.ssid);
  prefs.putString("pass",  in.password);
  prefs.putString("locq",  in.locationQuery);
  prefs.putString("cc",    in.countryCode);
  prefs.putString("label", in.label);
  // A new place (or the same one retyped) must be geocoded afresh.
  prefs.remove("lat");
  prefs.remove("lon");
  prefs.remove("geoName");
  storeParkingRule(prefs, "L", in.parking[0]);
  storeParkingRule(prefs, "R", in.parking[1]);
  prefs.end();
  loaded = false;              // force a re-read on next loadSettings()
  return true;
}

void saveResolvedLocation(double lat, double lon, const char* shortName) {
  Preferences prefs;
  if (prefs.begin(NVS_NS, /*readOnly=*/false)) {
    prefs.putDouble("lat", lat);
    prefs.putDouble("lon", lon);
    prefs.putString("geoName", shortName ? shortName : "");
    prefs.end();
  }
  // Apply to the in-memory settings so this boot can carry on.
  g.latitude = lat;
  g.longitude = lon;
  g.haveCoords = true;
  if (g.locationName[0] == '\0' && shortName) copyStr(g.locationName, sizeof(g.locationName), shortName);
}

void eraseAllSettings() {
  Preferences prefs;
  if (prefs.begin(NVS_NS, /*readOnly=*/false)) {
    prefs.clear();
    prefs.end();
  }
  loaded = false;
}
