#include "dayspec.h"
#include <toml.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace day {

namespace { IconChecker g_iconChecker = nullptr; }
void setIconChecker(IconChecker fn) { g_iconChecker = fn; }
IconChecker g_iconCheckerAccessor() { return g_iconChecker; }

// ---------------------------------------------------------------------------
// Field names. Every input has a short name for the TOML and a canonical one;
// both work, so copy templates and conditions can use either.
// ---------------------------------------------------------------------------
namespace {

struct FieldDef { const char* shortName; const char* canonical; };
const FieldDef FIELDS[] = {
  { "tempMin",        "tempMinF" },
  { "tempMax",        "tempMaxF" },
  { "swing",          "tempSwingF" },
  { "rain",           "precipChanceMaxPct" },
  { "wind",           "windMaxMph" },
  { "aqi",            "aqiMax" },
  { "aqiMin",         "aqiMin" },
  { "humidityMin",    "humidityMinPct" },
  { "humidityMax",    "humidityMaxPct" },
  { "cloud",          "cloudCoverAvgPct" },
  { "sunHours",       "sunRunHours" },
  { "firstClearHour", "firstClearHour" },
};

double fieldOf(const Inputs& in, const char* canonical, bool& ok) {
  ok = true;
  if (!strcmp(canonical, "tempMinF"))           return in.tempMinF;
  if (!strcmp(canonical, "tempMaxF"))           return in.tempMaxF;
  if (!strcmp(canonical, "tempSwingF"))         return in.tempSwingF;
  if (!strcmp(canonical, "precipChanceMaxPct")) return in.precipChanceMaxPct;
  if (!strcmp(canonical, "windMaxMph"))         return in.windMaxMph;
  if (!strcmp(canonical, "aqiMax"))             return in.aqiMax;
  if (!strcmp(canonical, "aqiMin"))             return in.aqiMin;
  if (!strcmp(canonical, "humidityMinPct"))     return in.humidityMinPct;
  if (!strcmp(canonical, "humidityMaxPct"))     return in.humidityMaxPct;
  if (!strcmp(canonical, "cloudCoverAvgPct"))   return in.cloudCoverAvgPct;
  if (!strcmp(canonical, "sunRunHours"))        return in.sunRunHours;
  if (!strcmp(canonical, "firstClearHour")) {
    ok = in.hasFirstClearHour;
    return in.firstClearHour;
  }
  ok = false;
  return 0;
}

const char* canonicalOf(const char* name) {
  if (!name) return nullptr;
  for (const FieldDef& f : FIELDS) {
    if (!strcmp(name, f.shortName) || !strcmp(name, f.canonical)) return f.canonical;
  }
  return nullptr;
}

void cpy(char* dst, size_t n, const char* src) {
  if (!src) { dst[0] = '\0'; return; }
  snprintf(dst, n, "%s", src);
}

void upperCpy(char* dst, size_t n, const char* src) {
  size_t i = 0;
  if (src) for (; i + 1 < n && src[i]; i++) dst[i] = (char)toupper((unsigned char)src[i]);
  dst[i] = '\0';
}

// --- tomlc99 helpers. Every string it hands back is a fresh allocation, so
// these copy into a caller buffer and free immediately.
bool tStr(toml_table_t* t, const char* key, char* out, size_t n) {
  if (!t) return false;
  toml_datum_t d = toml_string_in(t, key);
  if (!d.ok) return false;
  cpy(out, n, d.u.s);
  free(d.u.s);
  return true;
}
bool tStrAt(toml_array_t* a, int i, char* out, size_t n) {
  toml_datum_t d = toml_string_at(a, i);
  if (!d.ok) return false;
  cpy(out, n, d.u.s);
  free(d.u.s);
  return true;
}
bool tNum(toml_table_t* t, const char* key, double& out) {
  if (!t) return false;
  toml_datum_t d = toml_double_in(t, key);
  if (d.ok) { out = d.u.d; return true; }
  d = toml_int_in(t, key);
  if (d.ok) { out = (double)d.u.i; return true; }
  return false;
}
bool tBool(toml_table_t* t, const char* key, bool& out) {
  if (!t) return false;
  toml_datum_t d = toml_bool_in(t, key);
  if (!d.ok) return false;
  out = d.u.b;
  return true;
}

// "tempMax >= 78" -> field, operator, number. The whole condition grammar,
// deliberately: three tokens, so no parser worth the name and no build step.
// An array rather than a table, so two conditions can name the same field -
// "tempMax >= 65", "tempMax < 75" is a band, which a table cannot express.
bool parseWhen(const char* expr, char* field, size_t fieldN, char* op, size_t opN, double& value) {
  if (!expr) return false;
  const char* p = expr;
  while (*p == ' ') p++;
  size_t k = 0;
  while (*p && *p != ' ' && !strchr("<>=!", *p) && k + 1 < fieldN) field[k++] = *p++;
  field[k] = '\0';
  if (k == 0) return false;
  while (*p == ' ') p++;
  k = 0;
  while (*p && strchr("<>=!", *p) && k + 1 < opN) op[k++] = *p++;
  op[k] = '\0';
  if (k == 0) return false;
  while (*p == ' ') p++;
  char* end = nullptr;
  value = strtod(p, &end);
  return end && end != p;
}

bool applyOp(const char* op, double v, double t) {
  if (!strcmp(op, ">="))  return v >= t;
  if (!strcmp(op, ">"))   return v > t;
  if (!strcmp(op, "<="))  return v <= t;
  if (!strcmp(op, "<"))   return v < t;
  if (!strcmp(op, "==") || !strcmp(op, "=")) return v == t;
  if (!strcmp(op, "!="))  return v != t;
  return false;
}

void hour12(int h, char* out, size_t n) {
  int d = h % 12; if (d == 0) d = 12;
  snprintf(out, n, "%d %s", d, h >= 12 ? "PM" : "AM");
}
void hourCompact(int h, char* out, size_t n) {
  int d = h % 12; if (d == 0) d = 12;
  snprintf(out, n, "%d%c", d, h >= 12 ? 'P' : 'A');
}

} // namespace

bool fieldValue(const Inputs& in, const char* name, double& v) {
  const char* c = canonicalOf(name);
  if (!c) return false;
  bool ok;
  v = fieldOf(in, c, ok);
  return ok;
}

void interpolate(const char* tmpl, const Inputs& in, double value, int degreesShort,
                 char* out, size_t n, const char* footerSuffix) {
  size_t o = 0;
  if (!tmpl) { out[0] = '\0'; return; }
  for (const char* p = tmpl; *p && o + 1 < n; ) {
    if (*p != '{') { out[o++] = *p++; continue; }
    const char* e = strchr(p, '}');
    if (!e) { out[o++] = *p++; continue; }
    char tok[32];
    size_t tl = (size_t)(e - p - 1); if (tl >= sizeof(tok)) tl = sizeof(tok) - 1;
    memcpy(tok, p + 1, tl); tok[tl] = '\0';
    char rep[64] = "";
    double v;
    if (!strcmp(tok, "value"))                snprintf(rep, sizeof(rep), "%d", (int)lround(value));
    else if (!strcmp(tok, "degreesShort"))    snprintf(rep, sizeof(rep), "%d", degreesShort);
    else if (!strcmp(tok, "firstClearHour"))  { if (in.hasFirstClearHour) hour12(in.firstClearHour, rep, sizeof(rep)); }
    else if (!strcmp(tok, "conditionSummary")) cpy(rep, sizeof(rep), in.conditionSummary);
    else if (!strcmp(tok, "sunsetLocal"))     cpy(rep, sizeof(rep), in.sunsetLocal);
    else if (!strcmp(tok, "weekdayName"))     cpy(rep, sizeof(rep), in.weekdayName);
    else if (!strcmp(tok, "footerSuffix"))    cpy(rep, sizeof(rep), footerSuffix ? footerSuffix : "");
    else if (!strcmp(tok, "sunWindow")) {
      if (in.sunRunStartHour >= 0) {
        char a[8], b[8];
        hourCompact(in.sunRunStartHour, a, sizeof(a));
        hourCompact(in.sunRunEndHour, b, sizeof(b));
        snprintf(rep, sizeof(rep), "%s-%s", a, b);
      }
    }
    else if (!strcmp(tok, "rainWindow")) {
      if (in.rainStartHour >= 0) {
        char a[8], b[8];
        hourCompact(in.rainStartHour, a, sizeof(a));
        if (in.rainEndHour == in.rainStartHour) snprintf(rep, sizeof(rep), "%s", a);
        else { hourCompact(in.rainEndHour, b, sizeof(b)); snprintf(rep, sizeof(rep), "%s-%s", a, b); }
      }
    }
    else if (fieldValue(in, tok, v))          snprintf(rep, sizeof(rep), "%d", (int)lround(v));
    for (const char* r = rep; *r && o + 1 < n; ) out[o++] = *r++;
    p = e + 1;
  }
  out[o] = '\0';
}

// ---------------------------------------------------------------------------

Spec::~Spec() { if (root_) toml_free(root_); }

const char* Spec::name() const { return nameBuf_; }
const char* Spec::version() const { return versionBuf_; }
const char* Spec::locationLabel() const { return locationBuf_; }

int Spec::outcomeCount() const {
  if (!root_) return 0;
  toml_array_t* a = toml_array_in(root_, "outcome");
  return a ? toml_array_nelem(a) : 0;
}

const char* Spec::outcomeId(int i) const {
  idBuf_[0] = '\0';
  if (!root_) return idBuf_;
  toml_array_t* a = toml_array_in(root_, "outcome");
  if (!a || i < 0 || i >= toml_array_nelem(a)) return idBuf_;
  tStr(toml_table_at(a, i), "id", idBuf_, sizeof(idBuf_));
  return idBuf_;
}

bool Spec::load(const char* text, size_t len, char* err, size_t errLen) {
  if (!text || len == 0) { snprintf(err, errLen, "empty rules file"); return false; }
  // toml_parse writes into its input; the tree it returns owns copies of every
  // key and value, so this scratch buffer can go as soon as parsing is done.
  char* scratch = (char*)malloc(len + 1);
  if (!scratch) { snprintf(err, errLen, "out of memory"); return false; }
  memcpy(scratch, text, len);
  scratch[len] = '\0';

  char perr[128] = "";
  toml_table_t* root = toml_parse(scratch, perr, sizeof(perr));
  free(scratch);
  if (!root) { snprintf(err, errLen, "toml: %s", perr); return false; }

  if (!validate(root, err, errLen)) { toml_free(root); return false; }

  if (root_) toml_free(root_);
  root_ = root;
  valid_ = true;
  tStr(root_, "name", nameBuf_, sizeof(nameBuf_));
  tStr(root_, "version", versionBuf_, sizeof(versionBuf_));
  tStr(toml_table_in(root_, "screen"), "location", locationBuf_, sizeof(locationBuf_));
  err[0] = '\0';
  return true;
}

// --- holidays ---------------------------------------------------------------
namespace {
int daysInMonth(int y, int m) {
  static const int d[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  if (m < 1 || m > 12) return 30;
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m - 1];
}
} // namespace

bool Spec::holidayFor(const Inputs& in, char* out, size_t n) const {
  out[0] = '\0';
  if (!root_ || in.month < 1) return false;
  toml_array_t* arr = toml_array_in(root_, "holiday");
  if (!arr) return false;
  for (int i = 0; i < toml_array_nelem(arr); i++) {
    toml_table_t* h = toml_table_at(arr, i);
    double month = 0;
    if (!tNum(h, "month", month) || (int)month != in.month) continue;

    double day = 0;
    if (tNum(h, "day", day)) {
      if ((int)day == in.dayOfMonth) { tStr(h, "name", out, n); return true; }
      continue;
    }
    // Floating: the nth given weekday of the month, or the last one.
    double wd = 0, nth = 0;
    if (!tNum(h, "weekday", wd) || !tNum(h, "nth", nth)) continue;
    int firstWd = (((in.weekday - (in.dayOfMonth - 1)) % 7) + 7) % 7;
    int firstOccurrence = 1 + (((int)wd - firstWd + 7) % 7);
    int target;
    if ((int)nth < 0) {
      target = firstOccurrence;
      while (target + 7 <= daysInMonth(in.year, in.month)) target += 7;
    } else {
      target = firstOccurrence + ((int)nth - 1) * 7;
    }
    if (target == in.dayOfMonth && target <= daysInMonth(in.year, in.month)) {
      tStr(h, "name", out, n);
      return true;
    }
  }
  return false;
}

// --- stats ------------------------------------------------------------------
namespace {
// Bands run top to bottom; the first whose max >= value wins. A band naming a
// `requires` field is skipped when that input is missing.
toml_table_t* pickBand(toml_array_t* bands, double value, const Inputs& in) {
  if (!bands) return nullptr;
  for (int i = 0; i < toml_array_nelem(bands); i++) {
    toml_table_t* b = toml_table_at(bands, i);
    double mx = 0;
    if (!tNum(b, "max", mx) || value > mx) continue;
    char req[24];
    if (tStr(b, "requires", req, sizeof(req))) { double t; if (!fieldValue(in, req, t)) continue; }
    return b;
  }
  return nullptr;
}
} // namespace

void Spec::resolveStat(toml_table_t* st, const Inputs& in, Stat& out) const {
  tStr(st, "id", out.id, sizeof(out.id));
  tStr(st, "label", out.label, sizeof(out.label));

  if (!tStr(st, "icon", out.icon, sizeof(out.icon))) {
    char f[24] = "";
    double v = 0;
    tStr(st, "icon_field", f, sizeof(f));
    fieldValue(in, f, v);
    toml_table_t* b = pickBand(toml_array_in(st, "icon_bands"), v, in);
    if (b) tStr(b, "icon", out.icon, sizeof(out.icon));
  }
  {
    char f[24] = "", fmt[80] = "";
    double v = 0;
    tStr(st, "value_field", f, sizeof(f));
    fieldValue(in, f, v);
    if (tStr(st, "value_format", fmt, sizeof(fmt))) interpolate(fmt, in, v, 0, out.value, sizeof(out.value));
    else {
      toml_table_t* b = pickBand(toml_array_in(st, "value_bands"), v, in);
      char text[48] = "";
      if (b) tStr(b, "text", text, sizeof(text));
      interpolate(text, in, v, 0, out.value, sizeof(out.value));
    }
  }
  {
    char f[24] = "", fmt[80] = "";
    double v = 0;
    if (tStr(st, "sub_field", f, sizeof(f))) {
      fieldValue(in, f, v);
      if (tStr(st, "sub_format", fmt, sizeof(fmt))) interpolate(fmt, in, v, 0, out.sub, sizeof(out.sub));
      else {
        toml_table_t* b = pickBand(toml_array_in(st, "sub_bands"), v, in);
        char text[48] = "";
        if (b) tStr(b, "text", text, sizeof(text));
        interpolate(text, in, v, 0, out.sub, sizeof(out.sub));
      }
    }
  }
  {
    char f[24] = "";
    double v = 0;
    tStr(st, "word_field", f, sizeof(f));
    fieldValue(in, f, v);
    toml_table_t* b = pickBand(toml_array_in(st, "word_bands"), v, in);
    if (b) tStr(b, "text", out.word, sizeof(out.word));
    toml_table_t* ov = toml_table_in(st, "word_override");
    if (ov) {
      char of[24] = "", op[4] = "";
      double t = 0, cur = 0;
      if (tStr(ov, "field", of, sizeof(of)) && tStr(ov, "op", op, sizeof(op)) &&
          tNum(ov, "value", t) && fieldValue(in, of, cur) && applyOp(op, cur, t)) {
        tStr(ov, "text", out.word, sizeof(out.word));
      }
    }
  }
}

// --- evaluate ---------------------------------------------------------------
namespace {
// All conditions in [outcome.when] must pass. `failed` records the first that
// did not, which is how sun_hat_day explains itself.
bool whenPasses(toml_table_t* outcome, const Inputs& in, char* failed, size_t failedLen) {
  if (failed) failed[0] = '\0';
  toml_array_t* when = toml_array_in(outcome, "when");
  if (!when) return true;              // the catch-all
  bool ok = true;
  for (int i = 0; i < toml_array_nelem(when); i++) {
    char expr[48], field[24], op[4];
    double target = 0, v = 0;
    if (!tStrAt(when, i, expr, sizeof(expr))) { ok = false; continue; }
    if (!parseWhen(expr, field, sizeof(field), op, sizeof(op), target)) { ok = false; continue; }
    if (!fieldValue(in, field, v) || !applyOp(op, v, target)) {
      ok = false;
      if (failed && !failed[0]) cpy(failed, failedLen, field);
    }
  }
  return ok;
}

uint32_t hashOf(const char* s) {
  uint32_t h = 2166136261u;
  for (; s && *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
  return h;
}
} // namespace

bool Spec::evaluate(const Inputs& in, Screen& out, const char* eyebrowOverride, bool tomorrow) const {
  if (!valid_ || !root_) return false;
  out = Screen{};
  out.tomorrow = tomorrow;

  toml_array_t* outcomes = toml_array_in(root_, "outcome");
  if (!outcomes) return false;

  // Which beach-day test failed, for the outcome that explains itself.
  char beachFailed[24] = "";
  double beachTempThreshold = 0;
  for (int i = 0; i < toml_array_nelem(outcomes); i++) {
    toml_table_t* o = toml_table_at(outcomes, i);
    bool isBeach = false;
    if (!tBool(o, "is_beach_day", isBeach) || !isBeach) continue;
    whenPasses(o, in, beachFailed, sizeof(beachFailed));
    // The temperature bar the near-miss copy counts down from.
    toml_array_t* when = toml_array_in(o, "when");
    for (int k = 0; when && k < toml_array_nelem(when); k++) {
      char expr[48], field[24], op[4];
      double val = 0;
      if (!tStrAt(when, k, expr, sizeof(expr))) continue;
      if (parseWhen(expr, field, sizeof(field), op, sizeof(op), val) && !strcmp(field, "tempMax"))
        beachTempThreshold = val;
    }
    break;
  }

  toml_table_t* chosen = nullptr;
  for (int i = 0; i < toml_array_nelem(outcomes); i++) {
    toml_table_t* o = toml_table_at(outcomes, i);
    if (whenPasses(o, in, nullptr, 0)) { chosen = o; break; }
  }
  if (!chosen) return false;

  Outcome& oc = out.outcome;
  tStr(chosen, "id", oc.id, sizeof(oc.id));

  // A condition owns a pool of variants, and one is chosen per day. The pool
  // is `names` (name-only variants, the quick case) followed by any
  // [[outcome.variant]] tables, which may override anything the outcome sets.
  // The pick is seeded by the date and the outcome id, so it holds all day,
  // differs tomorrow, and two outcomes do not rotate in lockstep. The board
  // redraws hourly; anything random would churn under the reader.
  toml_array_t* names = toml_array_in(chosen, "names");
  toml_array_t* variants = toml_array_in(chosen, "variant");
  const int nNames = names ? toml_array_nelem(names) : 0;
  const int nVariants = variants ? toml_array_nelem(variants) : 0;
  const int pool = nNames + nVariants;

  toml_table_t* v = nullptr;    // chosen variant table, if the pick landed on one
  char picked[40] = "";
  if (pool > 0) {
    int idx = (int)(((uint32_t)in.epochDay + hashOf(oc.id)) % (uint32_t)pool);
    if (idx < nNames) tStrAt(names, idx, picked, sizeof(picked));
    else {
      v = toml_table_at(variants, idx - nNames);
      tStr(v, "name", picked, sizeof(picked));
    }
  }

  // Anything a variant does not set falls back to the outcome, so shared
  // values are written once.
  auto pick = [&](const char* key, char* out2, size_t n2) -> bool {
    if (v && tStr(v, key, out2, n2)) return true;
    return tStr(chosen, key, out2, n2);
  };

  {
    char holiday[20] = "";
    if (holidayFor(in, holiday, sizeof(holiday)) && holiday[0]) {
      cpy(oc.holiday, sizeof(oc.holiday), holiday);
      snprintf(oc.title, sizeof(oc.title), "%s %s", holiday, picked);
    } else {
      cpy(oc.title, sizeof(oc.title), picked);
    }
  }

  pick("tagline", oc.tagline, sizeof(oc.tagline));
  pick("icon", oc.heroIcon, sizeof(oc.heroIcon));
  tBool(chosen, "is_beach_day", oc.isBeachDay);
  if (!pick("color", oc.heroColor, sizeof(oc.heroColor))) cpy(oc.heroColor, sizeof(oc.heroColor), "none");
  {
    char panel[8] = "";
    if (!pick("panel", panel, sizeof(panel)))
      tStr(toml_table_in(root_, "screen"), "hero_panel_default", panel, sizeof(panel));
    oc.heroLight = !strcmp(panel, "light");
  }

  // Wear: ids into the [wear] catalogue, so a label and icon are written once.
  {
    toml_table_t* catalog = toml_table_in(root_, "wear");
    toml_array_t* ids = (v ? toml_array_in(v, "wear") : nullptr);
    if (!ids) ids = toml_array_in(chosen, "wear");
    oc.wearCount = 0;
    for (int i = 0; ids && i < toml_array_nelem(ids) && oc.wearCount < 4; i++) {
      char id[24] = "";
      if (!tStrAt(ids, i, id, sizeof(id))) continue;
      toml_table_t* item = toml_table_in(catalog, id);
      if (!item) continue;
      tStr(item, "label", oc.wear[oc.wearCount].label, sizeof(oc.wear[0].label));
      tStr(item, "icon", oc.wear[oc.wearCount].icon, sizeof(oc.wear[0].icon));
      oc.wearCount++;
    }
  }

  cpy(oc.failedTest, sizeof(oc.failedTest), beachFailed);
  {
    int degreesShort = (int)lround(beachTempThreshold - in.tempMaxF);
    char tmpl[180] = "";
    bool got = false;
    toml_table_t* byFailed = toml_table_in(chosen, "also_when_failed");
    if (byFailed && beachFailed[0]) got = tStr(byFailed, beachFailed, tmpl, sizeof(tmpl));
    if (!got) {
      double within = 0;
      char near[180] = "";
      if (tNum(chosen, "also_near_beach_within", within) &&
          tStr(chosen, "also_near_beach", near, sizeof(near)) &&
          degreesShort > 0 && degreesShort <= (int)within) {
        cpy(tmpl, sizeof(tmpl), near);
        got = true;
      }
    }
    if (!got) pick("also", tmpl, sizeof(tmpl));
    interpolate(tmpl, in, 0, degreesShort, oc.alsoGrab, sizeof(oc.alsoGrab));
  }
  oc.hasFooterSuffix = pick("footer_suffix", oc.footerSuffix, sizeof(oc.footerSuffix));

  // Stats
  toml_array_t* stats = toml_array_in(root_, "stat");
  out.statCount = 0;
  for (int i = 0; stats && i < toml_array_nelem(stats) && out.statCount < 6; i++) {
    resolveStat(toml_table_at(stats, i), in, out.stats[out.statCount++]);
  }

  // Header / footer
  toml_table_t* screen = toml_table_in(root_, "screen");
  upperCpy(out.eyebrow, sizeof(out.eyebrow),
           (eyebrowOverride && eyebrowOverride[0]) ? eyebrowOverride : locationBuf_);
  cpy(out.weekday, sizeof(out.weekday), in.weekdayName);

  {
    char tmpl[128] = "";
    tStr(screen, "subline", tmpl, sizeof(tmpl));
    interpolate(tmpl, in, 0, 0, out.subline, sizeof(out.subline));
    if (tomorrow) {
      char t[sizeof(out.subline)];
      const char* s = out.subline; size_t o = 0;
      while (*s && o + 1 < sizeof(t)) {
        if (!strncmp(s, " this ", 6)) { const char* r = " in the "; while (*r && o + 1 < sizeof(t)) t[o++] = *r++; s += 6; }
        else t[o++] = *s++;
      }
      t[o] = '\0';
      cpy(out.subline, sizeof(out.subline), t);
    }
  }
  {
    char tmpl[128] = "";
    if (!tStr(screen, "footer", tmpl, sizeof(tmpl))) cpy(tmpl, sizeof(tmpl), "Sunset: {sunsetLocal}");
    interpolate(tmpl, in, 0, 0, out.footer, sizeof(out.footer), oc.hasFooterSuffix ? oc.footerSuffix : "");
  }
  return true;
}

} // namespace day
