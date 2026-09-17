#include "weather.h"
#include "net.h"
#include <ArduinoJson.h>
#include <cstdio>

static long floorDiv(long a, long b) {
  long q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

long localDays(time_t utc, int32_t off)      { return floorDiv((long)utc + off, 86400L); }
int  localMinuteOfDay(time_t utc, int32_t off) {
  long s = ((long)utc + off) % 86400L;
  if (s < 0) s += 86400L;
  return (int)(s / 60);
}
int  localHour(time_t utc, int32_t off)      { return localMinuteOfDay(utc, off) / 60; }

static int16_t intOr(JsonVariantConst v, int16_t missing) {
  return v.isNull() ? missing : (int16_t)v.as<int>();
}

static bool parseForecast(const String& body, Fetched& out, char* err, size_t errLen) {
  JsonDocument filter;
  filter["utc_offset_seconds"] = true;
  filter["current"]["time"] = true;
  filter["hourly"]["time"] = true;
  filter["hourly"]["relative_humidity_2m"] = true;
  filter["hourly"]["precipitation_probability"] = true;
  filter["hourly"]["weather_code"] = true;
  static const char* dailyKeys[] = {
    "sunrise", "sunset", "uv_index_max", "temperature_2m_max", "temperature_2m_min",
    "weather_code", "precipitation_probability_max", "wind_speed_10m_max" };
  for (const char* k : dailyKeys) filter["daily"][k] = true;

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (e) { snprintf(err, errLen, "forecast json: %s", e.c_str()); return false; }

  out.utcOffsetSec = doc["utc_offset_seconds"] | 0;
  out.modelNowUtc  = (time_t)(doc["current"]["time"].as<long long>());

  JsonArrayConst times = doc["hourly"]["time"];
  if (times.size() == 0) { snprintf(err, errLen, "forecast: no hourly data"); return false; }
  out.todayStartUtc = (time_t)times[0].as<long long>();
  long day0 = localDays(out.todayStartUtc, out.utcOffsetSec);

  JsonArrayConst hum = doc["hourly"]["relative_humidity_2m"];
  JsonArrayConst pp  = doc["hourly"]["precipitation_probability"];
  JsonArrayConst wc  = doc["hourly"]["weather_code"];

  out.raw.wxCount = 0;
  for (size_t i = 0; i < times.size() && out.raw.wxCount < beach::RAW_MAX_HOURS; i++) {
    time_t t = (time_t)times[i].as<long long>();
    long day = localDays(t, out.utcOffsetSec) - day0;
    if (day < 0 || day > 1) continue;
    beach::RawHourly& r = out.raw.wx[out.raw.wxCount++];
    r.day        = (int8_t)day;
    r.hour       = (int8_t)localHour(t, out.utcOffsetSec);
    r.code       = intOr(wc[i], -1);
    r.precipProb = intOr(pp[i], -1);
    r.humidity   = intOr(hum[i], -1);
  }

  JsonObjectConst daily = doc["daily"];
  JsonArrayConst tmax = daily["temperature_2m_max"];
  for (int d = 0; d < 2 && (size_t)d < tmax.size(); d++) {
    beach::RawDaily& rd = out.raw.daily[d];
    rd.valid   = true;
    rd.tempMax = daily["temperature_2m_max"][d] | 0.0;
    rd.tempMin = daily["temperature_2m_min"][d] | 0.0;
    rd.windMax = daily["wind_speed_10m_max"][d] | 0.0;
    rd.uvMax   = daily["uv_index_max"][d] | 0.0;
    rd.code    = intOr(daily["weather_code"][d], 99);
    rd.precipProbMax = intOr(daily["precipitation_probability_max"][d], 0);
    long long sr = daily["sunrise"][d] | 0LL;
    long long ss = daily["sunset"][d] | 0LL;
    rd.sunriseMin = sr ? (int16_t)localMinuteOfDay((time_t)sr, out.utcOffsetSec) : 0;
    rd.sunsetMin  = ss ? (int16_t)localMinuteOfDay((time_t)ss, out.utcOffsetSec) : 0;
  }
  if (!out.raw.daily[0].valid) { snprintf(err, errLen, "forecast: no daily data"); return false; }
  return true;
}

static bool parseAirQuality(const String& body, Fetched& out) {
  JsonDocument filter;
  filter["hourly"]["time"] = true;
  filter["hourly"]["us_aqi"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;

  JsonArrayConst times = doc["hourly"]["time"];
  JsonArrayConst aqi   = doc["hourly"]["us_aqi"];
  long day0 = localDays(out.todayStartUtc, out.utcOffsetSec);
  out.raw.aqiCount = 0;
  for (size_t i = 0; i < times.size() && out.raw.aqiCount < beach::RAW_MAX_HOURS; i++) {
    time_t t = (time_t)times[i].as<long long>();
    long day = localDays(t, out.utcOffsetSec) - day0;
    if (day < 0 || day > 1) continue;
    beach::RawAqiHour& r = out.raw.aqi[out.raw.aqiCount++];
    r.day  = (int8_t)day;
    r.hour = (int8_t)localHour(t, out.utcOffsetSec);
    r.aqi  = intOr(aqi[i], -1);
  }
  return out.raw.aqiCount > 0;
}

bool fetchWeather(const Settings& s, Fetched& out, char* err, size_t errLen) {
  const char* scheme = s.useTls ? "https" : "http";
  char url[512];
  String body;

  snprintf(url, sizeof(url),
    "%s://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
    "&timezone=auto&timeformat=unixtime&forecast_days=2"
    "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch"
    "&current=weather_code"
    "&hourly=relative_humidity_2m,precipitation_probability,weather_code"
    "&daily=sunrise,sunset,uv_index_max,temperature_2m_max,temperature_2m_min,"
    "weather_code,precipitation_probability_max,wind_speed_10m_max",
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
