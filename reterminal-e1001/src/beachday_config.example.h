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
// Leaving YOUR_WIFI here is what makes a board start in setup-portal mode,
// which is how you hand one to someone else.
//
// Fill these in for your own bench board. Only the DEV build compiles them in,
// and there they override whatever the portal previously saved, so reflashing
// never costs you the password again. Every other build leaves them out -
// release firmware is public - and uses the network saved on the board. Set
// CFG_CONFIG_WINS to make the dev build prefer the saved network.
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
// change the display (day start, sunrise, evening start, parking cutoff)
// regardless of these intervals.
#define CFG_WAKE_DAY_MINUTES    60     // from sunrise until the evening starts
#define CFG_WAKE_NIGHT_MINUTES  240    // while dark: the evening, and before sunrise
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

// ---- Weekly reminder --------------------------------------------------------
// The footer bar, for one evening or one day a week - Wednesday evening, from
// sunset, by default. Street cleaning takes the bar first
// when both are due. The setup portal can change all of these.
#define CFG_REMINDER_ENABLED       true
#define CFG_REMINDER_DAY           3        // Wednesday
#define CFG_REMINDER_AT_EVENING    true     // false: that day, from when the day starts
#define CFG_REMINDER_TEXT          "Take your trash to the street tonight"

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

// ---- Network ----------------------------------------------------------------
// HTTPS without certificate pinning: traffic is encrypted, the server is not
// authenticated. Avoids a device in a drawer failing when a root CA rotates.
// Set to false for plain HTTP (Open-Meteo serves both).
#define CFG_USE_TLS  true
