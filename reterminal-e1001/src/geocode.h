// Turns "Santa Monica", "90401" or "Springfield, IL" into coordinates using
// Open-Meteo's free geocoding API. Runs on the first connected boot after the
// setup portal saved a place; the result is stored so it never runs again.
#pragma once
#include <cstddef>

struct GeoResult {
  double lat = 0, lon = 0;
  char shortName[24] = "";   // "Santa Monica" - fits the hero-panel label
  char fullName[80]  = "";   // "Santa Monica, California, US" - for the log
};

// countryCode may be "" for no hint. Returns false with a human-readable err.
bool geocode(const char* query, const char* countryCode, bool tls,
             GeoResult& out, char* err, size_t errLen);
