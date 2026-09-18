#include "geocode.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cctype>
#include <cstring>
#include "net.h"

namespace {

String urlEncode(const String& s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    unsigned char c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else if (c == ' ') out += '+';
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

String trimmed(String s) { s.trim(); return s; }

bool startsWithNoCase(const char* s, const String& prefix) {
  if (!s || prefix.isEmpty()) return false;
  for (size_t i = 0; i < prefix.length(); i++) {
    if (!s[i] || tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return false;
  }
  return true;
}

} // namespace

bool geocode(const char* query, const char* countryCode, bool tls,
             GeoResult& out, char* err, size_t errLen) {
  String q = trimmed(String(query));
  if (q.isEmpty()) { snprintf(err, errLen, "no place given"); return false; }

  // "Springfield, IL" -> search "Springfield", prefer results in "IL".
  String hint;
  int comma = q.indexOf(',');
  if (comma > 0) {
    hint = trimmed(q.substring(comma + 1));
    q = trimmed(q.substring(0, comma));
  }

  String url = String(tls ? "https" : "http") +
               "://geocoding-api.open-meteo.com/v1/search?count=8&language=en&format=json&name=" +
               urlEncode(q);
  if (countryCode && strlen(countryCode) == 2) url += "&countryCode=" + String(countryCode);

  String body;
  if (!netGet(url.c_str(), tls, body)) { snprintf(err, errLen, "geocoding request failed"); return false; }

  JsonDocument doc;
  if (deserializeJson(doc, body)) { snprintf(err, errLen, "geocoding: bad JSON"); return false; }
  JsonArrayConst results = doc["results"];
  if (results.size() == 0) { snprintf(err, errLen, "no place called '%s'", query); return false; }

  JsonObjectConst pick = results[0];
  if (!hint.isEmpty()) {
    for (JsonObjectConst r : results) {
      const char* admin1 = r["admin1"] | "";
      const char* cc     = r["country_code"] | "";
      const char* cname  = r["country"] | "";
      if (startsWithNoCase(admin1, hint) || hint.equalsIgnoreCase(cc) || startsWithNoCase(cname, hint)) {
        pick = r;
        break;
      }
    }
  }

  out.lat = pick["latitude"]  | 0.0;
  out.lon = pick["longitude"] | 0.0;
  const char* name   = pick["name"] | query;
  const char* admin1 = pick["admin1"] | "";
  const char* cc     = pick["country_code"] | "";
  snprintf(out.shortName, sizeof(out.shortName), "%s", name);
  if (admin1[0]) snprintf(out.fullName, sizeof(out.fullName), "%s, %s, %s", name, admin1, cc);
  else           snprintf(out.fullName, sizeof(out.fullName), "%s, %s", name, cc);
  return true;
}
