// The one place pixels get a colour. Everything above draws in ROLES; this
// maps them to what the panel can show. On the mono E1001: ink/caption are
// black, paper is white, colour accents and the band are dot patterns. A colour
// panel (E1002) gets a second table here and nothing above it changes.
#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>

enum class Role : uint8_t { Ink = 0, Paper = 1, Sun = 2, Water = 3, Leaf = 4, Warm = 5, Band = 6, Caption = 7 };

void paintBegin(Adafruit_GFX* gfx);
void paintPixel(int x, int y, Role r);
void paintSpan(int y, int x0, int x1, Role r);                       // inclusive
void paintRect(int x, int y, int w, int h, Role r);
void paintRoundRect(int x, int y, int w, int h, int rad, Role r);   // filled
void paintRoundRectStroke(int x, int y, int w, int h, int rad, int stroke, Role r);
void paintDisc(int cx, int cy, int r, Role role);
void paintThickLine(int x0, int y0, int x1, int y1, int w, Role role);
void paintHLine(int x0, int x1, int y, int thickness, Role role);
