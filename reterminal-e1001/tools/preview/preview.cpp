// Render the real layout to an image on a Mac.
//
//     tools/preview.sh                 # every scenario, both panels
//     tools/preview.sh rain mono
//
// This links src/render.cpp itself rather than reimplementing the layout, so
// what comes out is what the panel draws - same type ramp, same icons, same
// dithering, same fitting decisions. It exists because the alternative is
// reflashing a board to find out that a line wrapped.
#include <Arduino.h>            // the shim; defines PreviewSerial
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include "render.h"
#include "version.h"
#include "dayspec.h"
#include "derive.h"

PreviewSerial Serial;
void previewSetColor(bool on);
bool previewSave(const char* path);

#ifndef DAYRULES_SPEC_PATH
#define DAYRULES_SPEC_PATH "../shared/v3/day-outcomes.toml"
#endif

namespace {

struct Scenario {
  const char* name;
  int  sunriseMin, sunsetMin;
  int  tempLoF, tempHiF;         // the daily swing, shaped over the window
  int  precipPct;                // peak chance, mid-afternoon
  int  cloudPct;
  int  windMph;
  int  aqi;
  int  code;                     // WMO
  bool stressCopy;               // force two-line copy to test the fitting
  bool findWhimsy = false;       // move to the first date that swaps a tile
  const char* bar = nullptr;     // a footer reminder to show, at its default wording
};

const Scenario SCENARIOS[] = {
  // A hot clear midsummer day: Beach Day, and the longest readings table.
  { "beach",  5 * 60 + 12, 20 * 60 + 31, 62, 84,  5, 10,  8, 32, 0, false },
  // Rain over 50% picks a rain outcome and one of its variants.
  { "rain",   6 * 60 + 40, 19 * 60 + 10, 51, 58, 85, 95, 14, 18, 61, false },
  // Short winter daylight: the readings table at its shortest.
  { "winter", 7 * 60 + 12, 16 * 60 + 33, 28, 41, 10, 70, 20, 44, 3, false },
  // Hazy and hot, so the AIR tile has something to say.
  { "smoke",  5 * 60 + 40, 20 * 60 +  5, 70, 97,  0, 20,  6, 148, 45, false },
  // Worst-case copy: everything that can wrap to two lines does, which is
  // where the stat band runs out of height.
  { "stress", 5 * 60 + 12, 20 * 60 + 31, 62, 84,  5, 10,  8, 32, 0, true },
  // Below freezing with snow in the forecast: the full-snow-gear square.
  { "snow",   7 * 60 + 15, 16 * 60 + 40, 18, 29, 70, 95, 10, 25, 73, false },
  // The beach day a week later, on a date that swaps one tile for whimsy.
  { "whimsy", 5 * 60 + 12, 20 * 60 + 31, 62, 84,  5, 10,  8, 32, 0, false, true },
  // Hot and gusty: a square named after things (Kite, Sailboat), to check the
  // hero shows the thing's own icon rather than the outcome's.
  { "kite",   5 * 60 + 40, 19 * 60 + 50, 66, 86,  5, 20, 21, 30, 1, false },
  // Trash night: the longest default text the footer bar has to carry.
  { "trash",  6 * 60 + 40, 19 * 60 + 10, 58, 72,  5, 30, 10, 30, 2, false, false,
    "Take your trash to the street tonight" },
};

// A plausible day rather than a flat line, so the readings table and the
// derived min/max actually differ.
void buildRaw(const Scenario& sc, day::Raw& raw) {
  raw = day::Raw{};
  for (int d = 0; d < 2; d++) {
    raw.day[d].valid = true;
    raw.day[d].sunriseMin = (int16_t)sc.sunriseMin;
    raw.day[d].sunsetMin  = (int16_t)sc.sunsetMin;
    raw.day[d].dailyCode  = (int16_t)sc.code;
  }
  const int h0 = sc.sunriseMin / 60, h1 = sc.sunsetMin / 60;
  int n = 0, na = 0;
  for (int h = 0; h < 24 && n < day::MAX_ROWS; h++) {
    if (h < h0 || h > h1) continue;                 // only the window is fetched here
    const float t = (float)(h - h0) / (float)(h1 - h0 > 0 ? h1 - h0 : 1);
    // Coolest at dawn, peak around 3pm, easing after.
    const float bell = 1.0f - (t - 0.68f) * (t - 0.68f) / 0.46f;
    const int temp = sc.tempLoF + (int)((sc.tempHiF - sc.tempLoF) * (bell < 0 ? 0 : bell));
    const int pp   = (h >= 13 && h <= 16) ? sc.precipPct : sc.precipPct / 4;
    const int cl   = (h >= 12 && h <= 17) ? sc.cloudPct : sc.cloudPct / 3;
    const int hum  = 88 - (int)(39 * (bell < 0 ? 0 : bell));
    raw.hours[n++] = day::HourRow{ 0, (int8_t)h, (double)temp, (double)(sc.windMph - 3 + (h % 5)),
                                   (int16_t)hum, (int16_t)pp, (int16_t)cl, (int16_t)sc.code };
    raw.aqi[na++] = day::AqiRow{ 0, (int8_t)h, (int16_t)(sc.aqi - 6 + (h % 11)) };
  }
  raw.n = n; raw.na = na;
}

// 2026-06-17, a Wednesday, plus an offset that stays inside June.
void fillDate(day::Inputs& in, int offset) {
  in.year = 2026; in.month = 6; in.dayOfMonth = 17 + offset;
  in.weekday = (3 + offset) % 7; in.epochDay = 20621 + offset;
}

// The first offset whose tiles differ from the same rules with whimsy off.
// Searched rather than hardcoded, so retuning [whimsy] one_in cannot quietly
// turn the scenario into an ordinary day.
int firstWhimsyOffset(const day::Spec& spec, const std::string& text, day::Inputs in) {
  std::string plainText = text;
  // The table header on a line of its own - the comments mention [whimsy] too.
  size_t at = plainText.find("\n[whimsy]\n");
  if (at == std::string::npos) return 0;
  at += 1;
  size_t end = plainText.find("\n\n", at);
  plainText.erase(at, (end == std::string::npos ? plainText.size() : end) - at);
  static day::Spec plain;
  char err[256] = "";
  if (!plain.load(plainText.c_str(), plainText.size(), err, sizeof(err))) {
    std::fprintf(stderr, "whimsy: rules without [whimsy] did not load: %s\n", err);
    return 0;
  }
  for (int off = 0; off < 13; off++) {
    fillDate(in, off);
    static day::Screen a, b;
    if (!spec.evaluate(in, a, nullptr, false) || !plain.evaluate(in, b, nullptr, false)) continue;
    for (int i = 0; i < a.outcome.wearCount; i++)
      if (std::strcmp(a.outcome.wear[i].label, b.outcome.wear[i].label)) {
        std::fprintf(stderr, "whimsy: day +%d swaps \"%s\" for \"%s\"\n", off, b.outcome.wear[i].label, a.outcome.wear[i].label);
        return off;
      }
  }
  std::fprintf(stderr, "whimsy: no whimsical day in the next 13\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const char* want  = argc > 1 ? argv[1] : "beach";
  const bool  color = argc > 2 && !std::strcmp(argv[2], "color");
  const char* outDash = argc > 3 ? argv[3] : "preview-dashboard.ppm";
  const char* outRead = argc > 4 ? argv[4] : "preview-readings.ppm";

  const Scenario* sc = nullptr;
  for (const auto& s : SCENARIOS) if (!std::strcmp(s.name, want)) sc = &s;
  if (!sc) { std::fprintf(stderr, "unknown scenario '%s'\n", want); return 2; }

  std::ifstream f(DAYRULES_SPEC_PATH);
  if (!f) { std::fprintf(stderr, "cannot open %s\n", DAYRULES_SPEC_PATH); return 2; }
  std::stringstream ss; ss << f.rdbuf();
  std::string text = ss.str();

  static day::Spec spec;
  char err[256] = "";
  if (!spec.load(text.c_str(), text.size(), err, sizeof(err))) {
    std::fprintf(stderr, "spec load failed: %s\n", err);
    return 1;
  }

  day::Raw raw;
  buildRaw(*sc, raw);
  day::Inputs in;
  day::derive(raw, 0, 3, in);
  fillDate(in, sc->findWhimsy ? firstWhimsyOffset(spec, text, in) : 0);

  ViewModel vm;
  if (!spec.evaluate(in, vm.screen, nullptr, false)) {
    std::fprintf(stderr, "evaluate failed\n");
    return 1;
  }
  vm.valid = true;
  vm.batteryPct = 96;
  std::snprintf(vm.updatedText, sizeof(vm.updatedText), "Updated 9:40a");

  if (sc->bar) {
    vm.parkingActive = true;
    vm.barIsReminder = true;
    std::snprintf(vm.parkingText, sizeof(vm.parkingText), "%s", sc->bar);
  }
  if (sc->stressCopy) {
    // Long enough to wrap in both places, which is what squeezes the band.
    std::snprintf(vm.screen.outcome.alsoGrab, sizeof(vm.screen.outcome.alsoGrab),
                  "Also grab: dry socks for the backpack, and the wide-brimmed "
                  "hat that lives by the back door.");
    vm.parkingActive = true;
    std::snprintf(vm.parkingText, sizeof(vm.parkingText), "Street cleaning 10am-12pm");
  }

  previewSetColor(color);
  renderBegin();

  renderScreen(vm);
  if (!previewSave(outDash)) { std::fprintf(stderr, "cannot write %s\n", outDash); return 1; }

  // The board's own footer for this screen (main.cpp).
  char foot[72];
  std::snprintf(foot, sizeof(foot), "%s \xC2\xB7 firmware %s \xC2\xB7 any button returns", vm.updatedText, FIRMWARE_VERSION);
  renderHourly(raw, 0, in, "Today's readings", foot);
  if (!previewSave(outRead)) { std::fprintf(stderr, "cannot write %s\n", outRead); return 1; }

  renderEnd();
  std::fprintf(stderr, "%-7s %-5s  %s  %s\n", sc->name, color ? "color" : "mono", outDash, outRead);
  return 0;
}
