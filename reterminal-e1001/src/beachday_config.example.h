// Beach Day — reTerminal E1001 configuration.
//
// You have two ways to configure a board:
//
//   1. The setup portal (no editing). Flash with this file as-is - the
//      YOUR_WIFI placeholder makes the board start as a "BeachDay-Setup-xxxx"
//      hotspot, and a phone fills in Wi-Fi, location and parking. That is the
//      path for a board you are giving to someone else.
//
//   2. Compile-time (your own bench unit):
//        cp src/beachday_config.example.h src/beachday_config.h   (gitignored)
//      and fill in Wi-Fi and location below.
//
// Anything the portal has saved (NVS) takes precedence over these values.
// Hold the MIDDLE button while pressing the RIGHT one to reopen the portal.
#pragma once

// ---- Wi-Fi ------------------------------------------------------------------
#define CFG_WIFI_SSID      "YOUR_WIFI"
#define CFG_WIFI_PASSWORD  "YOUR_PASSWORD"

// ---- Location ---------------------------------------------------------------
// Decimal degrees. Open-Meteo picks the timezone from these automatically.
#define CFG_LATITUDE       34.0195
#define CFG_LONGITUDE      -118.4912
// Shown in small caps at the top of the hero panel. "" hides it.
#define CFG_LOCATION_NAME  "Santa Monica"

// ---- Wake schedule ----------------------------------------------------------
// The board deep-sleeps between updates. It always wakes for the moments that
// change the display (sunrise, sunset-1h, sunset, parking cutoff) regardless
// of these intervals.
#define CFG_WAKE_DAY_MINUTES    60     // between sunrise-1h and sunset
#define CFG_WAKE_NIGHT_MINUTES  240    // overnight
#define CFG_RETRY_MINUTES       10     // after a failed fetch

// ---- Street parking alerts (shared/rules.json "parkingAlert") ---------------
// Day of week: 0 = Sunday ... 6 = Saturday. Weeks: which weeks of the month.
#define CFG_PARKING_LEFT_ENABLED   true
#define CFG_PARKING_LEFT_WEEKS     { 1, 3 }
#define CFG_PARKING_LEFT_DAY       3        // Wednesday
#define CFG_PARKING_LEFT_START     (8 * 60)
#define CFG_PARKING_LEFT_END       (10 * 60)

#define CFG_PARKING_RIGHT_ENABLED  true
#define CFG_PARKING_RIGHT_WEEKS    { 1, 3 }
#define CFG_PARKING_RIGHT_DAY      4        // Thursday
#define CFG_PARKING_RIGHT_START    (8 * 60)
#define CFG_PARKING_RIGHT_END      (10 * 60)

// ---- Dev build only ---------------------------------------------------------
// [env:dev] stays awake instead of deep-sleeping, so it has to refresh the
// forecast on a timer. It does that by restarting, which re-runs the normal
// boot path. Ignored by the release build, which refreshes on every wake.
#define CFG_DEV_REFRESH_MINUTES 20

// ---- Over-the-air updates --------------------------------------------------
// The board checks this URL for a newer firmware version, at most once every
// CFG_OTA_CHECK_HOURS, and only when the battery is above the floor below.
// Publish new firmware with tools/release.sh. Holding the LEFT button (KEY2)
// while the board wakes forces an immediate check.
#define CFG_OTA_ENABLED        true
#define CFG_OTA_MANIFEST_URL   "https://github.com/hankleukart/beach-day/releases/latest/download/manifest.json"
#define CFG_OTA_CHECK_HOURS    24
#define CFG_OTA_MIN_BATTERY_PCT 30

// ---- Runtime rules ----------------------------------------------------------
// The outcomes, criteria and copy live in shared/v3/day-outcomes.json, loaded
// at boot from the board's LittleFS (pio run -t uploadfs) with a built-in
// fallback. Once a day the board re-fetches it from here, so copy and
// thresholds change on every unit without a firmware update. "" disables.
#define CFG_RULES_URL          "https://raw.githubusercontent.com/hankleukart/beach-day/v3-redesign/shared/v3/day-outcomes.json"
#define CFG_RULES_CHECK_HOURS  24

// ---- Network ----------------------------------------------------------------
// HTTPS without certificate pinning: traffic is encrypted, the server is not
// authenticated. Avoids a device in a drawer failing when a root CA rotates.
// Set to false for plain HTTP (Open-Meteo serves both).
#define CFG_USE_TLS  true
