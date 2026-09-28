// Everything that must be true of a rules file before it is allowed to replace
// a working one. The failure modes worth catching are the silent ones: a
// mistyped field name makes its condition never pass, so the day quietly
// resolves to the wrong outcome with nothing in the log.
#include "dayspec.h"
#include <toml.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace day {
namespace {



bool vStr(toml_table_t* t, const char* key, char* out, size_t n) {
  if (!t) return false;
  toml_datum_t d = toml_string_in(t, key);
  if (!d.ok) return false;
  snprintf(out, n, "%s", d.u.s);
  free(d.u.s);
  return true;
}
bool vStrAt(toml_array_t* a, int i, char* out, size_t n) {
  toml_datum_t d = toml_string_at(a, i);
  if (!d.ok) return false;
  snprintf(out, n, "%s", d.u.s);
  free(d.u.s);
  return true;
}
bool vNum(toml_table_t* t, const char* key, double& out) {
  if (!t) return false;
  toml_datum_t d = toml_double_in(t, key);
  if (d.ok) { out = d.u.d; return true; }
  d = toml_int_in(t, key);
  if (d.ok) { out = (double)d.u.i; return true; }
  return false;
}
bool vHas(toml_table_t* t, const char* key) {
  if (!t) return false;
  toml_datum_t s = toml_string_in(t, key);
  if (s.ok) { free(s.u.s); return true; }
  if (toml_int_in(t, key).ok || toml_double_in(t, key).ok || toml_bool_in(t, key).ok) return true;
  return toml_table_in(t, key) || toml_array_in(t, key);
}

// The engine's own parser, duplicated here so validation matches evaluation.
bool vParseWhen(const char* expr, char* field, size_t fieldN, char* op, size_t opN, double& value) {
  const char* p = expr;
  while (*p == ' ') p++;
  size_t k = 0;
  while (*p && *p != ' ' && !strchr("<>=!", *p) && k + 1 < fieldN) field[k++] = *p++;
  field[k] = '\0';
  if (!k) return false;
  while (*p == ' ') p++;
  k = 0;
  while (*p && strchr("<>=!", *p) && k + 1 < opN) op[k++] = *p++;
  op[k] = '\0';
  if (!k) return false;
  while (*p == ' ') p++;
  char* end = nullptr;
  value = strtod(p, &end);
  return end && end != p;
}

// A condition names a field the operator can also be read from; still used
// where only the operator matters.
bool vParseCond(const char* expr, char* op, size_t opN, double& value) {
  char field[24];
  return vParseWhen(expr, field, sizeof(field), op, opN, value);
}
bool vKnownOp(const char* op) {
  static const char* ops[] = { ">=", ">", "<=", "<", "==", "=", "!=" };
  for (auto o : ops) if (!strcmp(op, o)) return true;
  return false;
}
bool vKnownField(const char* f) {
  Inputs probe;
  probe.hasFirstClearHour = true;   // so the optional input resolves too
  double v;
  return f && f[0] && fieldValue(probe, f, v);
}

struct V {
  toml_table_t* root;
  char* err; size_t n;
  IconChecker checker;
  bool fail(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(err, n, fmt, ap); va_end(ap);
    return false;
  }
  bool fits(const char* what, const char* who, const char* value, size_t cap) {
    if (value && strlen(value) >= cap) return fail("%s: %s too long (max %u)", who, what, (unsigned)cap - 1);
    return true;
  }
  bool knownIcon(const char* who, const char* what, const char* nameStr) {
    if (!nameStr || !nameStr[0]) return fail("%s: %s missing", who, what);
    toml_array_t* icons = toml_array_in(root, "icons");
    if (!icons) return fail("no icons list to check names against");
    for (int i = 0; i < toml_array_nelem(icons); i++) {
      char got[24];
      if (vStrAt(icons, i, got, sizeof(got)) && !strcmp(got, nameStr)) {
        if (checker && !checker(nameStr)) return fail("%s: no icon art for '%s'", who, nameStr);
        return true;
      }
    }
    return fail("%s: %s '%s' is not in icons", who, what, nameStr);
  }
  bool bands(const char* who, toml_array_t* b, bool wantIcon) {
    if (!b || toml_array_nelem(b) == 0) return fail("%s: empty bands", who);
    double prev = -1e18;
    int count = toml_array_nelem(b);
    for (int i = 0; i < count; i++) {
      toml_table_t* e = toml_table_at(b, i);
      if (!e) return fail("%s: band %d is not a table", who, i);
      double mx = 0;
      if (!vNum(e, "max", mx)) return fail("%s: band %d has no max", who, i);
      if (mx < prev) return fail("%s: band %d max goes backwards", who, i);
      prev = mx;
      char req[24];
      bool gated = vStr(e, "requires", req, sizeof(req));
      if (gated && !vKnownField(req)) return fail("%s: band requires unknown '%s'", who, req);
      if (gated && i == count - 1) return fail("%s: last band must not have 'requires'", who);
      char tmp[64];
      if (wantIcon) { if (!vStr(e, "icon", tmp, sizeof(tmp)) || !knownIcon(who, "band icon", tmp)) return false; }
      else if (!vStr(e, "text", tmp, sizeof(tmp))) return fail("%s: band %d has no text", who, i);
    }
    return true;
  }
};

} // namespace

bool Spec::validate(toml_table_t* root, char* err, size_t errLen) const {
  V v{ root, err, errLen, g_iconCheckerAccessor() };

  toml_array_t* outcomes = toml_array_in(root, "outcome");
  if (!outcomes || toml_array_nelem(outcomes) == 0) return v.fail("no outcomes");
  if (!toml_array_in(root, "icons")) return v.fail("no icons list");
  toml_table_t* catalog = toml_table_in(root, "wear");
  if (!catalog) return v.fail("no [wear] catalogue");

  // Wear catalogue
  for (int i = 0; ; i++) {
    const char* key = toml_key_in(catalog, i);
    if (!key) break;
    toml_table_t* item = toml_table_in(catalog, key);
    if (!item) return v.fail("wear.%s is not a table", key);
    char label[40], icon[24];
    if (!vStr(item, "label", label, sizeof(label))) return v.fail("wear.%s has no label", key);
    if (!v.fits("label", key, label, sizeof(Wear::label))) return false;
    if (!vStr(item, "icon", icon, sizeof(icon))) return v.fail("wear.%s has no icon", key);
    if (!v.knownIcon(key, "icon", icon)) return false;
  }

  const int count = toml_array_nelem(outcomes);
  for (int i = 0; i < count; i++) {
    toml_table_t* o = toml_table_at(outcomes, i);
    char id[40] = "";
    if (!vStr(o, "id", id, sizeof(id))) return v.fail("outcome %d has no id", i);
    if (!v.fits("id", id, id, sizeof(Outcome::id))) return false;
    for (int j = 0; j < i; j++) {
      char other[40];
      if (vStr(toml_table_at(outcomes, j), "id", other, sizeof(other)) && !strcmp(other, id))
        return v.fail("duplicate outcome id '%s'", id);
    }

    // The pool: `names` (name-only variants) plus any [[outcome.variant]].
    toml_array_t* names = toml_array_in(o, "names");
    toml_array_t* variants = toml_array_in(o, "variant");
    const int nNames = names ? toml_array_nelem(names) : 0;
    const int nVariants = variants ? toml_array_nelem(variants) : 0;
    if (nNames + nVariants == 0) return v.fail("%s: needs at least one name or variant", id);

    char nm[80];
    for (int k = 0; k < nNames; k++) {
      if (!vStrAt(names, k, nm, sizeof(nm))) return v.fail("%s: name %d is not a string", id, k);
      // Leave room for the longest holiday prefix plus a space.
      if (!v.fits("name", id, nm, sizeof(Outcome::title) - sizeof(Outcome::holiday))) return false;
    }
    for (int k = 0; k < nVariants; k++) {
      toml_table_t* var = toml_table_at(variants, k);
      if (!var) return v.fail("%s: variant %d is not a table", id, k);
      if (!vStr(var, "name", nm, sizeof(nm))) return v.fail("%s: variant %d has no name", id, k);
      if (!v.fits("variant name", id, nm, sizeof(Outcome::title) - sizeof(Outcome::holiday))) return false;
    }

    // Every field a variant may override has to be valid wherever it appears,
    // and must resolve for each variant once the outcome's fallback is applied.
    char tmp[80];
    for (int k = -1; k < nVariants; k++) {
      toml_table_t* var = (k < 0) ? nullptr : toml_table_at(variants, k);
      const char* who = (k < 0) ? id : nm;
      if (k >= 0) vStr(var, "name", nm, sizeof(nm));

      auto get = [&](const char* key, char* dst, size_t n) {
        if (var && vStr(var, key, dst, n)) return true;
        return vStr(o, key, dst, n);
      };
      if (get("tagline", tmp, sizeof(tmp)) && !v.fits("tagline", who, tmp, sizeof(Outcome::tagline))) return false;
      if (!get("icon", tmp, sizeof(tmp))) return v.fail("%s: no icon", who);
      if (!v.knownIcon(who, "icon", tmp)) return false;
      if (get("color", tmp, sizeof(tmp))) {
        static const char* inks[] = { "none", "yellow", "blue", "red", "green" };
        bool ok = false;
        for (auto c : inks) if (!strcmp(tmp, c)) { ok = true; break; }
        if (!ok) return v.fail("%s: color '%s' is not an available ink", who, tmp);
      }
      if (get("panel", tmp, sizeof(tmp)) && strcmp(tmp, "light") && strcmp(tmp, "dark"))
        return v.fail("%s: panel must be light or dark", who);
      if (get("also", tmp, sizeof(tmp)) && !v.fits("also", who, tmp, sizeof(Outcome::alsoGrab))) return false;

      toml_array_t* wear = (var ? toml_array_in(var, "wear") : nullptr);
      if (!wear) wear = toml_array_in(o, "wear");
      if (!wear || toml_array_nelem(wear) != 4) return v.fail("%s: needs exactly four wear items", who);
      for (int w2 = 0; w2 < 4; w2++) {
        char wid[40];
        if (!vStrAt(wear, w2, wid, sizeof(wid))) return v.fail("%s: wear %d is not a name", who, w2);
        if (!toml_table_in(catalog, wid)) return v.fail("%s: no wear called '%s'", who, wid);
      }
    }

    toml_array_t* when = toml_array_in(o, "when");
    const int conds = when ? toml_array_nelem(when) : 0;
    for (int k = 0; k < conds; k++) {
      char expr[48], field[24], op[4];
      double val = 0;
      if (!vStrAt(when, k, expr, sizeof(expr)))
        return v.fail("%s: condition %d must be text like \"rain >= 50\"", id, k);
      if (!vParseWhen(expr, field, sizeof(field), op, sizeof(op), val))
        return v.fail("%s: cannot read condition \"%s\"", id, expr);
      if (!vKnownField(field)) return v.fail("%s: unknown field '%s' in \"%s\"", id, field, expr);
      if (!vKnownOp(op)) return v.fail("%s: bad operator '%s' in \"%s\"", id, op, expr);
    }
    if (i == count - 1 && conds > 0) return v.fail("last outcome '%s' must have no conditions", id);
    if (i != count - 1 && conds == 0) return v.fail("%s: only the last outcome may have no conditions", id);

    toml_table_t* byFailed = toml_table_in(o, "also_when_failed");
    if (byFailed) {
      for (int k = 0; ; k++) {
        const char* field = toml_key_in(byFailed, k);
        if (!field) break;
        if (!vKnownField(field)) return v.fail("%s: also_when_failed.%s is not a field", id, field);
        char line[220];
        if (!vStr(byFailed, field, line, sizeof(line))) return v.fail("%s: also_when_failed.%s must be text", id, field);
        if (!v.fits("also_when_failed line", id, line, sizeof(Outcome::alsoGrab))) return false;
      }
    }
    if (vHas(o, "also_near_beach") != vHas(o, "also_near_beach_within"))
      return v.fail("%s: also_near_beach needs also_near_beach_within", id);
  }

  toml_array_t* stats = toml_array_in(root, "stat");
  if (!stats || toml_array_nelem(stats) == 0) return v.fail("no stats");
  if (toml_array_nelem(stats) > 6) return v.fail("more than six stats");
  for (int i = 0; i < toml_array_nelem(stats); i++) {
    toml_table_t* st = toml_table_at(stats, i);
    char id[40] = "", tmp[64] = "";
    if (!vStr(st, "id", id, sizeof(id))) return v.fail("stat %d has no id", i);
    if (!v.fits("stat id", id, id, sizeof(Stat::id))) return false;
    if (vStr(st, "label", tmp, sizeof(tmp)) && !v.fits("label", id, tmp, sizeof(Stat::label))) return false;

    if (vStr(st, "icon", tmp, sizeof(tmp))) { if (!v.knownIcon(id, "icon", tmp)) return false; }
    else {
      if (!vStr(st, "icon_field", tmp, sizeof(tmp)) || !vKnownField(tmp)) return v.fail("%s: icon_field unknown", id);
      if (!v.bands(id, toml_array_in(st, "icon_bands"), true)) return false;
    }
    if (!vStr(st, "value_field", tmp, sizeof(tmp)) || !vKnownField(tmp)) return v.fail("%s: value_field unknown", id);
    bool hasFormat = vHas(st, "value_format"), hasBands = vHas(st, "value_bands");
    if (hasFormat == hasBands) return v.fail("%s: needs value_format or value_bands, not both", id);
    if (hasBands && !v.bands(id, toml_array_in(st, "value_bands"), false)) return false;

    if (!vStr(st, "word_field", tmp, sizeof(tmp)) || !vKnownField(tmp)) return v.fail("%s: word_field unknown", id);
    if (!v.bands(id, toml_array_in(st, "word_bands"), false)) return false;

    toml_table_t* ov = toml_table_in(st, "word_override");
    if (ov) {
      char op[8];
      double val = 0;
      if (!vStr(ov, "field", tmp, sizeof(tmp)) || !vKnownField(tmp)) return v.fail("%s: override field unknown", id);
      if (!vStr(ov, "op", op, sizeof(op)) || !vKnownOp(op)) return v.fail("%s: override op bad", id);
      if (!vNum(ov, "value", val)) return v.fail("%s: override needs a value", id);
      if (!vStr(ov, "text", tmp, sizeof(tmp)) || !v.fits("override text", id, tmp, sizeof(Stat::word))) return false;
    }
  }

  toml_array_t* holidays = toml_array_in(root, "holiday");
  for (int i = 0; holidays && i < toml_array_nelem(holidays); i++) {
    toml_table_t* h = toml_table_at(holidays, i);
    char nm[40] = "";
    if (!vStr(h, "name", nm, sizeof(nm))) return v.fail("holiday %d has no name", i);
    if (!v.fits("name", nm, nm, sizeof(Outcome::holiday))) return false;
    double month = 0, day = 0, wd = 0, nth = 0;
    if (!vNum(h, "month", month) || month < 1 || month > 12) return v.fail("%s: month must be 1-12", nm);
    if (vNum(h, "day", day)) {
      if (day < 1 || day > 31) return v.fail("%s: day must be 1-31", nm);
    } else if (vNum(h, "weekday", wd) && vNum(h, "nth", nth)) {
      if (wd < 0 || wd > 6) return v.fail("%s: weekday must be 0-6 (0 = Sunday)", nm);
      if (nth == 0 || nth > 5 || nth < -1) return v.fail("%s: nth must be 1-5, or -1 for last", nm);
    } else {
      return v.fail("%s: needs a day, or a weekday and nth", nm);
    }
  }

  err[0] = '\0';
  return true;
}

} // namespace day
