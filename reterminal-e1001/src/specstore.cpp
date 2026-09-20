#include "specstore.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include "net.h"
#include "spec_embedded.h"
#include "icons.h"

namespace {
day::Spec g;
const char* PATH = "/day-outcomes.json";
bool fsOk = false;

bool mountFs() {
  if (fsOk) return true;
  fsOk = LittleFS.begin(true);
  if (!fsOk) Serial.println("[spec] LittleFS mount failed");
  return fsOk;
}

bool loadFromFs(char* status, size_t n) {
  if (!mountFs() || !LittleFS.exists(PATH)) return false;
  File f = LittleFS.open(PATH, "r");
  if (!f) return false;
  String body = f.readString();
  f.close();
  char err[96];
  if (!g.load(body.c_str(), body.length(), err, sizeof(err))) {
    Serial.printf("[spec] LittleFS copy rejected: %s\n", err);
    return false;
  }
  snprintf(status, n, "LittleFS: %s v%s", g.name(), g.version());
  return true;
}
} // namespace

day::Spec& spec() { return g; }

bool specLoad(char* status, size_t n) {
  // Names are checked against the art actually compiled in, not just the
  // spec's own icons[] list.
  day::setIconChecker(iconExists);
  if (loadFromFs(status, n)) return true;
  char err[96];
  if (g.load(SPEC_EMBEDDED, SPEC_EMBEDDED_LEN, err, sizeof(err))) {
    snprintf(status, n, "embedded: %s v%s", g.name(), g.version());
    return true;
  }
  snprintf(status, n, "embedded spec invalid: %s", err);
  return false;
}

bool specFetchDue(const Settings& s) {
  if (s.rulesUrl[0] == '\0' || s.rulesCheckHours == 0) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  Preferences p;
  if (!p.begin("beachday", true)) return true;
  time_t last = (time_t)p.getULong64("rulesLast", 0);
  p.end();
  return last == 0 || (now - last) >= (time_t)s.rulesCheckHours * 3600;
}

bool specFetch(const Settings& s, char* status, size_t n) {
  String body;
  if (!netGet(s.rulesUrl, s.useTls, body)) { snprintf(status, n, "rules: request failed"); return false; }
  Preferences p;
  if (p.begin("beachday", false)) { p.putULong64("rulesLast", (uint64_t)time(nullptr)); p.end(); }

  // Validate on a scratch instance so a bad file never displaces the live one.
  day::Spec probe;
  char err[96];
  if (!probe.load(body.c_str(), body.length(), err, sizeof(err))) { snprintf(status, n, "rules rejected: %s", err); return false; }

  if (mountFs()) {
    File f = LittleFS.open(PATH, "w");
    if (f) { f.print(body); f.close(); }
  }
  if (!g.load(body.c_str(), body.length(), err, sizeof(err))) { snprintf(status, n, "rules: reload failed"); return false; }
  snprintf(status, n, "rules: %s v%s (%u bytes)", g.name(), g.version(), body.length());
  return true;
}
