#include "render.h"
#include <Arduino.h>
#include <cstring>
#include "panel.h"
#include "paint.h"
#include "text.h"
#include "icons.h"
#include "wmo.h"
#include <cmath>

namespace {

// ---- geometry: 800x480 e-ink layout -----------------------------------------
constexpr int W = PANEL_W, H = PANEL_H;
constexpr int LEFT_W = 272;
constexpr int COL_X0 = LEFT_W + 26, COL_X1 = W - 26;   // 298..774 (476px)
constexpr int COL_W = COL_X1 - COL_X0;

// ---- type ramp --------------------------------------------------------------
const TextStyle T_EYEBROW       { Face::Body,      13, 3.4f, Role::Paper };
const TextStyle T_HEADLINE      { Face::Display,   66, -0.5f, Role::Paper };
const TextStyle T_TAGLINE       { Face::Body,      13, 3.4f, Role::Paper };
const TextStyle T_WEEKDAY       { Face::Display,   44, 0,    Role::Ink };
const TextStyle T_CORNER        { Face::Body,      13, 0,    Role::Ink };

// Battery pictogram for the corner: outline, nub, and a proportional fill.
constexpr int BATT_W = 22, BATT_H = 12;
void batteryGlyph(int x, int y, int pct) {
  paintRoundRectStroke(x, y, BATT_W - 3, BATT_H, 2, 2, Role::Ink);
  paintRect(x + BATT_W - 3, y + 4, 3, BATT_H - 8, Role::Ink);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  const int inner = BATT_W - 3 - 6;
  const int fill = (inner * pct + 50) / 100;
  if (fill > 0) paintRect(x + 3, y + 3, fill, BATT_H - 6, Role::Ink);
}
const TextStyle T_SECTION       { Face::Body,      12, 1.7f, Role::Ink };
const TextStyle T_TILE          { Face::Body,      15, 0,    Role::Ink };
const TextStyle T_ALSO_BOLD     { Face::Body,      14, 0,    Role::Ink };
const TextStyle T_ALSO          { Face::BodyLight, 14, 0,    Role::Ink };
const TextStyle T_STATLBL       { Face::Body,      12, 1.7f, Role::Ink };
const TextStyle T_STATVAL       { Face::Body,      31, 0,    Role::Ink };
const TextStyle T_STATUNIT_PCT  { Face::Body,      18, 0,    Role::Ink };
const TextStyle T_STATUNIT_WORD { Face::Body,      16, 0,    Role::Ink };
const TextStyle T_STATSUB       { Face::BodyLight, 14, 0,    Role::Ink };
const TextStyle T_FOOTER        { Face::Body,      14, 0,    Role::Ink };
const TextStyle T_STATUS        { Face::BodyLight, 10, 0,    Role::Caption };
const TextStyle T_MSG_H         { Face::Display,   34, 0,    Role::Ink };
const TextStyle T_MSG_B         { Face::BodyLight, 16, 0,    Role::Ink };

Tint tintFor(const char* name) {
  if (!name) return Tint::None;
  if (!strcmp(name, "yellow")) return Tint::Yellow;
  if (!strcmp(name, "blue"))   return Tint::Blue;
  if (!strcmp(name, "red"))    return Tint::Red;
  if (!strcmp(name, "green"))  return Tint::Green;
  return Tint::None;
}

// Titles are free text now - "Puddle Day", or "Thanksgiving Rain Boots
// Day" on a holiday - so the size comes from the words rather than the words
// being pre-split to fit a size. Largest size that wraps into the panel wins.
constexpr int TITLE_MAX_LINES = 4;

int fitTitle(const char* title, int maxH, char lines[TITLE_MAX_LINES][160], int& lineCount, int& leading) {
  const int avail = LEFT_W - 44;
  for (int px = 66; px >= 24; px -= 2) {
    TextStyle st = T_HEADLINE; st.px = (float)px;
    int n = textWrap(st, title, avail, lines, TITLE_MAX_LINES);
    // Keep "... Day" on its own line when a short two-word name would otherwise stay on one line.
    if (n == 1) {
      const char* sp = strrchr(title, ' ');
      if (sp && sp > title && !strcmp(sp, " Day")) {
        size_t firstLen = (size_t)(sp - title);
        if (firstLen < 159) {
          memcpy(lines[0], title, firstLen); lines[0][firstLen] = '\0';
          snprintf(lines[1], 160, "%s", sp + 1);
          n = 2;
        }
      }
    }
    bool fits = (n > 0);
    for (int i = 0; i < n; i++) if (textWidth(st, lines[i]) > avail) fits = false;
    int lead = (px * 90) / 100;
    int blockH = (n - 1) * lead + textCapHeight(st);
    if (fits && blockH <= maxH) { lineCount = n; leading = lead; return px; }
  }
  TextStyle st = T_HEADLINE; st.px = 24;
  lineCount = textWrap(st, title, avail, lines, TITLE_MAX_LINES);
  leading = 22;
  return 24;
}

void drawHero(const ViewModel& vm) {
  const day::Outcome& oc = vm.screen.outcome;
  const int cx = LEFT_W / 2;

  const HeroSurface hero = paintHeroSurface(tintFor(oc.heroColor), oc.heroLight);

  paintRect(0, 0, LEFT_W, H, hero.bg);
  // A white hero needs a rule to stay a distinct zone; a filled one does not.
  if (hero.bg == Role::Paper) paintRect(LEFT_W - 3, 0, 3, H, Role::Ink);

  TextStyle eyebrow = T_EYEBROW; eyebrow.role = hero.fg;
  TextStyle tagline = T_TAGLINE; tagline.role = hero.fg;
  const int eyeCap = textCapHeight(eyebrow);
  const int tagCap = textCapHeight(tagline);
  const int eyeBase = 30 + eyeCap;
  const int tagBase = H - 28;
  textDrawCentered(eyebrow, cx, eyeBase, vm.screen.eyebrow);
  textDrawCentered(tagline, cx, tagBase, oc.tagline);

  const int iconPx = 124;
  const int iconTitleGap = 18;
  const int zoneTop = eyeBase + 16;
  const int zoneBottom = tagBase - tagCap - 16;
  const int maxTitleH = (zoneBottom - zoneTop) - iconPx - iconTitleGap;

  char lines[TITLE_MAX_LINES][160];
  int n = 0, leading = 0;
  int px = fitTitle(oc.title, maxTitleH, lines, n, leading);
  TextStyle st = T_HEADLINE; st.px = (float)px; st.role = hero.fg;
  const int cap = textCapHeight(st);
  const int titleBlockH = (n > 0 ? (n - 1) * leading + cap : 0);
  const int groupH = iconPx + iconTitleGap + titleBlockH;
  const int groupTop = zoneTop + (zoneBottom - zoneTop - groupH) / 2;

  IconStyle heroStyle;
  heroStyle.ink = hero.fg;
  heroStyle.paper = hero.bg;
  heroStyle.accents = hero.accents && (panelIsColor() || paintAccentsLegibleAt(iconPx));
  drawIcon(oc.heroIcon, cx, groupTop + iconPx / 2, iconPx, heroStyle);

  int y = groupTop + iconPx + iconTitleGap + cap;
  for (int i = 0; i < n; i++) {
    textDrawCentered(st, cx, y, lines[i]);
    y += leading;
  }
}

// ---- 3x2 conditions grid ----------------------------------------------------
void drawStatValue(int x, int baseline, int avail, const char* valStr) {
  const size_t len = strlen(valStr);
  const char* unit = nullptr;
  TextStyle unitStyle = T_STATUNIT_WORD;
  int unitGap = 4;
  size_t numLen = len;

  if (len > 4 && !strcmp(valStr + len - 4, " mph")) {
    unit = "mph"; numLen = len - 4;
  } else if (len > 4 && !strcmp(valStr + len - 4, " aqi")) {
    unit = "aqi"; numLen = len - 4;
  } else if (len > 1 && valStr[len - 1] == '%') {
    unit = "%"; numLen = len - 1;
    unitStyle = T_STATUNIT_PCT;
    unitGap = 2;
  }

  char numBuf[24];
  if (numLen >= sizeof(numBuf)) numLen = sizeof(numBuf) - 1;
  memcpy(numBuf, valStr, numLen);
  numBuf[numLen] = '\0';

  TextStyle val = T_STATVAL;
  const int unitW = unit ? (unitGap + textWidth(unitStyle, unit)) : 0;
  while (val.px > 18 && textWidth(val, numBuf) + unitW > avail) val.px -= 1;

  int penX = textDraw(val, x, baseline, numBuf);
  if (unit) textDraw(unitStyle, penX + unitGap, baseline, unit);
}

void drawConditionsGrid(const day::Screen& s, int gridY0, int rowH) {
  constexpr int COLS = 3, ROWS = 2;
  const int colW = COL_W / COLS;
  const int x1 = COL_X0 + colW;
  const int x2 = COL_X0 + 2 * colW;

  // Inner dividers + bottom border
  paintHLine(COL_X0, COL_X1, gridY0 + rowH, 1, Role::Ink);
  paintHLine(COL_X0, COL_X1, gridY0 + 2 * rowH, 1, Role::Ink);
  paintRect(x1, gridY0, 1, 2 * rowH, Role::Ink);
  paintRect(x2, gridY0, 1, 2 * rowH, Role::Ink);

  const int capLbl = textCapHeight(T_STATLBL);
  const bool statAccents = paintAccentsLegibleAt(18);

  for (int i = 0; i < s.statCount && i < COLS * ROWS; i++) {
    const int r = i / COLS, c = i % COLS;
    const int ry = gridY0 + r * rowH;
    const int cellX = (c == 0) ? COL_X0 : (c == 1 ? x1 + 14 : x2 + 14);
    const int cellRight = (c == 0) ? (x1 - 14) : (c == 1 ? (x2 - 14) : COL_X1);
    const int avail = cellRight - cellX;

    const day::Stat& st = s.stats[i];

    // Row 1: 18x18 icon + uppercase label
    IconStyle ist; ist.accents = statAccents;
    const int iconCy = ry + 13 + 9;
    drawIcon(st.icon, cellX + 9, iconCy, 18, ist);

    TextStyle lbl = T_STATLBL;
    const int lblX = cellX + 18 + 7;
    const int lblAvail = cellRight - lblX;
    while (lbl.tracking > 0.4f && textWidth(lbl, st.label) > lblAvail) lbl.tracking -= 0.3f;
    while (lbl.px > 9 && textWidth(lbl, st.label) > lblAvail) lbl.px -= 1;
    textDraw(lbl, lblX, iconCy + capLbl / 2, st.label);

    // Row 2: 31px primary value (with smaller unit suffix when present)
    drawStatValue(cellX, ry + 62, avail, st.value);

    // Row 3: 14px descriptive line: "Word (sub)" or "Word"
    char desc[48] = "";
    if (st.sub[0] && st.word[0]) {
      snprintf(desc, sizeof(desc), "%s (%s)", st.word, st.sub);
    } else if (st.word[0]) {
      snprintf(desc, sizeof(desc), "%s", st.word);
    } else if (st.sub[0]) {
      snprintf(desc, sizeof(desc), "%s", st.sub);
    }
    if (desc[0]) {
      TextStyle sb = T_STATSUB;
      while (sb.px > 10 && textWidth(sb, desc) > avail) sb.px -= 1;
      textDraw(sb, cellX, ry + 86, desc);
    }
  }
}

void drawAlsoLine(int x, int baseline, const char* line) {
  constexpr const char* PREFIX = "Also grab:";
  const size_t plen = strlen(PREFIX);
  if (!strncmp(line, PREFIX, plen)) {
    int pen = textDraw(T_ALSO_BOLD, x, baseline, PREFIX);
    textDraw(T_ALSO, pen, baseline, line + plen);
  } else {
    textDraw(T_ALSO, x, baseline, line);
  }
}

void drawRight(const ViewModel& vm) {
  const day::Screen& s = vm.screen;
  char lines[2][160];

  // Header: Weekday + corner status
  textDraw(T_WEEKDAY, COL_X0, 52, s.weekday);
  {
    if (vm.updatedText[0]) textDrawRight(T_CORNER, COL_X1, 35, vm.updatedText);
    if (vm.batteryPct >= 0) {
      const int baseline = 53;
      char pct[8];
      snprintf(pct, sizeof(pct), "%d%%", vm.batteryPct);
      int x = COL_X1 - textWidth(T_CORNER, pct);
      textDraw(T_CORNER, x, baseline, pct);
      batteryGlyph(x - 6 - BATT_W, baseline - BATT_H + 1, vm.batteryPct);
    }
  }

  const int ruleY = 67;
  paintHLine(COL_X0, COL_X1, ruleY, 3, Role::Ink);

  // 3x2 Conditions Grid
  const int gridY0 = ruleY + 3;
  const int rowH = 104;
  drawConditionsGrid(s, gridY0, rowH);
  const int gridY2 = gridY0 + 2 * rowH;

  // What to wear section
  const int headY = gridY2 + 16 + textCapHeight(T_SECTION);
  textDraw(T_SECTION, COL_X0, headY, s.tomorrow ? "WHAT TO WEAR TOMORROW" : "WHAT TO WEAR");

  const int tileY = headY + 9;
  const int tileH = 80;
  const int gap = 10;
  const int tileW = (COL_W - 3 * gap) / 4;
  const bool tileAccents = paintAccentsLegibleAt(30);
  for (int i = 0; i < 4; i++) {
    int x = COL_X0 + i * (tileW + gap);
    paintRoundRectStroke(x, tileY, tileW, tileH, 10, 2, Role::Ink);
    if (i < s.outcome.wearCount) {
      const day::Wear& w = s.outcome.wear[i];
      IconStyle ist; ist.accents = tileAccents;
      drawIcon(w.icon, x + tileW / 2, tileY + 29, 30, ist);
      TextStyle st = T_TILE;
      while (st.px > 10 && textWidth(st, w.label) > tileW - 10) st.px -= 1;
      textDrawCentered(st, x + tileW / 2, tileY + 66, w.label);
    }
  }

  // Footer: 1px top border, "Also grab: ..." on the left, Sunset / Parking alert on the right
  const int footerRuleY = 430;
  paintHLine(COL_X0, COL_X1, footerRuleY, 1, Role::Ink);

  int rightStartX = COL_X1;
  if (vm.parkingActive) {
    TextStyle ft = T_FOOTER;
    ft.role = paintAlertTextRole();
    int pillW = textWidth(ft, vm.parkingText) + 22;
    if (pillW > COL_W / 2) pillW = COL_W / 2;
    // A long alert shrinks to fit its bar rather than running past the ends.
    while (ft.px > 10 && textWidth(ft, vm.parkingText) > pillW - 16) ft.px -= 1;
    rightStartX = COL_X1 - pillW;
    paintAlertBar(rightStartX, footerRuleY + 10, pillW, 30, 7);
    textDrawCentered(ft, rightStartX + pillW / 2, footerRuleY + 30, vm.parkingText);
  } else if (s.footer[0]) {
    int fw = textWidth(T_FOOTER, s.footer);
    int fx = COL_X1 - fw;
    textDraw(T_FOOTER, fx, footerRuleY + 27, s.footer);
    IconStyle ist; ist.accents = false;
    drawIcon("sunset", fx - 7 - 10, footerRuleY + 22, 20, ist);
    rightStartX = fx - 7 - 20;
  }

  const int availLeft = rightStartX - COL_X0 - 16;
  if (availLeft > 60 && s.outcome.alsoGrab[0]) {
    char cleanAlso[180];
    snprintf(cleanAlso, sizeof(cleanAlso), "%s", s.outcome.alsoGrab);
    size_t alen = strlen(cleanAlso);
    if (alen > 0 && cleanAlso[alen - 1] == '.' && !strncmp(cleanAlso, "Also grab:", 10)) {
      cleanAlso[alen - 1] = '\0';
    }
    const int n = textWrap(T_ALSO, cleanAlso, availLeft, lines, 2);
    if (n == 1) {
      drawAlsoLine(COL_X0, footerRuleY + 27, lines[0]);
    } else if (n >= 2) {
      drawAlsoLine(COL_X0, footerRuleY + 20, lines[0]);
      drawAlsoLine(COL_X0, footerRuleY + 37, lines[1]);
    }
  }
}

void frame() {
  paintRoundRectStroke(12, 12, W - 24, H - 24, 8, 4, Role::Ink);
}

} // namespace

void renderBegin() {
  panelBegin();
  paintBegin();
  textInit();
}

void renderScreen(const ViewModel& vm) {
  panelFirstPage();
  do {
    panelFill(panelPaper());
    drawHero(vm);
    drawRight(vm);
  } while (panelNextPage());
}

void renderHourly(const day::Raw& raw, int dayIndex, const day::Inputs& in,
                  const char* heading, const char* footer) {
  const TextStyle HEAD { Face::Display,   26, 0,    Role::Ink };
  const TextStyle COL { Face::Body,      11, 1.5f, Role::Caption };
  const TextStyle CEL { Face::Body,      14, 0,    Role::Ink };
  const TextStyle DIM { Face::BodyLight, 13, 0,    Role::Caption };

  // One column table shared by the header and every row, so a heading can
  // never drift away from the numbers underneath it.
  struct Col { const char* name; int x; };
  static const Col cols[] = { { "HOUR", 24 }, { "TEMP", 112 }, { "RAIN", 198 },
                              { "CLOUD", 286 }, { "WIND", 378 }, { "AQI", 462 },
                              { "SKY", 546 } };
  const int nCols = (int)(sizeof(cols) / sizeof(cols[0]));

  panelFirstPage();
  do {
    panelFill(panelPaper());
    textDraw(HEAD, 24, 46, heading);

    // Naming the window matters more here than anywhere else on the device:
    // every number below is an aggregate over exactly these hours.
    char when[64];
    const day::DayMeta& meta = raw.day[dayIndex];
    if (meta.sunriseMin > 0 && meta.sunsetMin > 0) {
      char up[12], down[12];
      day::formatClock12(meta.sunriseMin, up, sizeof(up));
      day::formatClock12(meta.sunsetMin, down, sizeof(down));
      snprintf(when, sizeof(when), "%s  Â·  %s - %s", in.weekdayName, up, down);
    } else {
      snprintf(when, sizeof(when), "%s  Â·  no sun times", in.weekdayName);
    }
    textDrawRight(DIM, W - 24, 46, when);
    paintHLine(24, W - 24, 60, 3, Role::Ink);

    for (int c = 0; c < nCols; c++) textDraw(COL, cols[c].x, 84, cols[c].name);
    paintHLine(24, W - 24, 92, 1, Role::Caption);

    // Only the daylight window feeds the dashboard, so only it is listed -
    // otherwise the table would show rows the verdict never considered.
    int h0, h1;
    day::daylightHours(raw, dayIndex, h0, h1);

    // Midsummer runs to about eighteen rows and midwinter to nine, so the
    // pitch closes up to keep the whole window on one screen rather than
    // silently cutting the afternoon off the bottom.
    const int yTop = 112, yBottom = H - 70;
    const int rows = h1 - h0 + 1;
    int pitch = rows > 0 ? (yBottom - yTop) / rows : 23;
    if (pitch > 23) pitch = 23;
    if (pitch < 15) pitch = 15;

    int y = yTop, shown = 0;
    for (int h = h0; h <= h1 && y < yBottom; h++) {
      const day::HourRow* row = nullptr;
      for (int i = 0; i < raw.n; i++)
        if (raw.hours[i].day == dayIndex && raw.hours[i].hour == h) { row = &raw.hours[i]; break; }
      if (!row) continue;

      int aqi = -1;
      for (int i = 0; i < raw.na; i++)
        if (raw.aqi[i].day == dayIndex && raw.aqi[i].hour == h) { aqi = raw.aqi[i].aqi; break; }

      char buf[28];
      int dh = h % 12; if (dh == 0) dh = 12;
      snprintf(buf, sizeof(buf), "%d%s", dh, h >= 12 ? "pm" : "am");
      textDraw(CEL, cols[0].x, y, buf);
      snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf((float)row->tempF));
      textDraw(CEL, cols[1].x, y, buf);
      snprintf(buf, sizeof(buf), "%d%%", row->precipProb < 0 ? 0 : row->precipProb);
      textDraw(CEL, cols[2].x, y, buf);
      snprintf(buf, sizeof(buf), "%d%%", row->cloud < 0 ? 0 : row->cloud);
      textDraw(CEL, cols[3].x, y, buf);
      snprintf(buf, sizeof(buf), "%d", (int)lroundf((float)row->windMph));
      textDraw(CEL, cols[4].x, y, buf);
      if (aqi >= 0) { snprintf(buf, sizeof(buf), "%d", aqi); textDraw(CEL, cols[5].x, y, buf); }
      textDraw(CEL, cols[6].x, y, beach::wmoDescription(row->code));
      y += pitch;
      shown++;
    }
    if (shown == 0) textDraw(CEL, 24, yTop + 12, "No hourly rows for this day.");

    // The figures the rules actually ran on, so the summary can be checked
    // against the rows above it.
    paintHLine(24, W - 24, H - 60, 1, Role::Caption);
    char line[168];
    snprintf(line, sizeof(line),
             "%d\xC2\xB0-%d\xC2\xB0   rain %d%%   wind %d   cloud %d%%   hum %d-%d%%   aqi %d-%d",
             (int)in.tempMinF, (int)in.tempMaxF, (int)in.precipChanceMaxPct,
             (int)in.windMaxMph, (int)in.cloudCoverAvgPct,
             (int)in.humidityMinPct, (int)in.humidityMaxPct, (int)in.aqiMin, (int)in.aqiMax);
    textDraw(DIM, 24, H - 36, line);
    if (footer) textDrawRight(DIM, W - 24, H - 36, footer);
  } while (panelNextPage());
}

void renderMessage(const char* title, const char* line1, const char* line2) {
  panelFirstPage();
  do {
    panelFill(panelPaper());
    frame();
    textDrawCentered(T_MSG_H, W / 2, H / 2 - 36, title);
    char lines[2][160];
    int y = H / 2 + 8;
    if (line1) { int n = textWrap(T_MSG_B, line1, W - 120, lines, 2); for (int i = 0; i < n; i++) { textDrawCentered(T_MSG_B, W / 2, y, lines[i]); y += 22; } }
    y += 8;
    if (line2) { int n = textWrap(T_MSG_B, line2, W - 120, lines, 2); for (int i = 0; i < n; i++) { textDrawCentered(T_MSG_B, W / 2, y, lines[i]); y += 22; } }
  } while (panelNextPage());
}

void renderSetup(const char* apName, const char* ip) {
  panelFirstPage();
  do {
    panelFill(panelPaper());
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

    textDrawCentered(T_STATUS, W / 2, H - 42, "Press Green to cancel and return to the forecast.");
    textDrawCentered(T_STATUS, W / 2, H - 28, "This screen turns off automatically after 10 minutes.");
  } while (panelNextPage());
}

void renderEnd() { panelHibernate(); }
