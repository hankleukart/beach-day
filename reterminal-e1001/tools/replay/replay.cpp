// Replays archived forecasts through the real engine: the same derive() and
// Spec::evaluate() the board runs, against the same rules file. Driven by
// tools/replay.sh, which does the fetching and the bookkeeping; this end only
// turns rows into an outcome.
//
// stdin, one evaluation at a time:
//   E <key> <epochDay> <year> <month> <dom> <dow> <sunriseMin> <sunsetMin> <dailyCode> <nHours> <nAqi> <pinnable>
//   H <hour> <tempF> <windMph> <humidity> <precipProb> <cloud> <code>     x nHours
//   A <hour> <aqi>                                                       x nAqi
// stdout, one line each:
//   <key> <shownId> <liveId> <tempMax> <tempMin> <swing> <rain> <wind> <cloud> <aqi> <sunHours> <snow> | <title>
// Records arrive in wake order, so the morning pin carries across them
// exactly as it does across the board's wakes - through the same resolvePin.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include "dayspec.h"
#include "derive.h"

int main(int argc, char** argv) {
  const char* specPath = argc > 1 ? argv[1] : DAYRULES_SPEC_PATH;
  std::ifstream f(specPath);
  if (!f) { fprintf(stderr, "replay: cannot open %s\n", specPath); return 2; }
  std::stringstream ss; ss << f.rdbuf();
  const std::string text = ss.str();

  static day::Spec spec;
  char err[160];
  if (!spec.load(text.c_str(), text.size(), err, sizeof(err))) {
    fprintf(stderr, "replay: spec rejected: %s\n", err);
    return 2;
  }

  char tag[2];
  while (scanf("%1s", tag) == 1) {
    if (tag[0] != 'E') { fprintf(stderr, "replay: expected E, got %s\n", tag); return 2; }
    char key[64];
    long epochDay; int year, month, dom, dow, sunrise, sunset, dailyCode, nh, na, pinnable;
    if (scanf("%63s %ld %d %d %d %d %d %d %d %d %d %d", key, &epochDay, &year, &month, &dom, &dow,
              &sunrise, &sunset, &dailyCode, &nh, &na, &pinnable) != 12) { fprintf(stderr, "replay: bad E\n"); return 2; }

    static day::Raw raw;
    raw = day::Raw();
    raw.day[0].valid = true;
    raw.day[0].sunriseMin = (int16_t)sunrise;
    raw.day[0].sunsetMin = (int16_t)sunset;
    raw.day[0].dailyCode = (int16_t)dailyCode;
    for (int i = 0; i < nh; i++) {
      int hour, hum, pp, cloud, code; double t, w;
      if (scanf("%1s %d %lf %lf %d %d %d %d", tag, &hour, &t, &w, &hum, &pp, &cloud, &code) != 8) {
        fprintf(stderr, "replay: bad H in %s\n", key); return 2;
      }
      if (raw.n >= day::MAX_ROWS) continue;
      day::HourRow& r = raw.hours[raw.n++];
      r.day = 0; r.hour = (int8_t)hour; r.tempF = t; r.windMph = w;
      r.humidity = (int16_t)hum; r.precipProb = (int16_t)pp; r.cloud = (int16_t)cloud; r.code = (int16_t)code;
    }
    for (int i = 0; i < na; i++) {
      int hour, aqi;
      if (scanf("%1s %d %d", tag, &hour, &aqi) != 3) { fprintf(stderr, "replay: bad A in %s\n", key); return 2; }
      if (raw.na < day::MAX_ROWS) raw.aqi[raw.na++] = day::AqiRow{ 0, (int8_t)hour, (int16_t)aqi };
    }

    static day::Inputs in;
    in = day::Inputs();
    day::derive(raw, 0, dow, in);
    in.year = year; in.month = month; in.dayOfMonth = dom; in.weekday = dow; in.epochDay = epochDay;

    static day::Screen screen;
    screen = day::Screen();
    if (!spec.evaluate(in, screen, nullptr, false)) { printf("%s ERROR ERROR 0 0 0 0 0 0 0 0 0 | -\n", key); continue; }
    static day::DayPin pin;
    char live[24], show[24];
    snprintf(live, sizeof(live), "%s", screen.outcome.id);
    day::resolvePin(spec, pin, epochDay, pinnable != 0, live, show, sizeof(show));
    if (strcmp(show, live) != 0) spec.evaluate(in, screen, nullptr, false, show);
    printf("%s %s %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f | %s\n", key, screen.outcome.id, live,
           in.tempMaxF, in.tempMinF, in.tempSwingF, in.precipChanceMaxPct, in.windMaxMph,
           in.cloudCoverAvgPct, in.aqiMax, in.sunRunHours, in.snowHours, screen.outcome.title);
  }
  return 0;
}
