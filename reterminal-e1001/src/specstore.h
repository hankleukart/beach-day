// Where the rules come from, in order: LittleFS (/day-outcomes.json, put there
// by `pio run -t uploadfs` or by a fetch), then the copy embedded at build
// time. A fetched file replaces the LittleFS one only after it parses and
// passes the engine's sanity checks, so a bad publish cannot take the display
// down.
#pragma once
#include <cstddef>
#include "dayspec.h"
#include "settings.h"

day::Spec& spec();
bool specLoad(char* status, size_t n);                               // true when any spec is loaded
bool specFetchDue(const Settings& s);
bool specFetch(const Settings& s, char* status, size_t n);           // GET, validate, store, reload
