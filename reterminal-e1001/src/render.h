// The v3 layout at 800x480, from the "Venice Beach Display" mock: dark hero
// panel on the left (eyebrow, icon, headline, tagline), and on the right the
// weekday, subline, four wear tiles, the also-grab line, a five-column stat
// strip and a footer pill. Everything is drawn in roles (paint.h) with the
// design's own typefaces (text.h) and vector icons (icons.h).
#pragma once
#include <cstdint>
#include "dayspec.h"

// Plain data so main can park it in RTC memory and redraw after a failed fetch.
struct ViewModel {
  bool        valid = false;
  day::Screen screen;
  bool        parkingActive = false;
  char        parkingText[40] = "";
  char        statusText[40] = "";     // "Updated 2:05 PM · 76%" / "OFFLINE since 2:05 PM"
  bool        stale = false;
};

void renderBegin();
void renderScreen(const ViewModel& vm);
void renderMessage(const char* title, const char* line1, const char* line2);
void renderSetup(const char* apName, const char* ip);
void renderEnd();
