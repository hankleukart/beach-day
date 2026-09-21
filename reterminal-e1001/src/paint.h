// The one place pixels get a colour. Everything above draws in ROLES; this
// maps them to what the panel can show. On the mono E1001: ink/caption are
// black, paper is white, colour accents and the band are dot patterns. A colour
// panel (E1002) gets a second table here and nothing above it changes.
#pragma once
#include <cstdint>

enum class Role : uint8_t { Ink = 0, Paper = 1, Sun = 2, Water = 3, Leaf = 4, Warm = 5, Band = 6, Caption = 7 };

void paintBegin();
void paintPixel(int x, int y, Role r);
void paintSpan(int y, int x0, int x1, Role r);                       // inclusive
void paintRect(int x, int y, int w, int h, Role r);
void paintRoundRect(int x, int y, int w, int h, int rad, Role r);   // filled
void paintRoundRectStroke(int x, int y, int w, int h, int rad, int stroke, Role r);
void paintDisc(int cx, int cy, int r, Role role);
void paintThickLine(int x0, int y0, int x1, int y1, int w, Role role);
void paintHLine(int x0, int x1, int y, int thickness, Role role);

// A large flat field of one ink is the thing this panel does well, so the hero
// gets a colour and everything else stays black on white. Mono has no way to
// show a tint without dithering under type, so it collapses to the light/dark
// panel it already had - the monochrome layout is unchanged.
enum class Tint : uint8_t { None, Yellow, Blue, Red, Green };

struct HeroSurface {
  Role bg;        // fill
  Role fg;        // type and icon strokes
  bool accents;   // let the icon use its own inks (only on a neutral field)
};
// preferLight: the outcome's light/dark choice, which is what mono obeys.
HeroSurface paintHeroSurface(Tint tint, bool preferLight);

// A solid bar that means "act on this". Red where red exists.
void paintAlertBar(int x, int y, int w, int h, int rad);
Role paintAlertTextRole();

// The stat strip's backing. On a 1-bit panel a dot field behind type destroys
// it, so this is an outline here; a colour panel fills it with the band tone.
void paintBandPanel(int x, int y, int w, int h, int rad);
// False when an accent fill would read as noise rather than colour at this size.
bool paintAccentsLegibleAt(int featurePx);
