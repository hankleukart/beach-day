// A panel that draws into memory instead of onto glass, so the layout can be
// looked at on a Mac. Colours are the same RGB565 values GxEPD2 uses, and the
// mono/colour switch is the one the real panels make, so dithering and ink
// choice take exactly the paths they take on hardware.
#include "panel.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr uint16_t C_BLACK = 0x0000, C_WHITE = 0xFFFF, C_RED = 0xF800,
                   C_YELLOW = 0xFFE0, C_BLUE = 0x001F, C_GREEN = 0x07E0;
uint16_t fb[PANEL_W * PANEL_H];
bool isColor = false;
bool drawn = false;
}

void previewSetColor(bool on) { isColor = on; }

// P6 PPM: no library needed here, and the tool script turns it into a PNG.
bool previewSave(const char* path) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", PANEL_W, PANEL_H);
  for (int i = 0; i < PANEL_W * PANEL_H; i++) {
    const uint16_t c = fb[i];
    const unsigned char rgb[3] = {
      (unsigned char)(((c >> 11) & 0x1F) * 255 / 31),
      (unsigned char)(((c >> 5)  & 0x3F) * 255 / 63),
      (unsigned char)(( c        & 0x1F) * 255 / 31),
    };
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
  return true;
}

void panelBegin() { panelFill(C_WHITE); }
void panelFirstPage() { drawn = false; }
bool panelNextPage() { drawn = !drawn; return false; }   // one pass, never repaged
void panelHibernate() {}

void panelFill(uint16_t c) { for (int i = 0; i < PANEL_W * PANEL_H; i++) fb[i] = c; }

void panelPixel(int x, int y, uint16_t c) {
  if (x < 0 || y < 0 || x >= PANEL_W || y >= PANEL_H) return;
  fb[y * PANEL_W + x] = c;
}

void panelHSpan(int x0, int x1, int y, uint16_t c) {
  if (y < 0 || y >= PANEL_H) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= PANEL_W) x1 = PANEL_W - 1;
  for (int x = x0; x <= x1; x++) fb[y * PANEL_W + x] = c;
}

bool panelIsColor() { return isColor; }
uint16_t panelInk()     { return C_BLACK; }
uint16_t panelPaper()   { return C_WHITE; }
uint16_t panelCaption() { return C_BLACK; }
uint16_t panelAccent(int role) {
  switch (role) {
    case 2:  return C_YELLOW;
    case 3:  return C_BLUE;
    case 4:  return C_GREEN;
    default: return C_RED;
  }
}
