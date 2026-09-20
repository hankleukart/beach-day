#include "weather.h"
#include "net.h"
#include <ArduinoJson.h>
#include <cstdio>

static long floorDiv(long a, long b) { long q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
long localDays(time_t utc, int32_t off)        { return floorDiv((long)utc + off, 86400L); }
int  localMinuteOfDay(time_t utc, int32_t off) { long s = ((long)utc + off) % 86400L; if (s < 0) s += 86400L; return (int)(s / 60); }
int  localHour(time_t utc, int32_t off)        { return localMinuteOfDay(utc, off) / 60; }

static int16_t intOr(JsonVariantConst v, int16_t missing) { return v.isNull() ? missing : (int16_t)v.as<int>(); }

static bool parseForecast(const String& body, Fetched& out, char* err, size_t errLen) {
  JsonDocument filter;
  filter["utc_offset_seconds"] = true;
  filter["current"]["time"] = true;
  for (const char* k : { "time", "temperature_2m", "relative_humidity_2m", "precipitation_probability", "weather_code", "cloud_cover", "wind_speed_10m" })
    filter["hourly"][k] = true;
  for (const char* k : { "sunrise", "sunset", "weather_code" }) filter["daily"][k] = true;

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (e) { snprintf(err, errLen, "forecast json: %s", e.c_str()); return false; }

  out.utcOffsetSec = doc["utc_offset_seconds"] | 0;
  out.modelNowUtc  = (time_t)(doc["current"]["time"].as<long long>());
  JsonArrayConst times = doc["hourly"]["time"];
  if (times.size() == 0) { snprintf(err, errLen, "forecast: no hourly data"); return false; }
  out.todayStartUtc = (time_t)times[0].as<long long>();
  long day0 = localDays(out.todayStartUtc, out.utcOffsetSec);

  JsonArrayConst temp = doc["hourly"]["temperature_2m"], hum = doc["hourly"]["relative_humidity_2m"],
                 pp = doc["hourly"]["precipitation_probability"], wc = doc["hourly"]["weather_code"],
                 cc = doc["hourly"]["cloud_cover"], ws = doc["hourly"]["wind_speed_10m"];
  out.raw.n = 0;
  for (size_t i = 0; i < times.size() && out.raw.n < day::MAX_ROWS; i++) {
    time_t t = (time_t)times[i].as<long long>();
    long d = localDays(t, out.utcOffsetSec) - day0;
    if (d < 0 || d > 1) continue;
    day::HourRow& r = out.raw.hours[out.raw.n++];
    r.day = (int8_t)d;
    r.hour = (int8_t)localHour(t, out.utcOffsetSec);
    r.tempF = temp[i] | 0.0;
    r.windMph = ws[i] | 0.0;
    r.humidity = intOr(hum[i], -1);
    r.precipProb = intOr(pp[i], -1);
    r.cloud = intOr(cc[i], -1);
    r.code = intOr(wc[i], -1);
  }
  JsonObjectConst daily = doc["daily"];
  JsonArrayConst sr = daily["sunrise"];
  for (int d = 0; d < 2 && (size_t)d < sr.size(); d++) {
    day::DayMeta& m = out.raw.day[d];
    m.valid = true;
    long long a = daily["sunrise"][d] | 0LL, b = daily["sunset"][d] | 0LL;
    m.sunriseMin = a ? (int16_t)localMinuteOfDay((time_t)a, out.utcOffsetSec) : 0;
    m.sunsetMin  = b ? (int16_t)localMinuteOfDay((time_t)b, out.utcOffsetSec) : 0;
    m.dailyCode  = intOr(daily["weather_code"][d], 99);
  }
  if (!out.raw.day[0].valid) { snprintf(err, errLen, "forecast: no daily data"); return false; }
  return true;
}

static bool parseAirQuality(const String& body, Fetched& out) {
  JsonDocument filter;
  filter["hourly"]["time"] = true;
  filter["hourly"]["us_aqi"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonArrayConst times = doc["hourly"]["time"], aqi = doc["hourly"]["us_aqi"];
  long day0 = localDays(out.todayStartUtc, out.utcOffsetSec);
  out.raw.na = 0;
  for (size_t i = 0; i < times.size() && out.raw.na < day::MAX_ROWS; i++) {
    time_t t = (time_t)times[i].as<long long>();
    long d = localDays(t, out.utcOffsetSec) - day0;
    if (d < 0 || d > 1) continue;
    out.raw.aqi[out.raw.na++] = day::AqiRow{ (int8_t)d, (int8_t)localHour(t, out.utcOffsetSec), intOr(aqi[i], -1) };
  }
  return out.raw.na > 0;
}

bool fetchWeather(const Settings& s, Fetched& out, char* err, size_t errLen) {
  const char* scheme = s.useTls ? "https" : "http";
  char url[560];
  String body;
  snprintf(url, sizeof(url),
    "%s://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
    "&timezone=auto&timeformat=unixtime&forecast_days=2"
    "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch"
    "&current=weather_code"
    "&hourly=temperature_2m,relative_humidity_2m,precipitation_probability,weather_code,cloud_cover,wind_speed_10m"
    "&daily=sunrise,sunset,weather_code",
    scheme, s.latitude, s.longitude);
  if (!netGet(url, s.useTls, body)) { snprintf(err, errLen, "forecast request failed"); return false; }
  if (!parseForecast(body, out, err, errLen)) return false;
  body = String();
  snprintf(url, sizeof(url),
    "%s://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.5f&longitude=%.5f"
    "&timezone=auto&timeformat=unixtime&forecast_days=2&hourly=us_aqi",
    scheme, s.latitude, s.longitude);
  out.aqiOk = netGet(url, s.useTls, body) && parseAirQuality(body, out);
  if (!out.aqiOk) Serial.println("[weather] air quality unavailable; AQI will read 0");
  return true;
}
