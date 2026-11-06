// What the setup page keeps when a field comes back unchanged. Saving the page
// replaces everything it owns, so without these a blank password box would wipe
// the saved password, and every save would look the place up again.
#pragma once
#include <cstring>

namespace beach {

// A blank box on the saved network means "keep it". On any other network it
// means an open network: the old password must not follow it there.
inline bool keepSavedPassword(const char* newPassword, const char* newSsid, const char* savedSsid) {
  return !newPassword[0] && newSsid[0] && strcmp(newSsid, savedSsid) == 0;
}

// The same place and country keep their coordinates. One that never resolved
// has none to keep, so it gets looked up again.
inline bool keepSavedPlace(bool haveCoords, const char* newPlace, const char* shownPlace,
                           const char* newCountry, const char* shownCountry) {
  return haveCoords && newPlace[0] && strcmp(newPlace, shownPlace) == 0 && strcmp(newCountry, shownCountry) == 0;
}

} // namespace beach
