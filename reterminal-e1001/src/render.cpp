#include "render.h"
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <cstring>
#include "pins.h"
#include "paint.h"
#include "text.h"
#include "icons.h"

namespace {

SPIClass hspi(HSPI);
GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>
    display(GxEPD2_750_GDEY075T7(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// ---- geometry: the 1200x825 mock re-laid at 800x480, same proportions ------
constexpr int W = 800, H = 480;
constexpr int LEFT_W = 282;                    // 35% like the mock
constexpr int COL_X0 = 312, COL_X1 = 778;            // right column content bounds
constexpr int COL_W = COL_X1 - COL_X0;

// ---- type ramp --------------------------------------------------------------
const TextStyle T_EYEBROW  { Face::Body,      15, 3.4f, Role::Paper };
const TextStyle T_HEADLINE { Face::Display,   52, 0.5f, Role::Paper };
const TextStyle T_TAGLINE  { Face::Body,      15, 3.4f, Role::Paper };
const TextStyle T_WEEKDAY  { Face::Display,   40, 0,    Role::Ink };
const TextStyle T_CORNER   { Face::Body,      14, 0,    Role::Ink };
const TextStyle T_SUBLINE  { Face::BodyLight, 15, 0,    Role::Ink };
const TextStyle T_SECTION  { Face::Body,      11, 3.0f, Role::Caption };
const TextStyle T_TILE     { Face::Body,      14, 0,    Role::Ink };
const TextStyle T_ALSO     { Face::Body,      14, 0,    Role::Ink };
const TextStyle T_STATLBL  { Face::Body,      12, 2.0f, Role::Caption };
const TextStyle T_STATVAL  { Face::Display,   24, 0,    Role::Ink };
const TextStyle T_STATWORD { Face::BodyLight, 15, 0,    Role::Caption };
const TextStyle T_FOOTER   { Face::Body,      17, 0,    Role::Paper };
const TextStyle T_STATUS   { Face::BodyLight, 10, 0,    Role::Caption };
const TextStyle T_MSG_H    { Face::Display,   34, 0,    Role::Ink };
const TextStyle T_MSG_B    { Face::BodyLight, 16, 0,    Role::Ink };

void drawHero(const ViewModel& vm) {
  const day::Outcome& oc = vm.screen.outcome;
  paintRect(0, 0, LEFT_W, H, Role::Ink);
  const int cx = LEFT_W / 2;

  textDrawCentered(T_EYEBROW, cx, 44, vm.screen.eyebrow);

  // Headline block sits at the visual centre; three-line titles shift up.
  const int lineH = 46;
  int lines = oc.titleLines;
  int lastBaseline = (lines >= 3) ? 340 : 318;
  int firstBaseline = lastBaseline - (lines - 1) * lineH;
  int iconCy = firstBaseline - 46 - 56;         // 100px icon above the block
  IconStyle heroStyle; heroStyle.ink = Role::Paper; heroStyle.paper = Role::Ink; heroStyle.accents = false;
  drawIcon(oc.heroIcon, cx, iconCy, 100, heroStyle);

  for (int i = 0; i < lines; i++) {
    // Fit check: shrink a long word rather than clip it.
    TextStyle st = T_HEADLINE;
    while (st.px > 30 && textWidth(st, oc.title[i]) > LEFT_W - 28) st.px -= 2;
    textDrawCentered(st, cx, firstBaseline + i * lineH, oc.title[i]);
  }
  textDrawCentered(T_TAGLINE, cx, 434, oc.tagline);
}

void drawRight(const ViewModel& vm) {
  const day::Screen& s = vm.screen;
  char lines[2][160];

  // Weekday + corner stats
  textDraw(T_WEEKDAY, COL_X0, 58, s.weekday);
  textDrawRight(T_CORNER, COL_X1, 42, s.corner1);
  textDrawRight(T_CORNER, COL_X1, 62, s.corner2);

  // Subline (wraps to two lines if it must)
  int n = textWrap(T_SUBLINE, s.subline, COL_W, lines, 2);
  int y = 88;
  for (int i = 0; i < n; i++) { textDraw(T_SUBLINE, COL_X0, y, lines[i]); y += 19; }
  int ruleY = y + 2;
  paintHLine(COL_X0, COL_X1, ruleY, 3, Role::Ink);

  // Wear tiles. No heading: the four pictures say what they are, and the row
  // it used to occupy is worth more to the stat strip.
  const int tileY = ruleY + 20, tileH = 92, gap = 12;
  const int tileW = (COL_W - 3 * gap) / 4;
  const bool tileAccents = paintAccentsLegibleAt(44);
  for (int i = 0; i < 4; i++) {
    int x = COL_X0 + i * (tileW + gap);
    paintRoundRectStroke(x, tileY, tileW, tileH, 12, 2, Role::Ink);
    if (i < s.outcome.wearCount) {
      const day::Wear& w = s.outcome.wear[i];
      IconStyle ist; ist.accents = tileAccents;
      drawIcon(w.icon, x + tileW / 2, tileY + 36, 44, ist);
      TextStyle st = T_TILE;
      while (st.px > 10 && textWidth(st, w.label) > tileW - 10) st.px -= 1;
      textDrawCentered(st, x + tileW / 2, tileY + 80, w.label);
    }
  }

  // Also grab (up to two lines)
  int alsoY = tileY + tileH + 26;
  n = textWrap(T_ALSO, s.outcome.alsoGrab, COL_W, lines, 2);
  for (int i = 0; i < n; i++) { textDraw(T_ALSO, COL_X0, alsoY, lines[i]); alsoY += 19; }
  const int alsoBottom = alsoY - 19 + 5;

  // Footer pill, anchored to the bottom
  const int pillH = 38, pillY = H - 30 - pillH;

  // Stat strip fills whatever is left between them
  const int bandY = alsoBottom + 14;
  const int bandH = (pillY - 14) - bandY;
  if (bandH >= 56) {
    paintBandPanel(COL_X0, bandY, COL_W, bandH, 10);
    int cols = s.statCount > 5 ? 5 : s.statCount;
    if (cols > 0) {
      // Space the four rows off measured cap heights rather than guessed
      // offsets, so changing a size can't silently crowd its neighbour.
      const int iconPx = 26;
      const int GAP_ICON = 11, GAP_LABEL = 13, GAP_VALUE = 9;
      const int capLabel = textCapHeight(T_STATLBL);
      const int capValue = textCapHeight(T_STATVAL);
      const int capWord  = textCapHeight(T_STATWORD);
      const int contentH = iconPx + GAP_ICON + capLabel + GAP_LABEL + capValue + GAP_VALUE + capWord;

      int top = bandY + (bandH - contentH) / 2;
      if (top < bandY + 6) top = bandY + 6;

      const bool statAccents = paintAccentsLegibleAt(iconPx);
      const float colW = (float)COL_W / cols;
      for (int i = 0; i < cols; i++) {
        const day::Stat& st = s.stats[i];
        const int cx = COL_X0 + (int)(colW * i + colW / 2);
        const int avail = (int)colW - 10;
        int y = top;

        IconStyle ist; ist.accents = statAccents;
        drawIcon(st.icon, cx, y + iconPx / 2, iconPx, ist);
        y += iconPx + GAP_ICON;

        TextStyle lbl = T_STATLBL;
        while (lbl.tracking > 0.4f && textWidth(lbl, st.label) > avail) lbl.tracking -= 0.4f;
        while (lbl.px > 8 && textWidth(lbl, st.label) > avail) lbl.px -= 1;
        y += capLabel;
        textDrawCentered(lbl, cx, y, st.label);
        y += GAP_LABEL;

        TextStyle val = T_STATVAL;
        while (val.px > 14 && textWidth(val, st.value) > avail) val.px -= 1;
        y += capValue;
        textDrawCentered(val, cx, y, st.value);
        y += GAP_VALUE;

        TextStyle wrd = T_STATWORD;
        while (wrd.px > 10 && textWidth(wrd, st.word) > avail) wrd.px -= 1;
        y += capWord;
        textDrawCentered(wrd, cx, y, st.word);
      }
    }
  }

  paintRoundRect(COL_X0, pillY, COL_W, pillH, 9, Role::Ink);
  const char* footer = vm.parkingActive ? vm.parkingText : s.footer;
  TextStyle ft = T_FOOTER;
  while (ft.px > 12 && textWidth(ft, footer) > COL_W - 24) ft.px -= 1;
  textDrawCentered(ft, COL_X0 + COL_W / 2, pillY + 25, footer);

  if (vm.statusText[0]) textDrawRight(T_STATUS, COL_X1, H - 10, vm.statusText);
}

void frame() {
  paintRoundRectStroke(12, 12, W - 24, H - 24, 8, 4, Role::Ink);
}

} // namespace

void renderBegin() {
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  hspi.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
  display.epd2.selectSPI(hspi, SPISettings(2000000, MSBFIRST, SPI_MODE0));
  display.init(0, true, 10, false);
  display.setRotation(0);
  display.setTextWrap(false);
  paintBegin(&display);
  textInit();
}

void renderScreen(const ViewModel& vm) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawHero(vm);
    drawRight(vm);
  } while (display.nextPage());
}

void renderMessage(const char* title, const char* line1, const char* line2) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    frame();
    textDrawCentered(T_MSG_H, W / 2, H / 2 - 36, title);
    char lines[2][160];
    int y = H / 2 + 8;
    if (line1) { int n = textWrap(T_MSG_B, line1, W - 120, lines, 2); for (int i = 0; i < n; i++) { textDrawCentered(T_MSG_B, W / 2, y, lines[i]); y += 22; } }
    y += 8;
    if (line2) { int n = textWrap(T_MSG_B, line2, W - 120, lines, 2); for (int i = 0; i < n; i++) { textDrawCentered(T_MSG_B, W / 2, y, lines[i]); y += 22; } }
  } while (display.nextPage());
}

void renderSetup(const char* apName, const char* ip) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    frame();
    const int x0 = 60;
    int y = 84;
    textDraw(TextStyle{ Face::Display, 34, 0, Role::Ink }, x0, y, "Beach Day setup");
    y += 26;
    textDraw(T_MSG_B, x0, y, "Three steps on your phone. Takes about a minute.");

    const TextStyle step { Face::Body, 17, 0, Role::Ink };
    const TextStyle num  { Face::Display, 18, 0, Role::Paper };
    y += 52;
    // 1
    paintDisc(x0 + 16, y - 6, 17, Role::Ink); textDrawCentered(num, x0 + 16, y, "1");
    textDraw(step, x0 + 48, y, "Join the Wi-Fi network");
    const TextStyle ap { Face::Display, 30, 0.5f, Role::Paper };
    int apW = textWidth(ap, apName) + 30;
    paintRoundRect(x0 + 48, y + 14, apW, 46, 8, Role::Ink);
    textDraw(ap, x0 + 63, y + 47, apName);
    y += 96;
    // 2
    paintDisc(x0 + 16, y - 6, 17, Role::Ink); textDrawCentered(num, x0 + 16, y, "2");
    textDraw(step, x0 + 48, y, "A setup page opens by itself.");
    textDraw(T_MSG_B, x0 + 48, y + 26, "If it doesn't, open a browser and go to:");
    char url[48]; snprintf(url, sizeof(url), "http://%s", ip);
    textDraw(TextStyle{ Face::Body, 20, 0, Role::Ink }, x0 + 48, y + 56, url);
    y += 98;
    // 3
    paintDisc(x0 + 16, y - 6, 17, Role::Ink); textDrawCentered(num, x0 + 16, y, "3");
    textDraw(step, x0 + 48, y, "Enter your Wi-Fi and your beach town, then tap Save.");
    textDraw(T_MSG_B, x0 + 48, y + 26, "The forecast appears about a minute later.");

    textDrawCentered(T_STATUS, W / 2, H - 42, "This screen turns off after 10 minutes. Hold the middle button");
    textDrawCentered(T_STATUS, W / 2, H - 28, "and press the right one to open setup again.");
  } while (display.nextPage());
}

void renderEnd() { display.hibernate(); }
