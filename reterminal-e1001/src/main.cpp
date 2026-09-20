// Beach Day v3 for the Seeed reTerminal E1001.
//
// Every wake: Wi-Fi -> NTP -> Open-Meteo -> derive the day's numbers -> run
// the runtime rules (shared/v3/day-outcomes.json) -> draw -> sleep. The
// panel keeps its image while the board sleeps, so a failed fetch just leaves
// the last good screen up, stamped OFFLINE.
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>
#include "settings.h"
#include "net.h"
#include "weather.h"
#include "render.h"
#include "power.h"
#include "pins.h"
#include "ota.h"
#include "portal.h"
#include "geocode.h"
#include "specstore.h"
#include "version.h"
#include "derive.h"
#include "parking.h"
#include "devmode.h"

// Survives deep sleep (RTC slow memory), not a power cycle.
RTC_DATA_ATTR static ViewModel lastView;
RTC_DATA_ATTR static uint16_t  failCount  = 0;
RTC_DATA_ATTR static bool      staleShown = false;
RTC_DATA_ATTR static uint32_t  bootCount  = 0;

// Crash counter: bumped every boot, cleared when a cycle reaches sleep. After
// SAFE_MODE_THRESHOLD consecutive failures the board only looks for new
// firmware - the remote way out of a bad release.
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

struct LocalClock {
  time_t utc = 0; int32_t off = 0; long days = 0;
  int minuteOfDay = 0, secondOfMinute = 0;
  int dow = 0, dom = 1, tomorrowDow = 0, tomorrowDom = 1;
};

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
  c.utc = utc; c.off = off;
  c.days = localDays(utc, off);
  c.minuteOfDay = localMinuteOfDay(utc, off);
  long secs = ((long)utc + off) % 60;
  c.secondOfMinute = (int)(secs < 0 ? secs + 60 : secs);
  c.dow = (int)(((c.days % 7) + 11) % 7);          // 1970-01-01 was a Thursday
  int y, m;
  civilFromDays(c.days, y, m, c.dom);
  c.tomorrowDow = (c.dow + 1) % 7;
  civilFromDays(c.days + 1, y, m, c.tomorrowDom);
  return c;
}

// Sleep until the next regular interval or the next moment the screen would
// change (sunrise, the evening flip to tomorrow, sunset, a parking cutoff).
static uint32_t secondsToNextWake(const LocalClock& lc, const day::Raw& raw, const Settings& s) {
  const int now = lc.minuteOfDay;
  int sunrise = raw.day[0].sunriseMin, sunset = raw.day[0].sunsetMin;
  bool sunKnown = sunrise > 0 && sunset > 0;
  bool dayPeriod = !sunKnown || (now >= sunrise - 60 && now < sunset);
  int next = now + (dayPeriod ? s.wakeDayMinutes : s.wakeNightMinutes);
  auto consider = [&](int b) { if (b > now && b + 1 < next) next = b + 1; };
  if (sunKnown) { consider(sunrise); consider(sunset - 60); consider(sunset); consider(sunrise + 1440); }
  for (int i = 0; i < s.parkingCount; i++) {
    const beach::ParkingRule& r = s.parking[i];
    if (!r.enabled) continue;
    if (lc.dow == r.dayOfWeek && (r.weeksMask & (1u << (beach::weekOfMonth(lc.dom) - 1)))) consider(r.endMin);
  }
  long secs = (long)(next - now) * 60 - lc.secondOfMinute;
  if (secs < 120) secs = 120;
  return (uint32_t)secs;
}

static void fmtClock(int minutes, char* out, size_t n) { day::formatClock12(minutes, out, n); }

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

static void logScreen(const day::Screen& sc, const day::Inputs& in) {
  Serial.printf("[day] %s%s  tmin %d tmax %d swing %d rain %d wind %d aqi %d-%d hum %d-%d cloud %d clear@%d  '%s'\n",
                sc.outcome.id, sc.tomorrow ? " (tomorrow)" : "",
                (int)in.tempMinF, (int)in.tempMaxF, (int)in.tempSwingF, (int)in.precipChanceMaxPct,
                (int)in.windMaxMph, (int)in.aqiMin, (int)in.aqiMax, (int)in.humidityMinPct, (int)in.humidityMaxPct,
                (int)in.cloudCoverAvgPct, in.hasFirstClearHour ? in.firstClearHour : -1, in.conditionSummary);
  Serial.printf("[day] %s / %s | %s\n", sc.outcome.title[0], sc.outcome.tagline, sc.outcome.alsoGrab);
  for (int i = 0; i < sc.statCount; i++)
    Serial.printf("[day]   %-9s %-10s %s\n", sc.stats[i].label, sc.stats[i].value, sc.stats[i].word);
  Serial.printf("[day] %s\n", sc.footer);
}

void setup() {
  Serial.begin(115200);
  bootCount++;

  const Settings& s = loadSettings();

  pinMode(PIN_KEY1, INPUT_PULLUP);
  pinMode(PIN_KEY2, INPUT_PULLUP);
  bool wantSetup = (digitalRead(PIN_KEY1) == LOW);
  bool forceOta  = (digitalRead(PIN_KEY2) == LOW);

  if (wantSetup || !s.configured()) {
    Serial.printf("[beach] firmware %s, entering setup portal (%s)\n", FIRMWARE_VERSION,
                  wantSetup ? "middle button held" : "nothing configured");
    clearBootFailures();
    renderBegin();
    runSetupPortal(s);
  }

  int failures = bumpBootFailures();
  bool safeMode = (failures > SAFE_MODE_THRESHOLD);
  Serial.printf("[beach] firmware %s, boot %u, %s%s\n", FIRMWARE_VERSION, (unsigned)bootCount,
                wokeByButton() ? "button wake" : "timer/power wake", safeMode ? ", SAFE MODE" : "");
  if (forceOta) Serial.println(F("[beach] KEY2 held: forcing update + rules check"));

  renderBegin();

  char specStatus[96];
  if (!specLoad(specStatus, sizeof(specStatus))) {
    // Cannot happen with a valid embedded copy, but never draw a blank screen silently.
    renderMessage("Rules missing", specStatus, "Re-flash this display.");
    sleepNow(3600);
  }
  Serial.printf("[beach] rules from %s\n", specStatus);

  if (safeMode) {
    Serial.printf("[beach] %d consecutive failed boots; update-only mode\n", failures - 1);
    renderMessage("Updating", "This display hit a problem and is looking for new software.", "Leave it on Wi-Fi.");
    char st[64] = "no wifi";
    if (netConnect(s, 25000)) { netSyncTime(8000); otaCheck(s, true, st, sizeof(st)); netDisconnect(); }
    Serial.printf("[beach] safe-mode ota: %s\n", st);
    renderEnd();
    deepSleepFor(1800);
  }

  int batteryPct = batteryPercent(batteryVolts());
  Serial.printf("[beach] battery ~%d%%\n", batteryPct);

  bool online = netConnect(s, 20000);
  bool clockOk = online && netSyncTime(6000);

  if (online && s.needsGeocode()) {
    GeoResult geo; char gerr[64];
    if (geocode(s.locationQuery, s.countryCode, s.useTls, geo, gerr, sizeof(gerr))) {
      Serial.printf("[beach] '%s' -> %s (%.4f, %.4f)\n", s.locationQuery, geo.fullName, geo.lat, geo.lon);
      saveResolvedLocation(geo.lat, geo.lon, geo.shortName);
    } else {
      Serial.printf("[beach] geocode failed: %s\n", gerr);
      netDisconnect();
      char line[96];
      snprintf(line, sizeof(line), "Couldn't find \"%s\" - check the spelling or use a postal code.", s.locationQuery);
      renderMessage("Where's the beach?", line, "Hold the middle button and press the right one to open setup.");
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
        char stamp[40]; snprintf(stamp, sizeof(stamp), "%s", lastView.statusText);
        const char* at = strstr(stamp, "Updated ");
        snprintf(lastView.statusText, sizeof(lastView.statusText), "OFFLINE since %s", at ? stamp + 8 : stamp);
        lastView.stale = true;
        renderScreen(lastView);
        staleShown = true;
      }
    } else {
      char line[96];
      snprintf(line, sizeof(line), "Can't get online via Wi-Fi \"%s\" (%s).", s.ssid, err);
      renderMessage("No connection yet", line, "Retrying. To change Wi-Fi: hold the middle button, press the right one.");
    }
    sleepNow(retry);
  }

  time_t nowUtc = clockOk ? time(nullptr) : (f.modelNowUtc ? f.modelNowUtc : f.todayStartUtc);
  if (!clockOk && nowUtc > 1700000000) {
    struct timeval tv = { .tv_sec = nowUtc, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
  }
  LocalClock lc = makeClock(nowUtc, f.utcOffsetSec);

  // After the evening flip (sunset - 1h) the screen plans tomorrow. Before
  // dawn it still shows today: "Thursday" at 5 AM Thursday is right.
  int sunset = f.raw.day[0].sunsetMin;
  bool tomorrow = (sunset > 0 && lc.minuteOfDay >= sunset - 60 && f.raw.day[1].valid);
  int d = tomorrow ? 1 : 0;

  day::Inputs in;
  day::derive(f.raw, d, tomorrow ? lc.tomorrowDow : lc.dow, in);

  ViewModel vm;
  vm.valid = spec().evaluate(in, vm.screen, s.locationName, tomorrow);
  if (!vm.valid) {
    renderMessage("Rules problem", "The rules file loaded but produced no outcome.", "Check shared/v3/day-outcomes.json.");
    sleepNow(3600);
  }

  beach::ParkingInput pin{ (int16_t)lc.minuteOfDay, (int8_t)lc.dow, (int8_t)lc.dom,
                           (int8_t)lc.tomorrowDow, (int8_t)lc.tomorrowDom, (int16_t)sunset };
  beach::ParkingAlert pa = beach::evaluateParking(s.parking, s.parkingCount, pin);
  vm.parkingActive = pa.active;
  snprintf(vm.parkingText, sizeof(vm.parkingText), "%s", pa.text);

  char t[16]; fmtClock(lc.minuteOfDay, t, sizeof(t));
  snprintf(vm.statusText, sizeof(vm.statusText), "Updated %s \xC2\xB7 %d%%", t, batteryPct);

  Serial.printf("[beach] local %02d:%02d %s dom %d (offset %ld, %s)%s\n", lc.minuteOfDay / 60, lc.minuteOfDay % 60,
                day::weekdayName(lc.dow), lc.dom, (long)f.utcOffsetSec, clockOk ? "ntp" : "model time",
                pa.active ? "  PARKING" : "");
  logScreen(vm.screen, in);

  lastView = vm;
  renderScreen(lastView);
  failCount = 0;
  staleShown = false;

  // Housekeeping after the screen is right: rules first (visible next wake),
  // then firmware (reboots on success).
  bool rulesDue = forceOta || specFetchDue(s);
  bool otaWanted = forceOta || otaDue(s);
  if (rulesDue || otaWanted) {
    char st[96] = "";
    if (netConnect(s, 20000)) {
      if (rulesDue) { specFetch(s, st, sizeof(st)); Serial.printf("[beach] %s\n", st); }
      if (otaWanted) { otaCheck(s, forceOta, st, sizeof(st)); Serial.printf("[beach] ota: %s\n", st); }
      netDisconnect();
    }
  }

  sleepNow(secondsToNextWake(lc, f.raw, s));
}

void loop() {}
