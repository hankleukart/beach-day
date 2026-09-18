// Beach Day for the Seeed reTerminal E1001.
//
// Every wake: Wi-Fi -> NTP -> Open-Meteo -> aggregate -> rules -> draw -> sleep.
// The panel keeps its image while the board sleeps, so a failed fetch just
// leaves the last good screen up (redrawn with an OFFLINE stamp).
#include <Arduino.h>
#include <time.h>
#include "settings.h"
#include "net.h"
#include "weather.h"
#include "render.h"
#include "power.h"
#include "pins.h"
#include "beachrules.h"
#include "parking.h"
#include "aggregate.h"
#include "devmode.h"
#include "ota.h"
#include "portal.h"
#include "geocode.h"
#include "version.h"
#include <Preferences.h>

// Survives deep sleep (RTC slow memory), not a power cycle.
RTC_DATA_ATTR static ViewModel lastView;
RTC_DATA_ATTR static uint16_t  failCount   = 0;
RTC_DATA_ATTR static bool      staleShown  = false;
RTC_DATA_ATTR static uint32_t  bootCount   = 0;

// Crash counter. Incremented at the top of every boot and cleared once a cycle
// completes, so it only accumulates when the firmware fails before sleeping.
// After SAFE_MODE_THRESHOLD consecutive failures the board stops doing anything
// except looking for a newer firmware, which is the only remote way out of a
// bad release. (The stock Arduino bootloader has no automatic OTA rollback.)
static constexpr int SAFE_MODE_THRESHOLD = 3;

static int bumpBootFailures() {
  Preferences p;
  if (!p.begin("beachday", false)) return 0;
  int n = p.getInt("bootFail", 0) + 1;
  p.putInt("bootFail", n);
  p.end();
  return n;
}

static void clearBootFailures() {
  Preferences p;
  if (!p.begin("beachday", false)) return;
  if (p.getInt("bootFail", 0) != 0) p.putInt("bootFail", 0);
  p.end();
}

static const char* const WEEKDAYS[] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

struct LocalClock {
  time_t utc = 0;
  int32_t off = 0;
  long   days = 0;
  int    minuteOfDay = 0;
  int    secondOfMinute = 0;
  int    dow = 0, dom = 1;
  int    tomorrowDow = 0, tomorrowDom = 1;
};

// days since 1970-01-01 -> civil y/m/d (Howard Hinnant's algorithm)
static void civilFromDays(long z, int& y, int& m, int& d) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long yy  = yoe + era * 400;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp  = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yy + (m <= 2));
}

static LocalClock makeClock(time_t utc, int32_t off) {
  LocalClock c;
  c.utc = utc;
  c.off = off;
  c.days = localDays(utc, off);
  c.minuteOfDay = localMinuteOfDay(utc, off);
  long secs = ((long)utc + off) % 60;
  c.secondOfMinute = (int)(secs < 0 ? secs + 60 : secs);
  c.dow = (int)(((c.days % 7) + 11) % 7);          // 1970-01-01 was a Thursday (4)
  int y, m;
  civilFromDays(c.days, y, m, c.dom);
  c.tomorrowDow = (c.dow + 1) % 7;
  civilFromDays(c.days + 1, y, m, c.tomorrowDom);
  return c;
}

static void fmtClock(int minutes, char* out, size_t n) {
  int h = (minutes / 60) % 24, m = minutes % 60;
  const char* ampm = h >= 12 ? "PM" : "AM";
  int dh = h % 12;
  if (dh == 0) dh = 12;
  snprintf(out, n, "%d:%02d %s", dh, m, ampm);
}

static void upperInPlace(char* s) {
  for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void buildView(const beach::Inputs& in, const beach::Verdict& v,
                      const beach::ParkingAlert& pa, const LocalClock& lc,
                      const Settings& s, int batteryPct, ViewModel& vm) {
  const beach::DayInputs& d = *v.shown;
  vm = ViewModel{};
  vm.valid = true;
  vm.state = static_cast<uint8_t>(v.state);
  vm.isNight = v.isNight;
  vm.condSun = v.condForecast;
  vm.condTemp = v.condTemp;
  vm.condPrecip = v.condPrecip;
  vm.condWind = v.condWind;
  vm.condAqi = v.condAqi;
  vm.temp = d.temp; vm.tempMin = d.tempMin; vm.tempMax = d.temp;
  vm.precip = d.precipProb; vm.wind = d.wind;
  vm.aqi = d.aqi; vm.aqiMin = d.aqiMin; vm.aqiMax = d.aqi; vm.uv = d.uv;
  vm.humMin = d.humidityMin; vm.humMax = d.humidityMax;
  snprintf(vm.conditionText, sizeof(vm.conditionText), "%s", d.conditionText);

  if (v.isNight) snprintf(vm.dayLabel, sizeof(vm.dayLabel), "Tomorrow (%s)", WEEKDAYS[lc.tomorrowDow]);
  else           snprintf(vm.dayLabel, sizeof(vm.dayLabel), "%s", WEEKDAYS[lc.dow]);
  upperInPlace(vm.dayLabel);

  beach::sunRowText(v, vm.sunText, sizeof(vm.sunText));
  vm.clearingLater = v.sun.clearingLater;

  char t[16];
  if (in.sunriseMinutes == 0 || in.sunsetMinutes == 0) {
    snprintf(vm.footer, sizeof(vm.footer), "Always");
  } else if (lc.minuteOfDay >= in.sunriseMinutes && lc.minuteOfDay < in.sunsetMinutes) {
    fmtClock(in.sunsetMinutes, t, sizeof(t));
    snprintf(vm.footer, sizeof(vm.footer), "Sunset: %s", t);
  } else {
    fmtClock(in.sunriseMinutes, t, sizeof(t));
    snprintf(vm.footer, sizeof(vm.footer), "Sunrise: %s", t);
  }

  vm.parkingActive = pa.active;
  snprintf(vm.parkingText, sizeof(vm.parkingText), "%s", pa.text);
  snprintf(vm.locationName, sizeof(vm.locationName), "%s", s.locationName);
  fmtClock(lc.minuteOfDay, t, sizeof(t));
  snprintf(vm.updatedText, sizeof(vm.updatedText), "Updated %s", t);
  vm.batteryPct = (int8_t)batteryPct;
}

// Sleep until the next regular interval, or the next moment the display
// would change (sunrise, sunset-1h, sunset, a parking cutoff), whichever first.
static uint32_t secondsToNextWake(const LocalClock& lc, const beach::Inputs& in,
                                  const Settings& s) {
  const int now = lc.minuteOfDay;
  bool sunKnown = in.sunriseMinutes > 0 && in.sunsetMinutes > 0;
  bool dayPeriod = !sunKnown || (now >= in.sunriseMinutes - 60 && now < in.sunsetMinutes);
  int next = now + (dayPeriod ? s.wakeDayMinutes : s.wakeNightMinutes);

  auto consider = [&](int boundary) {
    // wake one minute past the boundary so the new state is unambiguous
    if (boundary > now && boundary + 1 < next) next = boundary + 1;
  };
  if (sunKnown) {
    consider(in.sunriseMinutes);
    consider(in.sunsetMinutes - beach::SUNSET_BUFFER_MIN);
    consider(in.sunsetMinutes);
    consider(in.sunriseMinutes + 1440);
  }
  for (int i = 0; i < s.parkingCount; i++) {
    const beach::ParkingRule& r = s.parking[i];
    if (!r.enabled) continue;
    bool today = (lc.dow == r.dayOfWeek) && (r.weeksMask & (1u << (beach::weekOfMonth(lc.dom) - 1)));
    if (today) consider(r.endMin);
  }
  long secs = (long)(next - now) * 60 - lc.secondOfMinute;
  if (secs < 120) secs = 120;
  return (uint32_t)secs;
}

// In the dev build this hands off to an interactive loop instead of sleeping,
// so the USB serial port stays up. Both paths never return.
static void sleepNow(uint32_t seconds) {
  clearBootFailures();
#ifdef BEACHDAY_DEV
  (void)seconds;
  devLoop(lastView, lastView.valid);
#else
  renderEnd();
  deepSleepFor(seconds);
#endif
}

void setup() {
  Serial.begin(115200);
  bootCount++;

  const Settings& s = loadSettings();

  // Buttons held while waking: KEY1 (middle) opens setup, KEY2 (left) forces
  // an update check. The wake itself is KEY0 (right), so the gesture is
  // "hold one, press the other".
  pinMode(PIN_KEY1, INPUT_PULLUP);
  pinMode(PIN_KEY2, INPUT_PULLUP);
  bool wantSetup = (digitalRead(PIN_KEY1) == LOW);
  bool forceOta  = (digitalRead(PIN_KEY2) == LOW);

  if (wantSetup || !s.configured()) {
    Serial.printf("[beach] firmware %s, entering setup portal (%s)\n", FIRMWARE_VERSION,
                  wantSetup ? "middle button held" : "nothing configured");
    clearBootFailures();          // a setup session is not a crash
    renderBegin();
    runSetupPortal(s);            // never returns
  }

  int failures = bumpBootFailures();
  bool safeMode = (failures > SAFE_MODE_THRESHOLD);
  Serial.printf("[beach] firmware %s, boot %u, %s%s\n", FIRMWARE_VERSION, (unsigned)bootCount,
                wokeByButton() ? "button wake" : "timer/power wake",
                safeMode ? ", SAFE MODE" : "");
  if (forceOta) Serial.println(F("[beach] KEY2 held: forcing an update check"));

  renderBegin();

  if (safeMode) {
    // Do nothing but try to fetch a fix.
    Serial.printf("[beach] %d consecutive failed boots; update-only mode\n", failures - 1);
    renderMessage("Updating", "This display hit a problem and is",
                  "looking for new software. Leave it on Wi-Fi.");
    char otaStatus[64] = "";
    if (netConnect(s, 25000)) {
      netSyncTime(8000);
      otaCheck(s, true, otaStatus, sizeof(otaStatus));
      netDisconnect();
    } else {
      snprintf(otaStatus, sizeof(otaStatus), "no wifi");
    }
    Serial.printf("[beach] safe-mode ota: %s\n", otaStatus);
    renderEnd();
    deepSleepFor(1800);
  }

  if (!s.configured()) {
    renderMessage("Beach Day needs setup", "Copy src/beachday_config.example.h to src/beachday_config.h,",
                  "add your Wi-Fi and location, then flash again.");
    sleepNow(3600);
  }

  int batteryPct = batteryPercent(batteryVolts());
  Serial.printf("[beach] battery ~%d%%\n", batteryPct);

  bool online = netConnect(s, 20000);
  bool clockOk = online && netSyncTime(6000);

  // First connected boot after the portal: turn the typed place into coordinates.
  if (online && s.needsGeocode()) {
    GeoResult geo;
    char gerr[64];
    if (geocode(s.locationQuery, s.countryCode, s.useTls, geo, gerr, sizeof(gerr))) {
      Serial.printf("[beach] '%s' -> %s (%.4f, %.4f)\n", s.locationQuery, geo.fullName, geo.lat, geo.lon);
      saveResolvedLocation(geo.lat, geo.lon, geo.shortName);
    } else {
      Serial.printf("[beach] geocode failed: %s\n", gerr);
      netDisconnect();
      char line[72];
      snprintf(line, sizeof(line), "Couldn't find \"%s\" - check the spelling or use a postal code.", s.locationQuery);
      renderMessage("Where's the beach?", line,
                    "Hold the middle button and press the right one to open setup.");
      sleepNow(3600);
    }
  }

  Fetched f;
  char err[64] = "no Wi-Fi";
  bool ok = online && fetchWeather(s, f, err, sizeof(err));
  netDisconnect();

  if (!ok) {
    failCount++;
    Serial.printf("[beach] fetch failed (%u in a row): %s\n", failCount, err);
    uint32_t retry = (uint32_t)s.retryMinutes * 60 * (failCount >= 6 ? 6 : 1);
    if (lastView.valid) {
      if (!staleShown) {
        // Keep the last verdict up, but say it is old.
        char stamp[32];
        snprintf(stamp, sizeof(stamp), "%s", lastView.updatedText);
        const char* at = strstr(stamp, "Updated ");
        snprintf(lastView.updatedText, sizeof(lastView.updatedText), "OFFLINE since %s",
                 at ? stamp + 8 : stamp);
        lastView.stale = true;
        renderView(lastView);
        staleShown = true;
      }
    } else {
      char line[72];
      snprintf(line, sizeof(line), "Can't get online via Wi-Fi \"%s\" (%s).", s.ssid, err);
      renderMessage("No connection yet", line,
                    "Retrying. To change Wi-Fi: hold the middle button, press the right one.");
    }
    sleepNow(retry);
  }

  // Clock: NTP if we have it, else the model's own timestamp (15-minute
  // granularity), else at least the right local date.
  time_t nowUtc = clockOk ? time(nullptr) : (f.modelNowUtc ? f.modelNowUtc : f.todayStartUtc);
  LocalClock lc = makeClock(nowUtc, f.utcOffsetSec);
  Serial.printf("[beach] local %02d:%02d dow %d dom %d (offset %ld, %s)\n",
                lc.minuteOfDay / 60, lc.minuteOfDay % 60, lc.dow, lc.dom,
                (long)f.utcOffsetSec, clockOk ? "ntp" : "model time");

  beach::Inputs in;
  beach::aggregate(f.raw, in);
  in.nowMinutes = (int16_t)lc.minuteOfDay;
  beach::Verdict v = beach::evaluate(in);

  beach::ParkingInput pin{ (int16_t)lc.minuteOfDay, (int8_t)lc.dow, (int8_t)lc.dom,
                           (int8_t)lc.tomorrowDow, (int8_t)lc.tomorrowDom, in.sunsetMinutes };
  beach::ParkingAlert pa = beach::evaluateParking(s.parking, s.parkingCount, pin);

  Serial.printf("[beach] %s  temp %d precip %d wind %d aqi %d code %d  sun[%s] temp[%d] rain[%d] wind[%d] aqi[%d] time[%d]  parking[%s]\n",
                beach::stateId(v.state), v.shown->temp, v.shown->precipProb, v.shown->wind,
                v.shown->aqi, v.shown->weatherCode, v.sun.clearingTime, v.condTemp, v.condPrecip,
                v.condWind, v.condAqi, v.condTime, pa.active ? pa.text : "-");

  buildView(in, v, pa, lc, s, batteryPct, lastView);
  Serial.printf("[beach] window today %02d:%02d-%02d:%02d  sun %d/%d hrs first %d  |  tomorrow %d/%d hrs first %d\n",
                in.sunriseMinutes / 60, in.sunriseMinutes % 60,
                in.sunsetMinutes / 60, in.sunsetMinutes % 60,
                beach::scanSun(in.today).sunHours, in.today.hourCount,
                beach::scanSun(in.today).firstSunnyHour,
                beach::scanSun(in.tomorrow).sunHours, in.tomorrow.hourCount,
                beach::scanSun(in.tomorrow).firstSunnyHour);
  renderView(lastView);
  failCount = 0;
  staleShown = false;

  // Housekeeping last: the screen is already correct, so an update that fails
  // or reboots the board costs nothing the viewer can see.
  if (forceOta || otaDue(s)) {
    char otaStatus[64] = "";
    if (netConnect(s, 20000)) {
      otaCheck(s, forceOta, otaStatus, sizeof(otaStatus));   // reboots on success
      netDisconnect();
      Serial.printf("[beach] ota: %s\n", otaStatus);
    }
  }

  sleepNow(secondsToNextWake(lc, in, s));
}

void loop() {}
