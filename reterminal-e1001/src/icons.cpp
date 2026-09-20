#include "icons.h"
#include "icons_data.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>

namespace {

struct Ctx {
  float k;            // px per design unit
  int ox, oy;         // top-left of the box
  int w;              // stroke px
  IconStyle st;
};

Role roleFor(const Ctx& c, uint8_t role) {
  switch (role) {
    case 0: return c.st.ink;
    case 1: return c.st.paper;
    case 2: return Role::Sun;
    case 3: return Role::Water;
    case 4: return Role::Leaf;
    default: return Role::Warm;
  }
}
bool isAccent(uint8_t role) { return role >= 2; }

inline int X(const Ctx& c, int16_t hu) { return c.ox + (int)lroundf(hu * 0.5f * c.k); }
inline int Y(const Ctx& c, int16_t hu) { return c.oy + (int)lroundf(hu * 0.5f * c.k); }
inline int L(const Ctx& c, int16_t hu) { return (int)lroundf(hu * 0.5f * c.k); }

void polyFill(const Ctx& c, const int16_t* v, int n, Role r) {
  // even-odd scanline fill over n/2 points
  int pts = n / 2;
  int ymin = 1 << 20, ymax = -(1 << 20);
  for (int i = 0; i < pts; i++) { int y = Y(c, v[2 * i + 1]); if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
  for (int y = ymin; y <= ymax; y++) {
    int xs[32]; int nx = 0;
    for (int i = 0; i < pts && nx < 32; i++) {
      int j = (i + 1) % pts;
      int x0 = X(c, v[2 * i]), y0 = Y(c, v[2 * i + 1]), x1 = X(c, v[2 * j]), y1 = Y(c, v[2 * j + 1]);
      if (y0 == y1) continue;
      if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) xs[nx++] = x0 + (int)lroundf((float)(y - y0) * (x1 - x0) / (float)(y1 - y0));
    }
    for (int i = 1; i < nx; i++) { int t = xs[i], j = i - 1; while (j >= 0 && xs[j] > t) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = t; }
    for (int i = 0; i + 1 < nx; i += 2) paintSpan(y, xs[i], xs[i + 1], r);
  }
}

void ellipseFill(const Ctx& c, int x0, int y0, int x1, int y1, Role r) {
  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, rx = (x1 - x0) * 0.5f, ry = (y1 - y0) * 0.5f;
  if (rx < 0.5f || ry < 0.5f) return;
  for (int y = y0; y <= y1; y++) {
    float t = (y - cy) / ry; if (t < -1) t = -1; if (t > 1) t = 1;
    int dx = (int)lroundf(rx * sqrtf(1 - t * t));
    paintSpan(y, (int)lroundf(cx - dx), (int)lroundf(cx + dx), r);
  }
}

void ellipseStroke(const Ctx& c, int x0, int y0, int x1, int y1, Role r) {
  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, rx = (x1 - x0) * 0.5f, ry = (y1 - y0) * 0.5f;
  int steps = (int)fmaxf(16, (rx + ry) * 1.2f);
  int px = 0, py = 0;
  for (int i = 0; i <= steps; i++) {
    float a = 2 * (float)M_PI * i / steps;
    int x = (int)lroundf(cx + rx * cosf(a)), y = (int)lroundf(cy + ry * sinf(a));
    if (i) paintThickLine(px, py, x, y, c.w, r);
    px = x; py = y;
  }
}

void arcStroke(const Ctx& c, int cx, int cy, int rad, int a0, int a1, Role r) {
  if (a1 < a0) a1 += 360;
  int steps = (int)fmaxf(4, (a1 - a0) / 360.0f * fmaxf(16, rad * 1.5f));
  int px = 0, py = 0;
  for (int i = 0; i <= steps; i++) {
    float a = (a0 + (a1 - a0) * (float)i / steps) * (float)M_PI / 180.0f;
    int x = (int)lroundf(cx + rad * cosf(a)), y = (int)lroundf(cy + rad * sinf(a));
    if (i) paintThickLine(px, py, x, y, c.w, r);
    px = x; py = y;
  }
}

void pieFill(const Ctx& c, int cx, int cy, int rad, int a0, int a1, Role r) {
  if (a1 < a0) a1 += 360;
  int16_t buf[2 + 2 * 40];
  int steps = 38;
  buf[0] = 0; buf[1] = 0;   // placeholder, filled below in px space via a local polyFill variant
  // build a polygon in pixel space and fill with the same scanline routine
  int pts[2 * 42]; int n = 0;
  pts[n++] = cx; pts[n++] = cy;
  for (int i = 0; i <= steps; i++) {
    float a = (a0 + (a1 - a0) * (float)i / steps) * (float)M_PI / 180.0f;
    pts[n++] = (int)lroundf(cx + rad * cosf(a)); pts[n++] = (int)lroundf(cy + rad * sinf(a));
  }
  int count = n / 2;
  int ymin = 1 << 20, ymax = -(1 << 20);
  for (int i = 0; i < count; i++) { if (pts[2 * i + 1] < ymin) ymin = pts[2 * i + 1]; if (pts[2 * i + 1] > ymax) ymax = pts[2 * i + 1]; }
  for (int y = ymin; y <= ymax; y++) {
    int xs[48]; int nx = 0;
    for (int i = 0; i < count && nx < 48; i++) {
      int j = (i + 1) % count;
      int x0 = pts[2 * i], y0 = pts[2 * i + 1], x1 = pts[2 * j], y1 = pts[2 * j + 1];
      if (y0 == y1) continue;
      if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) xs[nx++] = x0 + (int)lroundf((float)(y - y0) * (x1 - x0) / (float)(y1 - y0));
    }
    for (int i = 1; i < nx; i++) { int t = xs[i], j = i - 1; while (j >= 0 && xs[j] > t) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = t; }
    for (int i = 0; i + 1 < nx; i += 2) paintSpan(y, xs[i], xs[i + 1], r);
  }
  (void)buf;
}

void rrectStroke(const Ctx& c, int x0, int y0, int x1, int y1, int rad, Role r) {
  int w = x1 - x0, h = y1 - y0;
  if (rad * 2 > w) rad = w / 2;
  if (rad * 2 > h) rad = h / 2;
  paintThickLine(x0 + rad, y0, x1 - rad, y0, c.w, r);
  paintThickLine(x0 + rad, y1, x1 - rad, y1, c.w, r);
  paintThickLine(x0, y0 + rad, x0, y1 - rad, c.w, r);
  paintThickLine(x1, y0 + rad, x1, y1 - rad, c.w, r);
  if (rad > 0) {
    arcStroke(c, x0 + rad, y0 + rad, rad, 180, 270, r);
    arcStroke(c, x1 - rad, y0 + rad, rad, 270, 360, r);
    arcStroke(c, x1 - rad, y1 - rad, rad, 0, 90, r);
    arcStroke(c, x0 + rad, y1 - rad, rad, 90, 180, r);
  }
}

// union shape lists: count, then per shape: kind(0 disc cx cy r | 1 rrect x0 y0 x1 y1 rad)
void unionFill(const Ctx& c, const int16_t* v, Role r, int inset) {
  int count = v[0]; const int16_t* p = v + 1;
  for (int i = 0; i < count; i++) {
    if (p[0] == 0) {
      int rad = L(c, p[3]) - inset;
      if (rad > 0) paintDisc(X(c, p[1]), Y(c, p[2]), rad, r);
      p += 4;
    } else {
      int x0 = X(c, p[1]) + inset, y0 = Y(c, p[2]) + inset, x1 = X(c, p[3]) - inset, y1 = Y(c, p[4]) - inset;
      int rad = L(c, p[5]) - inset; if (rad < 0) rad = 0;
      if (x1 > x0 && y1 > y0) paintRoundRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, rad, r);
      p += 6;
    }
  }
}

void run(const Ctx& c, const IcCmd& cmd) {
  if (isAccent(cmd.role) && !c.st.accents) return;
  Role r = roleFor(c, cmd.role);
  const int16_t* v = cmd.v;
  switch (cmd.op) {
    case IC_LINE:
      for (int i = 0; i + 3 < cmd.n; i += 2) paintThickLine(X(c, v[i]), Y(c, v[i + 1]), X(c, v[i + 2]), Y(c, v[i + 3]), c.w, r);
      if (cmd.n == 2) paintDisc(X(c, v[0]), Y(c, v[1]), c.w / 2, r);
      break;
    case IC_LINEW: {
      int w = (int)fmaxf(1, lroundf(v[0] / 10.0f * c.k));
      for (int i = 1; i + 3 < cmd.n; i += 2) paintThickLine(X(c, v[i]), Y(c, v[i + 1]), X(c, v[i + 2]), Y(c, v[i + 3]), w, r);
      break;
    }
    case IC_ARC:          arcStroke(c, X(c, v[0]), Y(c, v[1]), L(c, v[2]), v[3], v[4], r); break;
    case IC_CIRCLE:       arcStroke(c, X(c, v[0]), Y(c, v[1]), L(c, v[2]), 0, 360, r); break;
    case IC_DISC:         paintDisc(X(c, v[0]), Y(c, v[1]), L(c, v[2]), r); break;
    case IC_ELLIPSE:      ellipseStroke(c, X(c, v[0]), Y(c, v[1]), X(c, v[2]), Y(c, v[3]), r); break;
    case IC_ELLIPSE_FILL: ellipseFill(c, X(c, v[0]), Y(c, v[1]), X(c, v[2]), Y(c, v[3]), r); break;
    case IC_RRECT:        rrectStroke(c, X(c, v[0]), Y(c, v[1]), X(c, v[2]), Y(c, v[3]), L(c, v[4]), r); break;
    case IC_RRECT_FILL: {
      int x0 = X(c, v[0]), y0 = Y(c, v[1]), x1 = X(c, v[2]), y1 = Y(c, v[3]);
      paintRoundRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, L(c, v[4]), r); break;
    }
    case IC_POLY: {
      int pts = cmd.n / 2;
      for (int i = 0; i < pts; i++) { int j = (i + 1) % pts; paintThickLine(X(c, v[2 * i]), Y(c, v[2 * i + 1]), X(c, v[2 * j]), Y(c, v[2 * j + 1]), c.w, r); }
      break;
    }
    case IC_POLY_FILL:    polyFill(c, v, cmd.n, r); break;
    case IC_PIE:          pieFill(c, X(c, v[0]), Y(c, v[1]), L(c, v[2]), v[3], v[4], r); break;
    case IC_UNION_OUTLINE: unionFill(c, v, r, 0); unionFill(c, v, c.st.paper, c.w); break;
    case IC_UNION_FILL:   unionFill(c, v, r, c.w); break;
  }
}

const IconDef* find(const char* name) {
  for (int i = 0; i < ICON_DEF_COUNT; i++) if (strcmp(ICON_DEFS[i].name, name) == 0) return &ICON_DEFS[i];
  return nullptr;
}

} // namespace

bool iconExists(const char* name) { return name && find(name) != nullptr; }

void drawIcon(const char* name, int cx, int cy, int size, const IconStyle& style) {
  const IconDef* d = name ? find(name) : nullptr;
  Ctx c;
  c.k = size / 48.0f;
  c.ox = cx - size / 2; c.oy = cy - size / 2;
  c.w = (int)fmaxf(2, lroundf(2.6f * c.k * 1.05f));
  c.st = style;
  if (!d) {   // unknown name: an obvious placeholder rather than nothing
    paintRoundRectStroke(c.ox + size / 6, c.oy + size / 6, size * 2 / 3, size * 2 / 3, size / 8, c.w, style.ink);
    paintThickLine(c.ox + size / 4, c.oy + size / 4, c.ox + size * 3 / 4, c.oy + size * 3 / 4, c.w, style.ink);
    return;
  }
  // accents first, then ink on top - matches the generator's preview
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < d->count; i++) {
      const IcCmd& cmd = d->cmds[i];
      bool accent = isAccent(cmd.role);
      if ((pass == 0) != accent) continue;
      run(c, cmd);
    }
  }
}
