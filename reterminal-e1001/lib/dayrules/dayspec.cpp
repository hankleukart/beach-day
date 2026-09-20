#include "dayspec.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace day {

static void cpy(char* dst, size_t n, const char* src) {
  if (!src) { dst[0] = '\0'; return; }
  snprintf(dst, n, "%s", src);
}

bool fieldValue(const Inputs& in, const char* f, double& v) {
  struct { const char* name; double val; bool ok; } table[] = {
    { "tempMinF", in.tempMinF, true }, { "tempMaxF", in.tempMaxF, true }, { "tempSwingF", in.tempSwingF, true },
    { "precipChanceMaxPct", in.precipChanceMaxPct, true }, { "windMaxMph", in.windMaxMph, true },
    { "aqiMax", in.aqiMax, true }, { "aqiMin", in.aqiMin, true },
    { "humidityMinPct", in.humidityMinPct, true }, { "humidityMaxPct", in.humidityMaxPct, true },
    { "cloudCoverAvgPct", in.cloudCoverAvgPct, true },
    { "firstClearHour", (double)in.firstClearHour, in.hasFirstClearHour },
  };
  for (auto& t : table) if (strcmp(t.name, f) == 0) { v = t.val; return t.ok; }
  return false;
}

static void hour12(int h, char* out, size_t n) {
  int d = h % 12; if (d == 0) d = 12;
  snprintf(out, n, "%d %s", d, h >= 12 ? "PM" : "AM");
}

void interpolate(const char* tmpl, const Inputs& in, double value, int degreesShort, char* out, size_t n) {
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
    else if (strcmp(tok, "conditionSummary") == 0) cpy(rep, sizeof(rep), in.conditionSummary);
    else if (strcmp(tok, "sunsetLocal") == 0)  cpy(rep, sizeof(rep), in.sunsetLocal);
    else if (strcmp(tok, "weekdayName") == 0)  cpy(rep, sizeof(rep), in.weekdayName);
    else if (fieldValue(in, tok, v))            snprintf(rep, sizeof(rep), "%d", (int)lround(v));
    for (const char* r = rep; *r && o + 1 < n; ) out[o++] = *r++;
    p = e + 1;
  }
  out[o] = '\0';
}

// ---------------------------------------------------------------------------

bool Spec::load(const char* json, size_t len, char* err, size_t errLen) {
  JsonDocument fresh;
  DeserializationError e = deserializeJson(fresh, json, len);
  if (e) { snprintf(err, errLen, "json: %s", e.c_str()); return false; }
  JsonArrayConst outcomes = fresh["outcomes"];
  if (outcomes.size() == 0) { snprintf(err, errLen, "no outcomes"); return false; }
  JsonObjectConst last = outcomes[outcomes.size() - 1];
  bool lastCatchesAll = last["isFallback"] | false;
  if (!lastCatchesAll) {
    JsonArrayConst all = last["when"]["all"];
    lastCatchesAll = last["when"].is<JsonObjectConst>() && all.isNull() == false && all.size() == 0;
  }
  if (!lastCatchesAll) { snprintf(err, errLen, "last outcome is not a catch-all"); return false; }
  for (JsonObjectConst o : outcomes) {
    if (!o["id"].is<const char*>()) { snprintf(err, errLen, "outcome without id"); return false; }
    if (o["wear"].as<JsonArrayConst>().size() != 4) { snprintf(err, errLen, "%s: needs exactly four wear items", o["id"] | "?"); return false; }
  }
  if (fresh["stats"].as<JsonArrayConst>().size() == 0) { snprintf(err, errLen, "no stats"); return false; }
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
  cpy(out.eyebrow, sizeof(out.eyebrow), (eyebrowOverride && eyebrowOverride[0]) ? eyebrowOverride : locationLabel());
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

  // footer: the template's literal prefix, then the ternary the spec describes
  const char* ft = sec["footer"] | "Sun goes down at {sunsetLocal}";
  const char* brace = strchr(ft, '{');
  char prefix[48];
  size_t pl = brace ? (size_t)(brace - ft) : strlen(ft);
  if (pl >= sizeof(prefix)) pl = sizeof(prefix) - 1;
  memcpy(prefix, ft, pl); prefix[pl] = '\0';
  if (oc.hasFooterSuffix && oc.footerSuffix[0]) snprintf(out.footer, sizeof(out.footer), "%s%s \xE2\x80\x94 %s", prefix, in.sunsetLocal, oc.footerSuffix);
  else snprintf(out.footer, sizeof(out.footer), "%s%s.", prefix, in.sunsetLocal);
  return true;
}

} // namespace day
