#include "aggregate.h"
#include "wmo.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace beach {

static double halfUp(double v) { return v >= 0 ? std::floor(v + 0.5) : -std::floor(-v + 0.5); }

int roundDisplay(double v) {
  const double eps = 1e-6;
  double scaled = v * 10.0;
  scaled += (scaled >= 0 ? eps : -eps);
  double oneDecimal = halfUp(scaled) / 10.0;
  oneDecimal += (oneDecimal >= 0 ? eps : -eps);
  return static_cast<int>(halfUp(oneDecimal));
}

static bool inActiveHours(int h) { return h >= ACTIVE_START_HOUR && h <= ACTIVE_END_HOUR; }

// Mode of the WMO code over active hours; ties go to the lower code, which is
// what Groovy's max{ a.value <=> b.value ?: b.key <=> a.key } resolves to.
static int modeCode(const RawForecast& raw, int day, int fallback) {
  int codes[MAX_ACTIVE_HOURS];
  int counts[MAX_ACTIVE_HOURS];
  int n = 0;
  for (int i = 0; i < raw.wxCount; i++) {
    const RawHourly& r = raw.wx[i];
    if (r.day != day || !inActiveHours(r.hour) || r.code < 0) continue;
    int k = 0;
    while (k < n && codes[k] != r.code) k++;
    if (k == n) { if (n == MAX_ACTIVE_HOURS) continue; codes[n] = r.code; counts[n] = 0; n++; }
    counts[k]++;
  }
  if (n == 0) return fallback;
  int best = 0;
  for (int k = 1; k < n; k++) {
    if (counts[k] > counts[best] || (counts[k] == counts[best] && codes[k] < codes[best])) best = k;
  }
  return codes[best];
}

static void fillDay(const RawForecast& raw, int day, int sunriseHour, int sunsetHour, DayInputs& d) {
  const RawDaily& daily = raw.daily[day];
  d.valid   = daily.valid;
  d.temp    = roundDisplay(daily.tempMax);
  d.tempMin = roundDisplay(daily.tempMin);
  d.wind    = roundDisplay(daily.windMax);
  d.uv      = roundDisplay(daily.uvMax);

  // Precip: today = max over active hours; tomorrow = the daily max. Not symmetric.
  if (day == 0) {
    int maxProb = 0;
    for (int i = 0; i < raw.wxCount; i++) {
      const RawHourly& r = raw.wx[i];
      if (r.day != 0 || !inActiveHours(r.hour)) continue;
      int p = r.precipProb < 0 ? 0 : r.precipProb;
      if (p > maxProb) maxProb = p;
    }
    d.precipProb = maxProb;
  } else {
    d.precipProb = daily.precipProbMax;
  }

  d.weatherCode = modeCode(raw, day, daily.code);
  snprintf(d.conditionText, sizeof(d.conditionText), "%s", wmoDescription(d.weatherCode));

  // Active-hour rows for the sun scan, in hour order.
  d.hourCount = 0;
  for (int h = ACTIVE_START_HOUR; h <= ACTIVE_END_HOUR; h++) {
    for (int i = 0; i < raw.wxCount; i++) {
      const RawHourly& r = raw.wx[i];
      if (r.day == day && r.hour == h) {
        // A missing code counts as not-sunny (99); the Hubitat app sent 0 here,
        // which would have read as clear sky. Open-Meteo never omits forecast
        // codes, so the two never disagree in practice.
        d.hours[d.hourCount++] = HourRow{ static_cast<int8_t>(h),
                                          static_cast<int16_t>(r.code < 0 ? 99 : r.code) };
        break;
      }
    }
  }

  // Humidity: whole day, all 24 hours.
  int hMin = 1000, hMax = -1;
  for (int i = 0; i < raw.wxCount; i++) {
    const RawHourly& r = raw.wx[i];
    if (r.day != day || r.humidity < 0) continue;
    if (r.humidity < hMin) hMin = r.humidity;
    if (r.humidity > hMax) hMax = r.humidity;
  }
  d.humidityMin = hMax < 0 ? 0 : hMin;
  d.humidityMax = hMax < 0 ? 0 : hMax;

  // AQI: sunrise hour .. sunset hour inclusive (today's sun, for both days).
  int aMin = 100000, aMax = -1;
  for (int i = 0; i < raw.aqiCount; i++) {
    const RawAqiHour& r = raw.aqi[i];
    if (r.day != day || r.aqi < 0) continue;
    if (r.hour < sunriseHour || r.hour > sunsetHour) continue;
    if (r.aqi < aMin) aMin = r.aqi;
    if (r.aqi > aMax) aMax = r.aqi;
  }
  d.aqi    = aMax < 0 ? 0 : aMax;
  d.aqiMin = aMax < 0 ? 0 : aMin;
}

void aggregate(const RawForecast& raw, Inputs& out) {
  out.sunriseMinutes = raw.daily[0].sunriseMin;
  out.sunsetMinutes  = raw.daily[0].sunsetMin;

  int sunriseHour = out.sunriseMinutes > 0 ? out.sunriseMinutes / 60 : AQI_FALLBACK_START_HOUR;
  int sunsetHour  = out.sunsetMinutes  > 0 ? out.sunsetMinutes  / 60 : AQI_FALLBACK_END_HOUR;

  fillDay(raw, 0, sunriseHour, sunsetHour, out.today);
  fillDay(raw, 1, sunriseHour, sunsetHour, out.tomorrow);
}

} // namespace beach
