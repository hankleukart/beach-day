#include "paint.h"
#include "panel.h"
#include <cstdlib>
#include <cmath>

namespace {

inline bool isAccent(Role r) {
  return r == Role::Sun || r == Role::Water || r == Role::Leaf || r == Role::Warm;
}

// Mono only: accents become a 25% ordered pattern and the band 12.5%. Both
// only ever ADD ink, so drawing them before ink is safe. On a colour panel
// nothing is patterned - the inks exist.
inline bool patternOn(int x, int y, Role r) {
  if (r == Role::Band) return ((x & 3) == 0) && ((y & 1) == 0);
  return ((x & 1) == 0) && ((y & 1) == 0);
}
inline bool isPattern(Role r) {
  if (panelIsColor()) return false;
  return r == Role::Band || isAccent(r);
}

inline uint16_t colorFor(Role r) {
  switch (r) {
    case Role::Paper:   return panelPaper();
    case Role::Caption: return panelCaption();
    case Role::Band:    return panelPaper();
    case Role::Sun:     return panelAccent(2);
    case Role::Water:   return panelAccent(3);
    case Role::Leaf:    return panelAccent(4);
    case Role::Warm:    return panelAccent(5);
    default:            return panelInk();
  }
}

} // namespace

void paintBegin() {}

void paintPixel(int x, int y, Role r) {
  if (isPattern(r)) { if (patternOn(x, y, r)) panelPixel(x, y, panelInk()); return; }
  panelPixel(x, y, colorFor(r));
}

void paintSpan(int y, int x0, int x1, Role r) {
  if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
  if (!isPattern(r)) { panelHSpan(x0, x1, y, colorFor(r)); return; }
  for (int x = x0; x <= x1; x++) if (patternOn(x, y, r)) panelPixel(x, y, panelInk());
}

void paintRect(int x, int y, int w, int h, Role r) {
  for (int yy = y; yy < y + h; yy++) paintSpan(yy, x, x + w - 1, r);
}

void paintDisc(int cx, int cy, int r, Role role) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    paintSpan(cy + dy, cx - dx, cx + dx, role);
  }
}

void paintRoundRect(int x, int y, int w, int h, int rad, Role r) {
  if (rad * 2 > w) rad = w / 2;
  if (rad * 2 > h) rad = h / 2;
  for (int yy = 0; yy < h; yy++) {
    int inset = 0;
    if (yy < rad)              { int dy = rad - yy - 1; inset = rad - (int)(sqrtf((float)(rad * rad - dy * dy)) + 0.5f); }
    else if (yy >= h - rad)    { int dy = yy - (h - rad); inset = rad - (int)(sqrtf((float)(rad * rad - dy * dy)) + 0.5f); }
    paintSpan(y + yy, x + inset, x + w - 1 - inset, r);
  }
}

void paintRoundRectStroke(int x, int y, int w, int h, int rad, int stroke, Role r) {
  // outer minus inner, done as two fills so the corners stay round
  if (r == Role::Paper) { paintRoundRect(x, y, w, h, rad, Role::Paper); return; }
  paintRoundRect(x, y, w, h, rad, r);
  paintRoundRect(x + stroke, y + stroke, w - 2 * stroke, h - 2 * stroke, rad > stroke ? rad - stroke : 0, Role::Paper);
}

void paintThickLine(int x0, int y0, int x1, int y1, int w, Role role) {
  int r = w / 2;
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    if (r <= 0) paintPixel(x0, y0, role); else paintDisc(x0, y0, r, role);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void paintHLine(int x0, int x1, int y, int thickness, Role role) {
  for (int i = 0; i < thickness; i++) paintSpan(y + i, x0, x1, role);
}

void paintBandPanel(int x, int y, int w, int h, int rad) {
  // The design calls for a #F2F2F2 fill. Neither panel has a light grey - mono
  // would need a dot field, which destroys the type sitting on it, and the
  // 6-colour panel has no grey ink at all. An outline carries the same
  // grouping on both and costs the type nothing.
  paintRoundRectStroke(x, y, w, h, rad, 2, Role::Ink);
}

bool paintAccentsLegibleAt(int featurePx) {
  // Real ink reads at any size; a 25% dot pattern inside a 22px icon is noise.
  return panelIsColor() ? true : featurePx >= 40;
}
