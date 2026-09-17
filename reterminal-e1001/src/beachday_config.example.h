// Beach Day — reTerminal E1001 configuration.
//
//   cp src/beachday_config.example.h src/beachday_config.h   (gitignored)
//
// then fill in Wi-Fi and location. Everything else has sensible defaults.
// These are compile-time defaults; settings.cpp lets values stored in flash
// (NVS) override them, which is where a future setup portal will write.
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

// ---- Network ----------------------------------------------------------------
// HTTPS without certificate pinning: traffic is encrypted, the server is not
// authenticated. Avoids a device in a drawer failing when a root CA rotates.
// Set to false for plain HTTP (Open-Meteo serves both).
#define CFG_USE_TLS  true
