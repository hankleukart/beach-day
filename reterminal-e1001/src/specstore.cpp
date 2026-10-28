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
const char* PATH = "/day-outcomes.toml";
bool fsOk = false;

int parseVersion(const char* v) {
  if (!v) return 0;
  int maj = 0, min = 0;
  sscanf(v, "%d.%d", &maj, &min);
  return maj * 1000 + min;
}

bool mountFs() {
  if (fsOk) return true;
  fsOk = LittleFS.begin(true);
  if (!fsOk) Serial.println("[spec] LittleFS mount failed");
  return fsOk;
}

bool saveToFs(const char* data, size_t len) {
  if (!mountFs()) return false;
  File f = LittleFS.open(PATH, "w");
  if (!f) return false;
  f.write((const uint8_t*)data, len);
  f.close();
  return true;
}
} // namespace

day::Spec& spec() { return g; }

bool specLoad(char* status, size_t n) {
  // Names are checked against the art actually compiled in, not just the
  // spec's own icons[] list.
  day::setIconChecker(iconExists);

  // Check embedded spec
  day::Spec emb;
  char embErr[96] = "";
  bool embOk = emb.load(SPEC_EMBEDDED, SPEC_EMBEDDED_LEN, embErr, sizeof(embErr));

  // Check LittleFS
  day::Spec fsSpec;
  char fsErr[96] = "";
  bool fsLoaded = false;
  if (mountFs() && LittleFS.exists(PATH)) {
    File f = LittleFS.open(PATH, "r");
    if (f) {
      String body = f.readString();
      f.close();
      fsLoaded = fsSpec.load(body.c_str(), body.length(), fsErr, sizeof(fsErr));
    }
  }

  Preferences p;
  p.begin("beachday", false);
  uint32_t fsMtime = p.getUInt("rulesMtime", 0);

  // If embedded is newer than LittleFS (by mtime or version string), upgrade LittleFS!
  bool embIsNewer = (SPEC_EMBEDDED_MTIME > fsMtime) || (parseVersion(emb.version()) > parseVersion(fsSpec.version()));
  if (embOk && (!fsLoaded || embIsNewer)) {
    saveToFs(SPEC_EMBEDDED, SPEC_EMBEDDED_LEN);
    p.putUInt("rulesMtime", SPEC_EMBEDDED_MTIME);
    p.end();
    g.load(SPEC_EMBEDDED, SPEC_EMBEDDED_LEN, embErr, sizeof(embErr));
    snprintf(status, n, "embedded v%s (updated LittleFS, mtime %lu)", emb.version(), (unsigned long)SPEC_EMBEDDED_MTIME);
    return true;
  }
  p.end();

  if (fsLoaded) {
    File f = LittleFS.open(PATH, "r");
    String body = f.readString();
    f.close();
    g.load(body.c_str(), body.length(), fsErr, sizeof(fsErr));
    snprintf(status, n, "LittleFS: %s v%s", g.name(), g.version());
    return true;
  }

  if (embOk) {
    g.load(SPEC_EMBEDDED, SPEC_EMBEDDED_LEN, embErr, sizeof(embErr));
    snprintf(status, n, "embedded: %s v%s", g.name(), g.version());
    return true;
  }

  snprintf(status, n, "spec invalid: fs=%s, emb=%s", fsErr, embErr);
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
  if (p.begin("beachday", false)) {
    time_t now = time(nullptr);
    p.putULong64("rulesLast", (uint64_t)now);
    p.putUInt("rulesMtime", (uint32_t)now);
    p.end();
  }

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
