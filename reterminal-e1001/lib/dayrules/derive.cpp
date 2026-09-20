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

static int modeCode(const Raw& raw, int d, int h0, int h1, int fallback) {
  int codes[MAX_ROWS], counts[MAX_ROWS], k = 0;
  for (int i = 0; i < raw.n; i++) {
    const HourRow& r = raw.hours[i];
    if (r.day != d || r.hour < h0 || r.hour > h1 || r.code < 0) continue;
    int j = 0; while (j < k && codes[j] != r.code) j++;
    if (j == k) { codes[k] = r.code; counts[k] = 0; k++; }
    counts[j]++;
  }
  if (k == 0) return fallback;
  int best = 0;
  for (int j = 1; j < k; j++) if (counts[j] > counts[best] || (counts[j] == counts[best] && codes[j] < codes[best])) best = j;
  return codes[best];
}

void derive(const Raw& raw, int d, int weekday, Inputs& out) {
  out = Inputs{};
  const DayMeta& m = raw.day[d];
  int h0 = FALLBACK_START_HOUR, h1 = FALLBACK_END_HOUR;
  if (m.sunriseMin > 0 && m.sunsetMin > 0) { h0 = m.sunriseMin / 60; h1 = m.sunsetMin / 60; }

  double tMin = 1e9, tMax = -1e9, wMax = -1e9;
  int pMax = -1, hMin = 1 << 14, hMax = -1;
  long cloudSum = 0; int cloudN = 0;
  bool any = false;
  for (int i = 0; i < raw.n; i++) {
    const HourRow& r = raw.hours[i];
    if (r.day != d || r.hour < h0 || r.hour > h1) continue;
    any = true;
    if (r.tempF < tMin) tMin = r.tempF;
    if (r.tempF > tMax) tMax = r.tempF;
    if (r.windMph > wMax) wMax = r.windMph;
    if (r.precipProb > pMax) pMax = r.precipProb;
    if (r.humidity >= 0) { if (r.humidity < hMin) hMin = r.humidity; if (r.humidity > hMax) hMax = r.humidity; }
    if (r.cloud >= 0) { cloudSum += r.cloud; cloudN++; }
  }
  // rows are scanned in hour order for the first clear hour
  for (int h = h0; h <= h1 && !out.hasFirstClearHour; h++) {
    for (int i = 0; i < raw.n; i++) {
      const HourRow& r = raw.hours[i];
      if (r.day == d && r.hour == h && r.cloud >= 0 && r.cloud <= CLEAR_CLOUD_MAX_PCT) {
        out.hasFirstClearHour = true; out.firstClearHour = h; break;
      }
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

  int aMin = 1 << 20, aMax = -1;
  for (int i = 0; i < raw.na; i++) {
    const AqiRow& r = raw.aqi[i];
    if (r.day != d || r.aqi < 0 || r.hour < h0 || r.hour > h1) continue;
    if (r.aqi < aMin) aMin = r.aqi;
    if (r.aqi > aMax) aMax = r.aqi;
  }
  out.aqiMax = aMax < 0 ? 0 : aMax;
  out.aqiMin = aMax < 0 ? 0 : aMin;

  snprintf(out.conditionSummary, sizeof(out.conditionSummary), "%s", beach::wmoDescription(modeCode(raw, d, h0, h1, m.dailyCode)));
  if (m.sunsetMin > 0) formatClock12(m.sunsetMin, out.sunsetLocal, sizeof(out.sunsetLocal));
  snprintf(out.weekdayName, sizeof(out.weekdayName), "%s", weekdayName(weekday));
}

} // namespace day
