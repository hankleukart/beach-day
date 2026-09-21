#include "dayspec.h"
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cstdarg>

namespace day {

// The eyebrow is set in caps by design, but the label can come from a portal
// entry or a geocoder, which return "Cambridge". Uppercase it here rather than
// asking whoever types it to hold shift.
static void upperCpy(char* dst, size_t n, const char* src) {
  size_t i = 0;
  if (src) for (; i + 1 < n && src[i]; i++) dst[i] = (char)toupper((unsigned char)src[i]);
  dst[i] = '\0';
}

static void cpy(char* dst, size_t n, const char* src) {
  if (!src) { dst[0] = '\0'; return; }
  snprintf(dst, n, "%s", src);
}

namespace { IconChecker g_iconChecker = nullptr; }
void setIconChecker(IconChecker fn) { g_iconChecker = fn; }

bool fieldValue(const Inputs& in, const char* f, double& v) {
  struct { const char* name; double val; bool ok; } table[] = {
    { "tempMinF", in.tempMinF, true }, { "tempMaxF", in.tempMaxF, true }, { "tempSwingF", in.tempSwingF, true },
    { "precipChanceMaxPct", in.precipChanceMaxPct, true }, { "windMaxMph", in.windMaxMph, true },
    { "aqiMax", in.aqiMax, true }, { "aqiMin", in.aqiMin, true },
    { "humidityMinPct", in.humidityMinPct, true }, { "humidityMaxPct", in.humidityMaxPct, true },
    { "cloudCoverAvgPct", in.cloudCoverAvgPct, true },
    { "sunRunHours", in.sunRunHours, true },
    { "firstClearHour", (double)in.firstClearHour, in.hasFirstClearHour },
  };
  for (auto& t : table) if (strcmp(t.name, f) == 0) { v = t.val; return t.ok; }
  return false;
}

static void hour12(int h, char* out, size_t n) {
  int d = h % 12; if (d == 0) d = 12;
  snprintf(out, n, "%d %s", d, h >= 12 ? "PM" : "AM");
}

// Compact form for the interval: 13 -> "1P", 9 -> "9A". Space is tight in a
// five-column strip, and the pattern reads fine next to a dash.
static void hourCompact(int h, char* out, size_t n) {
  int d = h % 12; if (d == 0) d = 12;
  snprintf(out, n, "%d%c", d, h >= 12 ? 'P' : 'A');
}

void interpolate(const char* tmpl, const Inputs& in, double value, int degreesShort, char* out, size_t n,
                 const char* footerSuffix) {
  size_t o = 0;
  for (const char* p = tmpl; *p && o + 1 < n; ) {
    if (*p != '{') { out[o++] = *p++; continue; }
    const char* e = strchr(p, '}');
    if (!e) { out[o++] = *p++; continue; }
    char tok[32];
    size_t tl = (size_t)(e - p - 1); if (tl >= sizeof(tok)) tl = sizeof(tok) - 1;
    memcpy(tok, p + 1, tl); tok[tl] = '\0';
    char rep[48] = "";
    double v;
    if (strcmp(tok, "value") == 0)              snprintf(rep, sizeof(rep), "%d", (int)lround(value));
    else if (strcmp(tok, "degreesShort") == 0)  snprintf(rep, sizeof(rep), "%d", degreesShort);
    else if (strcmp(tok, "firstClearHour") == 0) { if (in.hasFirstClearHour) hour12(in.firstClearHour, rep, sizeof(rep)); }
    else if (strcmp(tok, "rainWindow") == 0) {
      if (in.rainStartHour >= 0) {
        char a[8], b[8];
        hourCompact(in.rainStartHour, a, sizeof(a));
        if (in.rainEndHour == in.rainStartHour) snprintf(rep, sizeof(rep), "%s", a);
        else { hourCompact(in.rainEndHour, b, sizeof(b)); snprintf(rep, sizeof(rep), "%s-%s", a, b); }
      }
    }
    else if (strcmp(tok, "sunWindow") == 0) {
      if (in.sunRunStartHour >= 0) {
        char a[8], b[8];
        hourCompact(in.sunRunStartHour, a, sizeof(a));
        hourCompact(in.sunRunEndHour, b, sizeof(b));
        snprintf(rep, sizeof(rep), "%s-%s", a, b);
      }
    }
    else if (strcmp(tok, "conditionSummary") == 0) cpy(rep, sizeof(rep), in.conditionSummary);
    else if (strcmp(tok, "sunsetLocal") == 0)  cpy(rep, sizeof(rep), in.sunsetLocal);
    else if (strcmp(tok, "footerSuffix") == 0) cpy(rep, sizeof(rep), footerSuffix ? footerSuffix : "");
    else if (strcmp(tok, "weekdayName") == 0)  cpy(rep, sizeof(rep), in.weekdayName);
    else if (fieldValue(in, tok, v))            snprintf(rep, sizeof(rep), "%d", (int)lround(v));
    for (const char* r = rep; *r && o + 1 < n; ) out[o++] = *r++;
    p = e + 1;
  }
  out[o] = '\0';
}

// ---------------------------------------------------------------------------

// Every input name the rules may reference. A typo here is the nastiest kind
// of bug: an unknown field makes its condition silently never pass, so a day
// quietly resolves to the wrong outcome rather than failing loudly.
static bool isKnownField(const char* f) {
  Inputs probe;
  probe.hasFirstClearHour = true;   // so the optional field resolves too
  double v;
  return f && f[0] && fieldValue(probe, f, v);
}

static bool isKnownOp(const char* op) {
  static const char* ops[] = { ">=", ">", "<=", "<", "==", "!=" };
  for (auto o : ops) if (op && strcmp(op, o) == 0) return true;
  return false;
}

namespace {

struct Validator {
  JsonObjectConst root;
  char* err; size_t n;
  bool fail(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(err, n, fmt, ap); va_end(ap);
    return false;
  }
  // Fixed-size destination buffers mean an over-long string would be silently
  // clipped on screen; catch it at load instead.
  bool fits(const char* what, const char* who, const char* value, size_t cap) {
    if (value && strlen(value) >= cap) return fail("%s: %s too long (max %u)", who, what, (unsigned)cap - 1);
    return true;
  }
  bool knownIcon(const char* who, const char* what, const char* name) {
    if (!name || !name[0]) return fail("%s: %s missing", who, what);
    bool listed = false;
    for (JsonVariantConst i : root["icons"].as<JsonArrayConst>())
      if (strcmp(i | "", name) == 0) { listed = true; break; }
    if (!listed) return fail("%s: %s '%s' not in icons[]", who, what, name);
    if (g_iconChecker && !g_iconChecker(name)) return fail("%s: no icon art for '%s'", who, name);
    return true;
  }
  bool condition(const char* who, JsonObjectConst c) {
    const char* f = c["field"] | "";
    if (!isKnownField(f)) return fail("%s: unknown field '%s'", who, f);
    if (!isKnownOp(c["op"] | "")) return fail("%s: bad op '%s'", who, c["op"] | "");
    if (!c["value"].is<double>() && !c["value"].is<int>()) return fail("%s: %s needs a numeric value", who, f);
    return true;
  }
  bool bands(const char* who, JsonArrayConst b, bool wantIcon) {
    if (b.size() == 0) return fail("%s: empty bands", who);
    double prev = -1e18;
    size_t i = 0;
    for (JsonObjectConst e : b) {
      if (!e["max"].is<double>() && !e["max"].is<int>()) return fail("%s: band %u has no max", who, (unsigned)i);
      double m = e["max"] | 0.0;
      if (m < prev) return fail("%s: band %u max goes backwards", who, (unsigned)i);
      prev = m;
      const char* req = e["requires"] | (const char*)nullptr;
      if (req && !isKnownField(req)) return fail("%s: band requires unknown '%s'", who, req);
      if (wantIcon) { if (!knownIcon(who, "band icon", e["icon"] | "")) return false; }
      else if (!e["text"].is<const char*>()) return fail("%s: band %u has no text", who, (unsigned)i);
      // A gated last band can leave a value matching nothing at all.
      if (++i == b.size() && req) return fail("%s: last band must not have 'requires'", who);
    }
    return true;
  }
};

} // namespace

bool Spec::validate(JsonObjectConst root, char* err, size_t errLen) const {
  Validator v{ root, err, errLen };

  JsonArrayConst outcomes = root["outcomes"];
  if (outcomes.size() == 0) return v.fail("no outcomes");
  if (root["icons"].as<JsonArrayConst>().size() == 0) return v.fail("no icons[] to check names against");

  size_t idx = 0;
  for (JsonObjectConst o : outcomes) {
    const char* id = o["id"] | "";
    if (!id[0]) return v.fail("outcome %u has no id", (unsigned)idx);
    if (!v.fits("id", id, id, sizeof(Outcome::id))) return false;
    for (JsonObjectConst other : outcomes) {
      if (other == o) break;
      if (strcmp(other["id"] | "", id) == 0) return v.fail("duplicate outcome id '%s'", id);
    }

    JsonArrayConst title = o["title"];
    if (title.size() == 0 || title.size() > 3) return v.fail("%s: title needs 1-3 lines", id);
    for (JsonVariantConst t : title) if (!v.fits("title line", id, t | "", sizeof(Outcome::title[0]))) return false;
    if (!v.fits("tagline", id, o["tagline"] | "", sizeof(Outcome::tagline))) return false;
    if (!v.knownIcon(id, "heroIcon", o["heroIcon"] | "")) return false;

    JsonObjectConst when = o["when"];
    if (when.isNull()) return v.fail("%s: no when", id);
    for (JsonObjectConst c : when["all"].as<JsonArrayConst>()) if (!v.condition(id, c)) return false;
    for (JsonObjectConst c : when["any"].as<JsonArrayConst>()) if (!v.condition(id, c)) return false;

    JsonArrayConst wear = o["wear"];
    if (wear.size() != 4) return v.fail("%s: needs exactly four wear items", id);
    for (JsonObjectConst w : wear) {
      if (!v.fits("wear label", id, w["label"] | "", sizeof(Wear::label))) return false;
      if (!v.knownIcon(id, "wear icon", w["icon"] | "")) return false;
    }

    JsonVariantConst ag = o["alsoGrab"];
    if (ag.is<const char*>()) { if (!v.fits("alsoGrab", id, ag, sizeof(Outcome::alsoGrab))) return false; }
    else if (ag.is<JsonObjectConst>()) {
      for (JsonPairConst kv : ag["byFailedTest"].as<JsonObjectConst>()) {
        if (!isKnownField(kv.key().c_str())) return v.fail("%s: byFailedTest '%s' is not a field", id, kv.key().c_str());
        if (!v.fits("byFailedTest line", id, kv.value() | "", sizeof(Outcome::alsoGrab))) return false;
      }
      if (!ag["default"].is<const char*>()) return v.fail("%s: alsoGrab needs a default", id);
      if (!v.fits("alsoGrab default", id, ag["default"], sizeof(Outcome::alsoGrab))) return false;
    } else return v.fail("%s: alsoGrab must be text or an object", id);

    if (o["footerSuffix"].is<const char*>() && !v.fits("footerSuffix", id, o["footerSuffix"], sizeof(Outcome::footerSuffix))) return false;
    if (o["heroPanel"].is<const char*>()) {
      const char* hp = o["heroPanel"];
      if (strcmp(hp, "light") != 0 && strcmp(hp, "dark") != 0)
        return v.fail("%s: heroPanel must be light or dark", id);
    }
    if (o["heroColor"].is<const char*>()) {
      static const char* inks[] = { "none", "yellow", "blue", "red", "green" };
      const char* hc = o["heroColor"];
      bool ok = false;
      for (auto k : inks) if (strcmp(hc, k) == 0) { ok = true; break; }
      if (!ok) return v.fail("%s: heroColor '%s' is not an available ink", id, hc);
    }
    idx++;
  }

  // The last outcome has to catch everything, or some day matches nothing.
  JsonObjectConst last = outcomes[outcomes.size() - 1];
  bool catchAll = last["isFallback"] | false;
  if (!catchAll) {
    JsonObjectConst w = last["when"];
    catchAll = w["all"].as<JsonArrayConst>().size() == 0 && !w["any"].is<JsonArrayConst>();
  }
  if (!catchAll) return v.fail("last outcome '%s' is not a catch-all", last["id"] | "?");

  JsonArrayConst stats = root["stats"];
  if (stats.size() == 0) return v.fail("no stats");
  if (stats.size() > 6) return v.fail("more than six stats");
  for (JsonObjectConst st : stats) {
    const char* id = st["id"] | "";
    if (!id[0]) return v.fail("a stat has no id");
    if (!v.fits("stat id", id, id, sizeof(Stat::id))) return false;
    if (!v.fits("stat label", id, st["label"] | "", sizeof(Stat::label))) return false;

    if (st["icon"].is<const char*>()) { if (!v.knownIcon(id, "icon", st["icon"])) return false; }
    else {
      if (!isKnownField(st["icon"]["field"] | "")) return v.fail("%s: icon field unknown", id);
      if (!v.bands(id, st["icon"]["bands"], true)) return false;
    }

    JsonObjectConst val = st["value"];
    if (val.isNull()) return v.fail("%s: no value", id);
    if (!isKnownField(val["field"] | "")) return v.fail("%s: value field unknown", id);
    bool hasFormat = val["format"].is<const char*>();
    bool hasBands = val["bands"].is<JsonArrayConst>();
    if (hasFormat == hasBands) return v.fail("%s: value needs format or bands, not both", id);
    if (hasBands && !v.bands(id, val["bands"], false)) return false;

    JsonObjectConst word = st["word"];
    if (word.isNull()) return v.fail("%s: no word", id);
    if (!isKnownField(word["field"] | "")) return v.fail("%s: word field unknown", id);
    if (!v.bands(id, word["bands"], false)) return false;

    for (JsonObjectConst ov : st["overrides"].as<JsonArrayConst>()) {
      if (!v.condition(id, ov["if"])) return false;
      if (!v.fits("override text", id, ov["text"] | "", sizeof(Stat::word))) return false;
    }
  }
  err[0] = '\0';
  return true;
}

bool Spec::load(const char* json, size_t len, char* err, size_t errLen) {
  JsonDocument fresh;
  DeserializationError e = deserializeJson(fresh, json, len);
  if (e) { snprintf(err, errLen, "json: %s", e.c_str()); return false; }
  if (!validate(fresh.as<JsonObjectConst>(), err, errLen)) return false;
  doc_ = fresh;
  valid_ = true;
  err[0] = '\0';
  return true;
}

const char* Spec::name() const { return doc_["name"] | ""; }
const char* Spec::version() const { return doc_["version"] | ""; }
const char* Spec::locationLabel() const { return doc_["location"]["label"] | ""; }
int Spec::outcomeCount() const { return (int)doc_["outcomes"].as<JsonArrayConst>().size(); }
const char* Spec::outcomeId(int i) const { return doc_["outcomes"][i]["id"] | ""; }

bool Spec::cond(JsonObjectConst c, const Inputs& in) const {
  double v;
  if (!fieldValue(in, c["field"] | "", v)) return false;   // unknown or null field never passes
  double t = c["value"] | 0.0;
  const char* op = c["op"] | "";
  if (!strcmp(op, ">=")) return v >= t;
  if (!strcmp(op, ">"))  return v > t;
  if (!strcmp(op, "<=")) return v <= t;
  if (!strcmp(op, "<"))  return v < t;
  if (!strcmp(op, "==")) return v == t;
  if (!strcmp(op, "!=")) return v != t;
  return false;
}

// `all` must every pass (empty = true); `any` needs one (empty = false).
// Records the first failing `all` field so sun_hat_day can explain itself.
bool Spec::when(JsonObjectConst w, const Inputs& in, char* failed, size_t failedLen) const {
  if (failed) failed[0] = '\0';
  bool ok = true;
  if (w["all"].is<JsonArrayConst>()) {
    for (JsonObjectConst c : w["all"].as<JsonArrayConst>()) {
      if (!cond(c, in)) { ok = false; if (failed && !failed[0]) cpy(failed, failedLen, c["field"] | ""); }
    }
  }
  if (w["any"].is<JsonArrayConst>()) {
    bool anyOk = false;
    for (JsonObjectConst c : w["any"].as<JsonArrayConst>()) if (cond(c, in)) { anyOk = true; break; }
    if (!anyOk) ok = false;
  }
  return ok;
}

double Spec::beachTempThreshold() const {
  for (JsonObjectConst o : doc_["outcomes"].as<JsonArrayConst>()) {
    if (!(o["isBeachDay"] | false)) continue;
    for (JsonObjectConst c : o["when"]["all"].as<JsonArrayConst>()) {
      if (!strcmp(c["field"] | "", "tempMaxF")) return c["value"] | 0.0;
    }
  }
  return 0;
}

void Spec::resolveAlsoGrab(JsonVariantConst v, const Inputs& in, const char* failedTest, char* out, size_t n) const {
  int degreesShort = (int)lround(beachTempThreshold() - in.tempMaxF);
  if (v.is<const char*>()) { interpolate(v.as<const char*>(), in, 0, degreesShort, out, n); return; }
  JsonObjectConst o = v.as<JsonObjectConst>();
  const char* tmpl = nullptr;
  if (o["byFailedTest"].is<JsonObjectConst>() && failedTest && failedTest[0]) {
    tmpl = o["byFailedTest"][failedTest] | (const char*)nullptr;
  }
  if (!tmpl && o["whenNearBeachThreshold"].is<const char*>()) {
    int within = o["nearBeachThresholdWithinF"] | 0;
    if (degreesShort > 0 && degreesShort <= within) tmpl = o["whenNearBeachThreshold"];
  }
  if (!tmpl) tmpl = o["default"] | "";
  interpolate(tmpl, in, 0, degreesShort, out, n);
}

// bands: first entry whose max >= value; a band with "requires" is skipped
// when that input is missing.
static JsonObjectConst pickBand(JsonArrayConst bands, double value, const Inputs& in) {
  for (JsonObjectConst b : bands) {
    if (value > (b["max"] | 1e9)) continue;
    const char* req = b["requires"] | (const char*)nullptr;
    if (req) { double tmp; if (!fieldValue(in, req, tmp)) continue; }
    return b;
  }
  return JsonObjectConst();
}

void Spec::resolveStat(JsonObjectConst st, const Inputs& in, Stat& out) const {
  cpy(out.id, sizeof(out.id), st["id"] | "");
  cpy(out.label, sizeof(out.label), st["label"] | "");

  // icon: literal or banded
  if (st["icon"].is<const char*>()) cpy(out.icon, sizeof(out.icon), st["icon"]);
  else {
    double v = 0; fieldValue(in, st["icon"]["field"] | "", v);
    JsonObjectConst b = pickBand(st["icon"]["bands"], v, in);
    cpy(out.icon, sizeof(out.icon), b["icon"] | "");
  }
  // value: format string or banded text
  {
    JsonObjectConst spec = st["value"];
    double v = 0; fieldValue(in, spec["field"] | "", v);
    if (spec["format"].is<const char*>()) interpolate(spec["format"], in, v, 0, out.value, sizeof(out.value));
    else {
      JsonObjectConst b = pickBand(spec["bands"], v, in);
      interpolate(b["text"] | "", in, v, 0, out.value, sizeof(out.value));
    }
  }
  // word: banded, then overrides
  {
    JsonObjectConst spec = st["word"];
    double v = 0; fieldValue(in, spec["field"] | "", v);
    JsonObjectConst b = pickBand(spec["bands"], v, in);
    cpy(out.word, sizeof(out.word), b["text"] | "");
    for (JsonObjectConst ov : st["overrides"].as<JsonArrayConst>()) {
      if (cond(ov["if"], in)) { cpy(out.word, sizeof(out.word), ov["text"] | out.word); break; }
    }
  }
}

bool Spec::evaluate(const Inputs& in, Screen& out, const char* eyebrowOverride, bool tomorrow) const {
  if (!valid_) return false;
  out = Screen{};
  out.tomorrow = tomorrow;

  // Remember which beach_day test failed, for sun_hat_day's copy.
  char beachFailed[24] = "";
  for (JsonObjectConst o : doc_["outcomes"].as<JsonArrayConst>()) {
    if (o["isBeachDay"] | false) { when(o["when"], in, beachFailed, sizeof(beachFailed)); break; }
  }

  JsonObjectConst chosen;
  for (JsonObjectConst o : doc_["outcomes"].as<JsonArrayConst>()) {
    if (when(o["when"], in, nullptr, 0)) { chosen = o; break; }
  }
  if (chosen.isNull()) return false;

  Outcome& oc = out.outcome;
  cpy(oc.id, sizeof(oc.id), chosen["id"]);
  oc.titleLines = 0;
  for (JsonVariantConst t : chosen["title"].as<JsonArrayConst>()) {
    if (oc.titleLines >= 3) break;
    cpy(oc.title[oc.titleLines++], sizeof(oc.title[0]), t | "");
  }
  cpy(oc.tagline, sizeof(oc.tagline), chosen["tagline"] | "");
  cpy(oc.heroIcon, sizeof(oc.heroIcon), chosen["heroIcon"] | "");
  oc.isBeachDay = chosen["isBeachDay"] | false;
  {
    const char* hp = chosen["heroPanel"] | (const char*)nullptr;
    if (!hp) hp = doc_["screen"]["heroPanel"]["default"] | "dark";
    oc.heroLight = (strcmp(hp, "light") == 0);
  }
  cpy(oc.heroColor, sizeof(oc.heroColor), chosen["heroColor"] | "none");
  oc.wearCount = 0;
  for (JsonObjectConst w : chosen["wear"].as<JsonArrayConst>()) {
    if (oc.wearCount >= 4) break;
    cpy(oc.wear[oc.wearCount].label, sizeof(oc.wear[0].label), w["label"] | "");
    cpy(oc.wear[oc.wearCount].icon,  sizeof(oc.wear[0].icon),  w["icon"]  | "");
    oc.wearCount++;
  }
  cpy(oc.failedTest, sizeof(oc.failedTest), beachFailed);
  resolveAlsoGrab(chosen["alsoGrab"], in, beachFailed, oc.alsoGrab, sizeof(oc.alsoGrab));
  oc.hasFooterSuffix = chosen["footerSuffix"].is<const char*>();
  cpy(oc.footerSuffix, sizeof(oc.footerSuffix), oc.hasFooterSuffix ? chosen["footerSuffix"].as<const char*>() : "");

  out.statCount = 0;
  for (JsonObjectConst st : doc_["stats"].as<JsonArrayConst>()) {
    if (out.statCount >= 6) break;
    resolveStat(st, in, out.stats[out.statCount++]);
  }

  // Header / footer strings from screen.sections
  JsonObjectConst sec = doc_["screen"]["sections"];
  upperCpy(out.eyebrow, sizeof(out.eyebrow), (eyebrowOverride && eyebrowOverride[0]) ? eyebrowOverride : locationLabel());
  cpy(out.weekday, sizeof(out.weekday), in.weekdayName);
  cpy(out.wearHeading, sizeof(out.wearHeading), sec["wearHeading"] | "WEAR TODAY");

  char corner[96];
  interpolate(sec["cornerStats"] | "", in, 0, 0, corner, sizeof(corner));
  char* nl = strchr(corner, '\n');
  if (nl) { *nl = '\0'; cpy(out.corner1, sizeof(out.corner1), corner); cpy(out.corner2, sizeof(out.corner2), nl + 1); }
  else cpy(out.corner1, sizeof(out.corner1), corner);

  interpolate(sec["subline"] | "", in, 0, 0, out.subline, sizeof(out.subline));
  if (tomorrow) {
    // "74° this morning" reads wrong for tomorrow.
    char tmp[sizeof(out.subline)];
    const char* s = out.subline; size_t o = 0;
    while (*s && o + 1 < sizeof(tmp)) {
      if (!strncmp(s, " this ", 6)) { const char* r = " in the "; while (*r && o + 1 < sizeof(tmp)) tmp[o++] = *r++; s += 6; }
      else tmp[o++] = *s++;
    }
    tmp[o] = '\0';
    cpy(out.subline, sizeof(out.subline), tmp);
  }

  // The footer is an ordinary template. {footerSuffix} is only substituted if
  // the template asks for it, so a spec can drop the per-outcome tail simply
  // by not mentioning it.
  interpolate(sec["footer"] | "Sunset: {sunsetLocal}", in, 0, 0, out.footer, sizeof(out.footer),
              oc.hasFooterSuffix ? oc.footerSuffix : "");
  return true;
}

} // namespace day
