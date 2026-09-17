#include "render.h"
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <cmath>
#include <cstring>
#include "pins.h"
#include "beachrules.h"

namespace {

SPIClass hspi(HSPI);
// Full-height page buffer (800*480/8 = 48 KB): one pass per refresh.
GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>
    display(GxEPD2_750_GDEY075T7(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

constexpr uint16_t BLACK = GxEPD_BLACK;
constexpr uint16_t WHITE = GxEPD_WHITE;

// ---- geometry (1cqh = 4.8 px, 1cqw = 8 px on this panel) -------------------
constexpr int W = 800, H = 480;
constexpr int MARGIN = 12, GAP = 14, BORDER = 4, RADIUS = 7;
constexpr int HERO_X = MARGIN, HERO_Y = MARGIN, HERO_W = 317, HERO_H = H - 2 * MARGIN;
constexpr int DET_X = HERO_X + HERO_W + GAP, DET_Y = MARGIN;
constexpr int DET_W = W - DET_X - MARGIN, DET_H = HERO_H;
constexpr int HERO_PAD_X = 32, DET_PAD_X = 40, PAD_Y = 29;
constexpr int ICON = 134;                  // 28cqh
constexpr int TITLE_LINE = 58;             // 11cqh * 1.1

const GFXfont* const F_TITLE = &FreeSansBold24pt7b;  // cap ~34 px
const GFXfont* const F_BIG   = &FreeSansBold18pt7b;  // cap ~25 px
const GFXfont* const F_SUB   = &FreeSansBold12pt7b;  // cap ~17 px
const GFXfont* const F_BOLD9 = &FreeSansBold9pt7b;   // cap ~13 px
const GFXfont* const F_REG9  = &FreeSans9pt7b;

int capHeight(const GFXfont* f) {
  if (f == F_TITLE) return 34;
  if (f == F_BIG)   return 25;
  if (f == F_SUB)   return 17;
  return 13;
}

// ---- text ------------------------------------------------------------------
// '~' in a string draws a degree sign (the bundled fonts stop at ASCII 0x7E).
constexpr int DEGREE_ADVANCE = 9;

int glyphAdvance(const GFXfont* f, char c) {
  if ((uint8_t)c < f->first || (uint8_t)c > f->last) return 0;
  return pgm_read_byte(&f->glyph[(uint8_t)c - f->first].xAdvance);
}

int textWidth(const char* s, const GFXfont* f, int spacing = 0) {
  int w = 0, n = 0;
  for (; *s; s++, n++) w += (*s == '~') ? DEGREE_ADVANCE : glyphAdvance(f, *s);
  if (n > 1) w += spacing * (n - 1);
  return w;
}

int drawText(int x, int y, const char* s, const GFXfont* f, uint16_t color, int spacing = 0) {
  display.setFont(f);
  display.setTextColor(color);
  display.setCursor(x, y);
  for (; *s; s++) {
    if (*s == '~') {
      int cx = display.getCursorX() + 3;
      int cy = y - capHeight(f) + 3;
      display.drawCircle(cx, cy, 3, color);
      display.drawCircle(cx, cy, 2, color);
      display.setCursor(display.getCursorX() + DEGREE_ADVANCE, y);
    } else {
      display.write(*s);
    }
    if (spacing) display.setCursor(display.getCursorX() + spacing, y);
  }
  return display.getCursorX();
}

void drawTextCentered(int cx, int y, const char* s, const GFXfont* f, uint16_t c, int sp = 0) {
  drawText(cx - textWidth(s, f, sp) / 2, y, s, f, c, sp);
}
void drawTextRight(int xr, int y, const char* s, const GFXfont* f, uint16_t c, int sp = 0) {
  drawText(xr - textWidth(s, f, sp), y, s, f, c, sp);
}

void upperCopy(char* dst, size_t n, const char* src) {
  size_t i = 0;
  for (; i + 1 < n && src[i]; i++) dst[i] = (char)toupper((unsigned char)src[i]);
  dst[i] = '\0';
}

// Truncate `value` with ".." so label+value fits in maxW.
void drawLabelValue(int x, int y, const char* label, const char* value, int maxW) {
  int lx = drawText(x, y, label, F_BOLD9, BLACK);
  lx += 5;
  int avail = maxW - (lx - x);
  char buf[40];
  strncpy(buf, value, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  if (textWidth(buf, F_REG9) > avail) {
    size_t len = strlen(buf);
    while (len > 3 && textWidth(buf, F_REG9) > avail) {
      buf[len - 1] = '\0';
      len--;
      buf[len - 1] = '.'; buf[len - 2] = '.';
    }
  }
  drawText(lx, y, buf, F_REG9, BLACK);
}

// ---- shapes ----------------------------------------------------------------
void thickLine(int x0, int y0, int x1, int y1, int w, uint16_t c) {
  int r = w / 2;
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    display.fillCircle(x0, y0, r, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void ring(int cx, int cy, int r, int w, uint16_t fg, uint16_t bg) {
  display.fillCircle(cx, cy, r, fg);
  display.fillCircle(cx, cy, r - w, bg);
}

void dashedHLine(int x0, int x1, int y, uint16_t c) {
  for (int x = x0; x < x1; x += 10) display.drawFastHLine(x, y, min(6, x1 - x), c);
}

void cloudSilhouette(int cx, int cy, float k, int inset, uint16_t c) {
  display.fillCircle(cx - 22 * k, cy + 8 * k,  24 * k - inset, c);
  display.fillCircle(cx + 4 * k,  cy - 8 * k,  30 * k - inset, c);
  display.fillCircle(cx + 30 * k, cy + 10 * k, 22 * k - inset, c);
  int rr = max(1, (int)(8 * k) - inset);
  display.fillRoundRect(cx - 40 * k + inset, cy + 8 * k, 88 * k - 2 * inset, 26 * k - inset, rr, c);
}

void cloud(int cx, int cy, float k, uint16_t fg, uint16_t bg) {
  int stroke = max(3, (int)(8 * k));
  cloudSilhouette(cx, cy, k, 0, fg);
  cloudSilhouette(cx, cy, k, stroke, bg);
}

void sun(int cx, int cy, float k, int stroke, uint16_t fg, uint16_t bg) {
  ring(cx, cy, 24 * k, stroke, fg, bg);
  for (int i = 0; i < 8; i++) {
    float a = i * (float)M_PI / 4.0f;
    thickLine(cx + cosf(a) * 36 * k, cy + sinf(a) * 36 * k,
              cx + cosf(a) * 52 * k, cy + sinf(a) * 52 * k, stroke, fg);
  }
}

void wave(int x0, int x1, int y, int amp, uint16_t c) {
  for (int x = x0; x <= x1; x++) {
    int yy = y + (int)lroundf(amp * sinf((x - x0) * 2.0f * (float)M_PI / 56.0f));
    display.fillCircle(x, yy, 3, c);
  }
}

void heroIcon(beach::State st, bool clearingLater, int cx, int cy, uint16_t fg, uint16_t bg) {
  using beach::State;
  switch (st) {
    case State::BeachDay:
      sun(cx, cy - 16, 0.8f, 7, fg, bg);
      wave(cx - 56, cx + 56, cy + 40, 5, fg);
      wave(cx - 56, cx + 56, cy + 58, 5, fg);
      break;
    case State::NightTime:
      display.fillCircle(cx, cy, 46, fg);
      display.fillCircle(cx + 22, cy - 18, 42, bg);
      break;
    case State::IndoorDay:
      thickLine(cx - 52, cy - 2,  cx,      cy - 48, 8, fg);
      thickLine(cx,      cy - 48, cx + 52, cy - 2,  8, fg);
      thickLine(cx - 42, cy - 10, cx - 42, cy + 48, 8, fg);
      thickLine(cx + 42, cy - 10, cx + 42, cy + 48, 8, fg);
      thickLine(cx - 42, cy + 48, cx + 42, cy + 48, 8, fg);
      thickLine(cx - 12, cy + 48, cx - 12, cy + 12, 6, fg);
      thickLine(cx - 12, cy + 12, cx + 12, cy + 12, 6, fg);
      thickLine(cx + 12, cy + 12, cx + 12, cy + 48, 6, fg);
      break;
    case State::RainDay:
      cloud(cx, cy - 16, 0.85f, fg, bg);
      for (int dx : { -24, 0, 24 }) thickLine(cx + dx, cy + 30, cx + dx, cy + 54, 7, fg);
      break;
    case State::WindDay:
      thickLine(cx - 54, cy - 26, cx + 18, cy - 26, 7, fg);
      ring(cx + 18, cy - 38, 12, 7, fg, bg);
      thickLine(cx - 54, cy + 2,  cx + 38, cy + 2,  7, fg);
      ring(cx + 38, cy + 14, 12, 7, fg, bg);
      thickLine(cx - 54, cy + 30, cx + 6,  cy + 30, 7, fg);
      ring(cx + 6, cy + 42, 12, 7, fg, bg);
      break;
    case State::NiceDay:
      if (clearingLater) {
        sun(cx + 26, cy - 26, 0.55f, 6, fg, bg);
        cloud(cx - 8, cy + 14, 0.8f, fg, bg);
      } else {
        sun(cx, cy, 1.0f, 8, fg, bg);
      }
      break;
    case State::GreyDay:
      cloud(cx + 18, cy - 20, 0.65f, fg, bg);
      cloud(cx - 6,  cy + 12, 0.9f, fg, bg);
      break;
    case State::ChillyDay:
    case State::JustADay:
    default:
      cloud(cx, cy, 1.0f, fg, bg);
      break;
  }
}

// 24x24 pass / fail glyphs
void statusIcon(int x, int y, bool pass) {
  if (pass) {
    thickLine(x + 3, y + 13, x + 9,  y + 19, 4, BLACK);
    thickLine(x + 9, y + 19, x + 21, y + 5,  4, BLACK);
  } else {
    // umbrella canopy (arc) + pole, struck through
    display.fillCircle(x + 12, y + 13, 9, BLACK);
    display.fillCircle(x + 12, y + 13, 6, WHITE);
    display.fillRect(x + 2, y + 13, 21, 12, WHITE);
    thickLine(x + 12, y + 13, x + 12, y + 22, 2, BLACK);
    thickLine(x + 20, y + 4, x + 4, y + 20, 3, BLACK);
  }
}

void batteryGlyph(int x, int y, int pct) {
  display.drawRect(x, y, 20, 11, BLACK);
  display.fillRect(x + 20, y + 3, 2, 5, BLACK);
  int fill = (16 * pct + 50) / 100;
  if (fill > 0) display.fillRect(x + 2, y + 2, fill, 7, BLACK);
}

// ---- panels ----------------------------------------------------------------
void drawHero(const ViewModel& vm) {
  auto st = static_cast<beach::State>(vm.state);
  bool yes = (st == beach::State::BeachDay);
  uint16_t fg = yes ? WHITE : BLACK;
  uint16_t bg = yes ? BLACK : WHITE;

  display.fillRoundRect(HERO_X, HERO_Y, HERO_W, HERO_H, RADIUS, BLACK);
  if (!yes) {
    display.fillRoundRect(HERO_X + BORDER, HERO_Y + BORDER, HERO_W - 2 * BORDER,
                          HERO_H - 2 * BORDER, RADIUS - 2, WHITE);
  }

  int cx = HERO_X + HERO_W / 2;
  bool hasLoc = vm.locationName[0] != '\0';
  int total = (hasLoc ? 27 : 0) + ICON + 19 + (34 + TITLE_LINE) + 10 + 13;
  int y = HERO_Y + (HERO_H - total) / 2;

  if (hasLoc) {
    char loc[24];
    upperCopy(loc, sizeof(loc), vm.locationName);
    drawTextCentered(cx, y + 13, loc, F_BOLD9, fg, 2);
    y += 27;
  }
  heroIcon(st, vm.clearingLater, cx, y + ICON / 2, fg, bg);
  y += ICON + 19;

  char l1[16], l2[16];
  upperCopy(l1, sizeof(l1), beach::stateTitleLine1(st));
  upperCopy(l2, sizeof(l2), beach::stateTitleLine2(st));
  drawTextCentered(cx, y + 34, l1, F_TITLE, fg, 3);
  drawTextCentered(cx, y + 34 + TITLE_LINE, l2, F_TITLE, fg, 3);
  y += 34 + TITLE_LINE + 10;

  char sub[32];
  upperCopy(sub, sizeof(sub), beach::stateSubtitle(st));
  drawTextCentered(cx, y + 13, sub, F_BOLD9, fg, 1);
}

void drawDetails(const ViewModel& vm) {
  display.fillRoundRect(DET_X, DET_Y, DET_W, DET_H, RADIUS, BLACK);
  display.fillRoundRect(DET_X + BORDER, DET_Y + BORDER, DET_W - 2 * BORDER,
                        DET_H - 2 * BORDER, RADIUS - 2, WHITE);

  const int x0 = DET_X + DET_PAD_X;
  const int x1 = DET_X + DET_W - DET_PAD_X;
  const int iw = x1 - x0;
  int y = DET_Y + PAD_Y;

  // Header: weekday left, battery + freshness right
  if (vm.isNight) drawText(x0, y + 17, vm.dayLabel, F_BOLD9, BLACK, 1);
  else            drawText(x0, y + 17, vm.dayLabel, F_SUB, BLACK, 1);
  int rx = x1;
  if (vm.batteryPct >= 0) {
    batteryGlyph(x1 - 22, y + 5, vm.batteryPct);
    rx = x1 - 22 - 8;
  }
  drawTextRight(rx, y + 17, vm.updatedText, F_REG9, BLACK);
  y += 21 + 6;

  // Two columns of detail lines
  const int colW = 186;
  const int colR = x0 + colW + 8;
  char buf[40];
  drawLabelValue(x0, y + 13, vm.isNight ? "Tomorrow:" : "Today:", vm.conditionText, colW);
  snprintf(buf, sizeof(buf), "%d to %d~F", vm.tempMin, vm.tempMax);
  drawLabelValue(x0, y + 13 + 17, "Temps:", buf, colW);
  snprintf(buf, sizeof(buf), "%d to %d%%", vm.humMin, vm.humMax);
  drawLabelValue(colR, y + 13, "Humidity:", buf, x1 - colR);
  snprintf(buf, sizeof(buf), "%d to %d", vm.aqiMin, vm.aqiMax);
  drawLabelValue(colR, y + 13 + 17, "Air quality:", buf, x1 - colR);
  y += 13 + 17 + 4 + 7;
  display.fillRect(x0, y, iw, BORDER, BLACK);
  y += BORDER;

  // Footer geometry first so the table can flex into what is left
  const int innerBottom   = DET_Y + DET_H - PAD_Y;
  const int footerH       = vm.parkingActive ? 26 : 13;
  const int footerTop     = innerBottom - footerH;
  const int footerBorderY = footerTop - 7 - BORDER;
  const int tableBottom   = footerBorderY - 14;
  const int tableTop      = y + 4;
  const int rowH          = (tableBottom - tableTop) / 5;

  char thTemp[24], thRain[24], thWind[24], thAqi[24];
  snprintf(thTemp, sizeof(thTemp), "high above %d~F?", beach::TEMP_BEACH_MIN);
  snprintf(thRain, sizeof(thRain), "below %d%%?", (int)beach::PRECIP_BEACH_MAX);
  snprintf(thWind, sizeof(thWind), "max %d mph?", (int)beach::WIND_BEACH_MAX);
  snprintf(thAqi,  sizeof(thAqi),  "max below %d?", beach::AQI_BEACH_MAX);
  char vTemp[12], vRain[12], vWind[16], vAqi[12];
  snprintf(vTemp, sizeof(vTemp), "%d~F", vm.temp);
  snprintf(vRain, sizeof(vRain), "%d%%", vm.precip);
  snprintf(vWind, sizeof(vWind), "%d mph", vm.wind);
  snprintf(vAqi,  sizeof(vAqi),  "%d", vm.aqi);

  struct Row { const char* name; const char* threshold; const char* value; bool pass; };
  const Row rows[5] = {
    { "Sun",         "coming out?", vm.sunText, vm.condSun },
    { "Temperature", thTemp,        vTemp,      vm.condTemp },
    { "Rain Chance", thRain,        vRain,      vm.condPrecip },
    { "Wind",        thWind,        vWind,      vm.condWind },
    { "Air Quality", thAqi,         vAqi,       vm.condAqi },
  };
  for (int i = 0; i < 5; i++) {
    int top = tableTop + i * rowH;
    int vc  = top + rowH / 2;
    drawText(x0 + 8, vc - 3,  rows[i].name,      F_BOLD9, BLACK);
    drawText(x0 + 8, vc + 13, rows[i].threshold, F_REG9,  BLACK);
    int iconX = x1 - 8 - 24;
    statusIcon(iconX, vc - 12, rows[i].pass);
    drawTextRight(iconX - 16, vc + 7, rows[i].value, F_SUB, BLACK);
    if (i < 4) dashedHLine(x0, x1, top + rowH - 1, BLACK);
  }

  // Footer: parking alert replaces the sun line
  display.fillRect(x0, footerBorderY, iw, BORDER, BLACK);
  if (vm.parkingActive) {
    display.fillRoundRect(x0, footerTop, iw, 26, 4, BLACK);
    char t[40];
    upperCopy(t, sizeof(t), vm.parkingText);
    drawTextCentered((x0 + x1) / 2, footerTop + 18, t, F_BOLD9, WHITE, 1);
  } else {
    drawTextCentered((x0 + x1) / 2, footerTop + 13, vm.footer, F_BOLD9, BLACK);
  }
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
}

void renderView(const ViewModel& vm) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(WHITE);
    drawHero(vm);
    drawDetails(vm);
  } while (display.nextPage());
}

void renderMessage(const char* title, const char* line1, const char* line2) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(WHITE);
    display.fillRoundRect(MARGIN, MARGIN, W - 2 * MARGIN, H - 2 * MARGIN, RADIUS, BLACK);
    display.fillRoundRect(MARGIN + BORDER, MARGIN + BORDER, W - 2 * MARGIN - 2 * BORDER,
                          H - 2 * MARGIN - 2 * BORDER, RADIUS - 2, WHITE);
    drawTextCentered(W / 2, H / 2 - 40, title, F_BIG, BLACK, 1);
    if (line1) drawTextCentered(W / 2, H / 2 + 10, line1, F_SUB, BLACK);
    if (line2) drawTextCentered(W / 2, H / 2 + 44, line2, F_REG9, BLACK);
  } while (display.nextPage());
}

void renderEnd() {
  display.hibernate();
}
