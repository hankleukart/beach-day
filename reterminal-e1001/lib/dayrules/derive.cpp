#include "derive.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "wmo.h"

namespace day {

static const char* const WEEKDAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char* weekdayName(int w) { return WEEKDAYS[((w % 7) + 7) % 7]; }

void formatClock12(int minutes, char* out, size_t n) {
  int h = (minutes / 60) % 24, m = minutes % 60;
  int dh = h % 12; if (dh == 0) dh = 12;
  snprintf(out, n, "%d:%02d %s", dh, m, h >= 12 ? "PM" : "AM");
}

// WMO 71-77 and 85-86: snowfall, snow grains, snow showers. Freezing rain and
// drizzle (56-57, 66-67) are left out: they want a raincoat, not snow boots.
static bool isSnowCode(int c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }

// Hours are counted from midnight at the start of day 0, so yesterday's
// evening is a run of negative hours.
static int absHour(const HourRow& r) { return r.day * 24 + r.hour; }

static const HourRow* rowAt(const Raw& raw, int a) {
  for (int i = 0; i < raw.n; i++) if (absHour(raw.hours[i]) == a) return &raw.hours[i];
  return nullptr;
}

static int modeCode(const Raw& raw, const Span& sp, int fallback) {
  int codes[MAX_ROWS], counts[MAX_ROWS], k = 0;
  for (int i = 0; i < raw.n; i++) {
    const HourRow& r = raw.hours[i];
    const int a = absHour(r);
    if (a < sp.a0 || a > sp.a1 || r.code < 0) continue;
    int j = 0; while (j < k && codes[j] != r.code) j++;
    if (j == k) { codes[k] = r.code; counts[k] = 0; k++; }
    counts[j]++;
  }
  if (k == 0) return fallback;
  int best = 0;
  for (int j = 1; j < k; j++) if (counts[j] > counts[best] || (counts[j] == counts[best] && codes[j] < codes[best])) best = j;
  return codes[best];
}

void daylightHours(const Raw& raw, int d, int& h0, int& h1) {
  const DayMeta& m = raw.day[d];
  h0 = FALLBACK_START_HOUR; h1 = FALLBACK_END_HOUR;
  // Truncating to the hour is deliberate: sunrise at 6:42 makes the 6am row
  // part of the day, and sunset at 7:08 PM makes the 7pm row the last one.
  if (m.sunriseMin > 0 && m.sunsetMin > 0) { h0 = m.sunriseMin / 60; h1 = m.sunsetMin / 60; }
}

Span daySpan(const Raw& raw, int d) {
  int h0, h1;
  daylightHours(raw, d, h0, h1);
  return Span{ d * 24 + h0, d * 24 + h1 };
}

Span eveningSpan(const Raw& raw, int nowMinute) {
  // After noon the evening is tonight's; before it, yesterday's.
  const int d = nowMinute >= 12 * 60 ? 0 : -1;
  const DayMeta& eve = raw.meta(d);
  const int fall = eve.sunsetMin > 0 ? eve.sunsetMin / 60 : FALLBACK_END_HOUR;
  return Span{ d * 24 + fall, d * 24 + 23 };
}

void derive(const Raw& raw, int d, int weekday, Inputs& out) {
  derive(raw, daySpan(raw, d), weekday, out);
}

void derive(const Raw& raw, const Span& sp, int weekday, Inputs& out) {
  out = Inputs{};
  const DayMeta& m0 = raw.meta(spanDay(sp.a0));

  double tMin = 1e9, tMax = -1e9, wMax = -1e9;
  int pMax = -1, hMin = 1 << 14, hMax = -1;
  long cloudSum = 0; int cloudN = 0;
  int snowN = 0;
  bool any = false;
  for (int i = 0; i < raw.n; i++) {
    const HourRow& r = raw.hours[i];
    const int a = absHour(r);
    if (a < sp.a0 || a > sp.a1) continue;
    any = true;
    if (r.tempF < tMin) tMin = r.tempF;
    if (r.tempF > tMax) tMax = r.tempF;
    if (r.windMph > wMax) wMax = r.windMph;
    if (r.precipProb > pMax) pMax = r.precipProb;
    if (r.humidity >= 0) { if (r.humidity < hMin) hMin = r.humidity; if (r.humidity > hMax) hMax = r.humidity; }
    if (r.cloud >= 0) { cloudSum += r.cloud; cloudN++; }
    if (isSnowCode(r.code)) snowN++;
  }
  // Walk the window in hour order: first clear hour, and the longest unbroken
  // run of them. A missing cloud reading breaks a run rather than extending it.
  // The hours handed on are clock hours (0-23): they are only ever shown.
  int runStart = -1, bestStart = -1, bestEnd = -1, bestLen = 0;
  for (int a = sp.a0; a <= sp.a1; a++) {
    const HourRow* r = rowAt(raw, a);
    const bool clear = r && r->cloud >= 0 && r->cloud <= CLEAR_CLOUD_MAX_PCT;
    if (clear) {
      if (!out.hasFirstClearHour) { out.hasFirstClearHour = true; out.firstClearHour = spanHour(a); }
      if (runStart < 0) runStart = a;
      int len = a - runStart + 1;
      if (len > bestLen) { bestLen = len; bestStart = runStart; bestEnd = a; }
    } else {
      runStart = -1;
    }
  }
  out.sunRunHours = bestLen;
  out.sunRunStartHour = bestStart < 0 ? -1 : spanHour(bestStart);
  out.sunRunEndHour = bestEnd < 0 ? -1 : spanHour(bestEnd);

  // Wet window: first and last hour over the threshold, gaps included.
  for (int a = sp.a0; a <= sp.a1; a++) {
    const HourRow* r = rowAt(raw, a);
    if (r && r->precipProb > RAIN_WINDOW_MIN_PCT) {
      if (out.rainStartHour < 0) out.rainStartHour = spanHour(a);
      out.rainEndHour = spanHour(a);
    }
  }
  if (any) {
    out.tempMinF = std::round(tMin);
    out.tempMaxF = std::round(tMax);
    out.windMaxMph = std::round(wMax);
  }
  out.tempSwingF = out.tempMaxF - out.tempMinF;
  out.precipChanceMaxPct = pMax < 0 ? 0 : pMax;
  out.humidityMinPct = hMax < 0 ? 0 : hMin;
  out.humidityMaxPct = hMax < 0 ? 0 : hMax;
  out.cloudCoverAvgPct = cloudN ? std::round((double)cloudSum / cloudN) : 0;
  out.snowHours = snowN;

  int aMin = 1 << 20, aMax = -1;
  for (int i = 0; i < raw.na; i++) {
    const AqiRow& r = raw.aqi[i];
    const int a = r.day * 24 + r.hour;
    if (r.aqi < 0 || a < sp.a0 || a > sp.a1) continue;
    if (r.aqi < aMin) aMin = r.aqi;
    if (r.aqi > aMax) aMax = r.aqi;
  }
  out.aqiMax = aMax < 0 ? 0 : aMax;
  out.aqiMin = aMax < 0 ? 0 : aMin;

  snprintf(out.conditionSummary, sizeof(out.conditionSummary), "%s", beach::wmoDescription(modeCode(raw, sp, m0.dailyCode)));
  // The window's first day's sunset, and the first sunrise from its opening
  // hour on: a day's own sunrise, or for an evening, tomorrow's.
  const DayMeta& rise = (m0.sunriseMin > 0 && m0.sunriseMin / 60 >= spanHour(sp.a0)) ? m0 : raw.meta(spanDay(sp.a0) + 1);
  if (m0.sunsetMin > 0) formatClock12(m0.sunsetMin, out.sunsetLocal, sizeof(out.sunsetLocal));
  if (rise.sunriseMin > 0) formatClock12(rise.sunriseMin, out.sunriseLocal, sizeof(out.sunriseLocal));
  snprintf(out.weekdayName, sizeof(out.weekdayName), "%s", weekdayName(weekday));
}

} // namespace day
