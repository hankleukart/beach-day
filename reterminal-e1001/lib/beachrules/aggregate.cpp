#include "aggregate.h"
#include "wmo.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace beach {

static double halfUp(double v) { return v >= 0 ? std::floor(v + 0.5) : -std::floor(-v + 0.5); }

int roundDisplay(double v) { return static_cast<int>(halfUp(v)); }

Window daylightWindow(const RawDaily& d) {
  if (d.sunriseMin > 0 && d.sunsetMin > 0) {
    return Window{ d.sunriseMin / 60, d.sunsetMin / 60 };
  }
  return Window{ DAYLIGHT_FALLBACK_START_HOUR, DAYLIGHT_FALLBACK_END_HOUR };
}

// Mode of the WMO code over the window; ties go to the lower code, matching
// Groovy's max{ a.value <=> b.value ?: b.key <=> a.key }.
static int modeCode(const RawForecast& raw, int day, Window w, int fallback) {
  int codes[MAX_DAYLIGHT_HOURS];
  int counts[MAX_DAYLIGHT_HOURS];
  int n = 0;
  for (int i = 0; i < raw.wxCount; i++) {
    const RawHourly& r = raw.wx[i];
    if (r.day != day || r.hour < w.startHour || r.hour > w.endHour || r.code < 0) continue;
    int k = 0;
    while (k < n && codes[k] != r.code) k++;
    if (k == n) { if (n == MAX_DAYLIGHT_HOURS) continue; codes[n] = r.code; counts[n] = 0; n++; }
    counts[k]++;
  }
  if (n == 0) return fallback;
  int best = 0;
  for (int k = 1; k < n; k++) {
    if (counts[k] > counts[best] || (counts[k] == counts[best] && codes[k] < codes[best])) best = k;
  }
  return codes[best];
}

static void fillDay(const RawForecast& raw, int day, DayInputs& d) {
  const RawDaily& daily = raw.daily[day];
  const Window w = daylightWindow(daily);

  d.valid   = daily.valid;
  d.temp    = roundDisplay(daily.tempMax);
  d.tempMin = roundDisplay(daily.tempMin);
  d.wind    = roundDisplay(daily.windMax);
  d.uv      = roundDisplay(daily.uvMax);
  d.sunriseMin = daily.sunriseMin;
  d.sunsetMin  = daily.sunsetMin;

  // Hourly aggregates, all over this day's own daylight window.
  int precipMax = -1;
  int humMin = 1 << 14, humMax = -1;
  for (int i = 0; i < raw.wxCount; i++) {
    const RawHourly& r = raw.wx[i];
    if (r.day != day || r.hour < w.startHour || r.hour > w.endHour) continue;
    if (r.precipProb >= 0 && r.precipProb > precipMax) precipMax = r.precipProb;
    if (r.humidity >= 0) {
      if (r.humidity < humMin) humMin = r.humidity;
      if (r.humidity > humMax) humMax = r.humidity;
    }
  }
  // No hourly rows in the window -> fall back to the API's daily max.
  d.precipProb  = precipMax < 0 ? daily.precipProbMax : (int16_t)precipMax;
  d.humidityMin = humMax < 0 ? 0 : (int16_t)humMin;
  d.humidityMax = humMax < 0 ? 0 : (int16_t)humMax;

  d.weatherCode = modeCode(raw, day, w, daily.code);
  snprintf(d.conditionText, sizeof(d.conditionText), "%s", wmoDescription(d.weatherCode));

  // Window rows in hour order, for the sun scan.
  d.hourCount = 0;
  for (int h = w.startHour; h <= w.endHour && d.hourCount < MAX_DAYLIGHT_HOURS; h++) {
    for (int i = 0; i < raw.wxCount; i++) {
      const RawHourly& r = raw.wx[i];
      if (r.day == day && r.hour == h) {
        // A missing code counts as not-sunny. Open-Meteo never omits forecast
        // codes, so this never fires in practice.
        d.hours[d.hourCount++] = HourRow{ (int8_t)h, (int16_t)(r.code < 0 ? 99 : r.code) };
        break;
      }
    }
  }

  // AQI over the same window.
  int aMin = 1 << 20, aMax = -1;
  for (int i = 0; i < raw.aqiCount; i++) {
    const RawAqiHour& r = raw.aqi[i];
    if (r.day != day || r.aqi < 0) continue;
    if (r.hour < w.startHour || r.hour > w.endHour) continue;
    if (r.aqi < aMin) aMin = r.aqi;
    if (r.aqi > aMax) aMax = r.aqi;
  }
  d.aqi    = aMax < 0 ? 0 : (int16_t)aMax;
  d.aqiMin = aMax < 0 ? 0 : (int16_t)aMin;
}

void aggregate(const RawForecast& raw, Inputs& out) {
  // Today's sun drives the time condition and the footer.
  out.sunriseMinutes = raw.daily[0].sunriseMin;
  out.sunsetMinutes  = raw.daily[0].sunsetMin;
  fillDay(raw, 0, out.today);
  fillDay(raw, 1, out.tomorrow);
}

} // namespace beach
