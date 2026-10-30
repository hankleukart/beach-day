// Beach Day v3 for the Seeed reTerminal E1001.
//
// Every wake: Wi-Fi -> NTP -> Open-Meteo -> derive the day's numbers -> run
// the runtime rules (shared/v3/day-outcomes.toml) -> draw -> sleep. The
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
#include "panelmodel.h"
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

// The cycle's raw readings, at file scope so the dev-mode 'h' key can reach
// them without threading them through sleepNow.
static Fetched     g_fetched;
static day::Inputs g_inputs;
static int         g_day = 0;
static bool        g_haveFetch = false;

void devShowHourly() {
#ifdef BEACHDAY_DEV
  if (!g_haveFetch) {
    renderMessage("No readings yet", "Nothing has been fetched this boot.", "Press R to try again.");
    return;
  }
  renderHourly(g_fetched.raw, g_day, g_inputs,
               g_day ? "Tomorrow's readings" : "Today's readings", "press Green (or l) for the forecast");
#endif
}

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

// The day's pinned outcome, in NVS so a reboot or a flat battery mid-morning
// does not re-decide the day from whatever the forecast says by then.
static day::DayPin loadPin() {
  day::DayPin pin;
  Preferences p;
  if (p.begin("beachday", true)) {
    pin.epochDay = p.getLong("pinDay", -1);
    p.getString("pinId", pin.id, sizeof(pin.id));
    p.end();
  }
  return pin;
}
static void savePin(const day::DayPin& pin) {
  Preferences p;
  if (!p.begin("beachday", false)) return;
  p.putLong("pinDay", pin.epochDay);
  p.putString("pinId", pin.id);
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

// The evening flip: an hour after sunset the screen stops planning today and
// starts planning tomorrow. Clamped to the last minute of the day so a sunset
// close to midnight still flips before the date rolls over by itself.
static int eveningFlipMinute(int sunsetMin) {
  int m = sunsetMin + 60;
  return m > 1439 ? 1439 : m;
}

// Sleep until the next regular interval or the next moment the screen would
// change (sunrise, sunset, the evening flip to tomorrow, a parking cutoff).
static uint32_t secondsToNextWake(const LocalClock& lc, const day::Raw& raw, const Settings& s) {
  const int now = lc.minuteOfDay;
  int sunrise = raw.day[0].sunriseMin, sunset = raw.day[0].sunsetMin;
  bool sunKnown = sunrise > 0 && sunset > 0;
  bool dayPeriod = !sunKnown || (now >= sunrise - 60 && now < sunset);
  int next = now + (dayPeriod ? s.wakeDayMinutes : s.wakeNightMinutes);
  auto consider = [&](int b) { if (b > now && b + 1 < next) next = b + 1; };
  if (sunKnown) { consider(sunrise); consider(sunset); consider(eveningFlipMinute(sunset)); consider(sunrise + 1440); }
  for (int i = 0; i < s.parkingCount; i++) {
    const beach::ParkingRule& r = s.parking[i];
    if (!r.enabled) continue;
    if (lc.dow == r.dayOfWeek && (r.weeksMask & (1u << (beach::weekOfMonth(lc.dom) - 1)))) consider(r.endMin);
  }
  long secs = (long)(next - now) * 60 - lc.secondOfMinute;
  if (secs < 120) secs = 120;
  return (uint32_t)secs;
}

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
  Serial.printf("[day] %s / %s | %s\n", sc.outcome.title, sc.outcome.tagline, sc.outcome.alsoGrab);
  for (int i = 0; i < sc.statCount; i++)
    Serial.printf("[day]   %-9s %-10s %s\n", sc.stats[i].label, sc.stats[i].value, sc.stats[i].word);
  Serial.printf("[day] %s\n", sc.footer);
}

void setup() {
  Serial.begin(115200);
  bootCount++;

  const Settings& s = loadSettings();

  // Each button has a single-press job: the RTC records which one woke the
  // board. Holding a button through any other kind of wake still works.
  pinMode(PIN_KEY1, INPUT_PULLUP);
  pinMode(PIN_KEY2, INPUT_PULLUP);
  const WakeButton woke = wakeButton();
  bool wantSetup  = (digitalRead(PIN_KEY1) == LOW) || woke == WakeButton::LeftNav;
  bool wantHourly = (woke == WakeButton::RightNav);
  // Right Nav is a wake button itself, so "hold it while the board wakes" can
  // only ever arrive as a Right Nav wake - which used to mean readings, every
  // time. A long press tells them apart: a tap is the readings, still held
  // after two more seconds (about three from the press) is "update now". The
  // LED lights once the long press counts, so you know when to let go.
  bool forceOta = false;
  if (wantHourly) {
    const uint32_t t0 = millis();
    while (digitalRead(PIN_KEY2) == LOW && millis() - t0 < 2000) delay(20);
    if (digitalRead(PIN_KEY2) == LOW) {
      forceOta = true;
      wantHourly = false;
      pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);    // on
    }
  } else {
    forceOta = digitalRead(PIN_KEY2) == LOW;   // held through a power-on
  }

  if (wantSetup || !s.configured()) {
    Serial.printf("[beach] firmware %s, entering setup portal (%s)\n", FIRMWARE_VERSION,
                  wantSetup ? "Left Nav pressed" : "nothing configured");
    clearBootFailures();
    renderBegin();
    runSetupPortal(s);
  }

  // Only a genuine crash counts. A power cycle, the reset button or a firmware
  // upload also interrupts a boot, and those used to be counted - four flashes
  // in a day put a board into safe mode.
  int failures = 0;
  if (lastResetWasCrash()) failures = bumpBootFailures(); else clearBootFailures();
#ifdef BEACHDAY_DEV
  bool safeMode = false;       // on the bench you want to see the crash, not hide it
#else
  bool safeMode = (failures >= SAFE_MODE_THRESHOLD);
#endif
  Serial.printf("[beach] firmware %s, boot %u, %s, reset: %s%s\n", FIRMWARE_VERSION, (unsigned)bootCount,
                wokeByButton() ? "button wake" : "timer/power wake", resetReasonText(),
                safeMode ? ", SAFE MODE" : "");
  Serial.printf("[beach] panel %s (%s)\n", panelModelId(panelModel()), panelModelLabel(panelModel()));
  if (failures) Serial.printf("[beach] %d consecutive crash%s\n", failures, failures == 1 ? "" : "es");
  Serial.printf("[beach] wi-fi '%s' %s\n", s.ssid,
                s.configWon  ? "(from beachday_config.h, overriding the saved network - dev build)" :
                s.fromPortal ? "(saved by the setup portal - beachday_config.h is NOT used; press Left Nav to change it)"
                             : "(from beachday_config.h)");
  if (forceOta) Serial.println(F("[beach] Right Nav held: forcing update + rules check"));

  renderBegin();

  char specStatus[96];
  if (!specLoad(specStatus, sizeof(specStatus))) {
    // Cannot happen with a valid embedded copy, but never draw a blank screen silently.
    renderMessage("Rules missing", specStatus, "Re-flash this display.");
    sleepNow(3600);
  }
  Serial.printf("[beach] rules from %s\n", specStatus);

  if (safeMode) {
    if (failures >= SAFE_MODE_THRESHOLD * 4) {
      // Safe mode itself keeps crashing, so its own network call is suspect.
      // Sit still and say so rather than burning the battery in a reboot loop.
      Serial.println(F("[beach] safe mode is crashing too; halting until reflashed"));
      renderMessage("Needs a cable", "This display keeps restarting and can't fix itself.",
                    "Reconnect it to a computer and reflash.");
      clearBootFailures();
      renderEnd();
      deepSleepFor(6 * 3600);
    }
    Serial.println(F("[beach] update-only mode; the next boot will try normally again"));
    renderMessage("Updating", "This display hit a problem and is looking for new software.", "Leave it on Wi-Fi.");
    char st[64] = "no wifi";
    if (netConnect(s, 25000)) { netSyncTime(8000); otaCheck(s, true, st, sizeof(st)); netDisconnect(); }
    Serial.printf("[beach] safe-mode ota: %s\n", st);
    // Start the count over so safe mode is "every Nth boot", never a dead end.
    clearBootFailures();
    renderEnd();
    deepSleepFor(1800);
  }

  int batteryPct = batteryPercent(batteryVolts());
  Serial.printf("[beach] battery ~%d%%\n", batteryPct);

  bool online = netConnect(s, 20000);
  bool clockOk = online && netSyncTime(6000);

  if (forceOta) {
    digitalWrite(PIN_LED, HIGH);   // off: the long press has been taken
    // Say what is happening: a working update is otherwise a silent minute.
    renderMessage("Checking for updates", "Running firmware " FIRMWARE_VERSION ".",
                  "If there is a new one, it installs and restarts in about a minute.");
  }
  if (online && forceOta) {
    char st[96] = "";
    const OtaResult r = otaCheck(s, true, st, sizeof(st));   // restarts if it updated
    Serial.printf("[beach] ota: %s\n", st);
    char rules[96] = "";
    if (specFetch(s, rules, sizeof(rules))) Serial.printf("[beach] %s\n", rules);
    if (r != OtaResult::Updated) {
      // Up to date, or refused (battery, download): say which, then carry on.
      char why[120];
      if (r == OtaResult::UpToDate) snprintf(why, sizeof(why), "Firmware %s is the newest published.", FIRMWARE_VERSION);
      else snprintf(why, sizeof(why), "Firmware %s stays: %s.", FIRMWARE_VERSION, st);
      renderMessage(r == OtaResult::UpToDate ? "Already up to date" : "No firmware update", why,
                    "Showing the forecast in a moment.");
      delay(4000);
    }
  } else if (forceOta) {
    renderMessage("Can't check for updates", "The display isn't online.", "Check Wi-Fi with Left Nav.");
    delay(4000);
  }

  if (online && s.needsGeocode()) {
    GeoResult geo; char gerr[64];
    if (geocode(s.locationQuery, s.countryCode, s.useTls, geo, gerr, sizeof(gerr))) {
      Serial.printf("[beach] '%s' -> %s (%.4f, %.4f)\n", s.locationQuery, geo.fullName, geo.lat, geo.lon);
      saveResolvedLocation(geo.lat, geo.lon, geo.shortName);
    } else {
      Serial.printf("[beach] geocode failed: %s\n", gerr);
      netDisconnect();
      char line[140];
      snprintf(line, sizeof(line), "Couldn't find \"%s\" - check the spelling or use a postal code.", s.locationQuery);
      renderMessage("Where's the beach?", line, "Press Left Nav to open setup.");
      sleepNow(3600);
    }
  }

  Fetched& f = g_fetched;        // ~1.5 KB, file scope so the dev hook can reach it
  char err[64] = "no Wi-Fi";
  bool ok = online && fetchWeather(s, f, err, sizeof(err));
  netDisconnect();

  if (!ok) {
    failCount++;
    Serial.printf("[beach] fetch failed (%u in a row): %s\n", failCount, err);
    uint32_t retry = (uint32_t)s.retryMinutes * 60 * (failCount >= 6 ? 6 : 1);
    if (lastView.valid) {
      if (!staleShown) {
        char stamp[28]; snprintf(stamp, sizeof(stamp), "%s", lastView.updatedText);
        const char* at = strstr(stamp, "Updated ");
        snprintf(lastView.updatedText, sizeof(lastView.updatedText), "OFFLINE since %s", at ? stamp + 8 : stamp);
        lastView.stale = true;
        renderScreen(lastView);
        staleShown = true;
      }
    } else {
      char line[140];
      snprintf(line, sizeof(line), "Can't get online via Wi-Fi \"%s\" (%s).", s.ssid, err);
      renderMessage("No connection yet", line, "Retrying. To change Wi-Fi, press Left Nav.");
    }
    sleepNow(retry);
  }

  time_t nowUtc = clockOk ? time(nullptr) : (f.modelNowUtc ? f.modelNowUtc : f.todayStartUtc);
  if (!clockOk && nowUtc > 1700000000) {
    struct timeval tv = { .tv_sec = nowUtc, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
  }
  LocalClock lc = makeClock(nowUtc, f.utcOffsetSec);

  // After the evening flip (an hour past sunset) the screen plans tomorrow.
  // Before dawn it still shows today: "Thursday" at 5 AM Thursday is right.
  int sunset = f.raw.day[0].sunsetMin;
  bool tomorrow = (sunset > 0 && lc.minuteOfDay >= eveningFlipMinute(sunset) && f.raw.day[1].valid);
  int d = tomorrow ? 1 : 0;

  day::Inputs& in = g_inputs;
  day::derive(f.raw, d, tomorrow ? lc.tomorrowDow : lc.dow, in);
  g_day = d;
  g_haveFetch = true;

  // Calendar, for holiday names and for the day-stable pick among a day's
  // several names. When the screen is planning tomorrow, that is tomorrow's
  // date - a Christmas Eve evening should already say Christmas.
  {
    long days = lc.days + (tomorrow ? 1 : 0);
    int y, m, dom;
    civilFromDays(days, y, m, dom);
    in.year = y; in.month = m; in.dayOfMonth = dom;
    in.weekday = tomorrow ? lc.tomorrowDow : lc.dow;
    in.epochDay = days;
  }

  static ViewModel vm;           // ~1.4 KB
  vm.valid = spec().evaluate(in, vm.screen, s.locationName, tomorrow);

  // Decide the day once, at the first look from an hour before sunrise, and
  // hold it: the forecast high wobbling a degree across a line should not
  // repaint the day under whoever is reading it. Rain and snow (breaks_pin in
  // the rules) still get through. Tomorrow's evening preview is never pinned.
  if (vm.valid) {
    const int sunrise = f.raw.day[0].sunriseMin > 0 ? f.raw.day[0].sunriseMin : day::FALLBACK_START_HOUR * 60;
    const bool pinnable = !tomorrow && lc.minuteOfDay >= sunrise - 60;
    day::DayPin pin = loadPin();
    char show[24], live[24];
    snprintf(live, sizeof(live), "%s", vm.screen.outcome.id);
    if (day::resolvePin(spec(), pin, in.epochDay, pinnable, live, show, sizeof(show))) {
      savePin(pin);
      Serial.printf("[pin] %s pinned for the day\n", pin.id);
    }
    if (strcmp(show, live) != 0) {
      vm.valid = spec().evaluate(in, vm.screen, s.locationName, tomorrow, show);
      Serial.printf("[pin] holding %s (the forecast now says %s)\n", show, live);
    }
  }
  if (!vm.valid) {
    renderMessage("Rules problem", "The rules file loaded but produced no outcome.", "Check shared/v3/day-outcomes.toml.");
    sleepNow(3600);
  }

  beach::ParkingInput pin{ (int16_t)lc.minuteOfDay, (int8_t)lc.dow, (int8_t)lc.dom,
                           (int8_t)lc.tomorrowDow, (int8_t)lc.tomorrowDom, (int16_t)sunset };
  beach::ParkingAlert pa = beach::evaluateParking(s.parking, s.parkingCount, pin);
  // Street cleaning, else the weekly reminder. By the clock, not the forecast:
  // on trash night the bar stays up after the evening flip to tomorrow.
  const char* bar = beach::barText(pa, s.reminder, lc.dow);
  const bool reminding = bar && !pa.active;
  vm.parkingActive = bar != nullptr;
  vm.barIsReminder = reminding;
  snprintf(vm.parkingText, sizeof(vm.parkingText), "%s", bar ? bar : "");

  // "9:40a", matching the stat tiles: the corner is a quiet aside, not a
  // heading. (The OFFLINE stamp slices this at "Updated ", 8 characters.)
  {
    const int h = (lc.minuteOfDay / 60) % 24, m = lc.minuteOfDay % 60;
    int dh = h % 12; if (dh == 0) dh = 12;
    snprintf(vm.updatedText, sizeof(vm.updatedText), "Updated %d:%02d%c", dh, m, h >= 12 ? 'p' : 'a');
  }
  vm.batteryPct = (int8_t)batteryPct;

  Serial.printf("[beach] local %02d:%02d %s dom %d (offset %ld, %s)%s\n", lc.minuteOfDay / 60, lc.minuteOfDay % 60,
                day::weekdayName(lc.dow), lc.dom, (long)f.utcOffsetSec, clockOk ? "ntp" : "model time",
                pa.active ? "  PARKING" : reminding ? "  REMINDER" : "");
  logScreen(vm.screen, in);

  Serial.printf("[beach] free heap %u, stack headroom %u bytes\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)uxTaskGetStackHighWaterMark(nullptr));

  if (wantHourly) {
    // Show the readings instead of the verdict, then sleep as usual - the
    // next ordinary wake puts the dashboard back.
    Serial.println(F("[beach] Right Nav: showing the hourly readings"));
    // The version lives here: this is the screen you open to check what the
    // board is running, and the setup portal is only on someone's phone.
    char foot[72];
    snprintf(foot, sizeof(foot), "%s \xC2\xB7 firmware %s \xC2\xB7 any button returns", vm.updatedText, FIRMWARE_VERSION);
    renderHourly(f.raw, d, in, tomorrow ? "Tomorrow's readings" : "Today's readings", foot);
    failCount = 0;
    clearBootFailures();
    renderEnd();
    deepSleepFor(secondsToNextWake(lc, f.raw, s));
  }

  lastView = vm;
  renderScreen(lastView);
  failCount = 0;
  staleShown = false;

  // Housekeeping after the screen is right: rules first (visible next wake),
  // then firmware (reboots on success).
  bool rulesDue = !forceOta && specFetchDue(s);
  bool otaWanted = !forceOta && otaDue(s);
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
