#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>
#include "text.h"
#include "fontdata.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>

namespace {

struct FaceInfo { stbtt_fontinfo info; const uint8_t* data; bool ok; uint8_t threshold; };
FaceInfo faces[3];
uint8_t glyphBuf[128 * 128];

FaceInfo& face(Face f) { return faces[(int)f]; }

uint32_t decodeUtf8(const char*& s) {
  uint8_t c = (uint8_t)*s++;
  if (c < 0x80) return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
  uint32_t cp = c & (0x3F >> extra);
  while (extra-- > 0 && *s) cp = (cp << 6) | ((uint8_t)*s++ & 0x3F);
  return cp;
}

float scaleFor(const FaceInfo& fi, float px) { return stbtt_ScaleForMappingEmToPixels(&fi.info, px); }

} // namespace

void textInit() {
  struct { const uint8_t* d; uint8_t thr; } src[3] = {
    { FONT_FRAUNCES_BLACK, 96 },     // heavy face: keep thin joins by thresholding low
    { FONT_NUNITO_EXTRABOLD, 104 },
    { FONT_NUNITO_SEMIBOLD, 118 },   // lighter face: a higher threshold keeps counters open
  };
  for (int i = 0; i < 3; i++) {
    faces[i].data = src[i].d;
    faces[i].threshold = src[i].thr;
    faces[i].ok = stbtt_InitFont(&faces[i].info, src[i].d, stbtt_GetFontOffsetForIndex(src[i].d, 0)) != 0;
    if (!faces[i].ok) Serial.printf("[text] face %d failed to init\n", i);
  }
}

int textWidth(const TextStyle& st, const char* utf8) {
  FaceInfo& fi = face(st.face);
  if (!fi.ok || !utf8) return 0;
  float sc = scaleFor(fi, st.px), pen = 0;
  uint32_t prev = 0;
  int first = 1;
  for (const char* s = utf8; *s; ) {
    uint32_t cp = decodeUtf8(s);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&fi.info, (int)cp, &adv, &lsb);
    if (prev) pen += stbtt_GetCodepointKernAdvance(&fi.info, (int)prev, (int)cp) * sc;
    if (!first) pen += st.tracking;
    pen += adv * sc;
    prev = cp; first = 0;
  }
  return (int)lroundf(pen);
}

int textDraw(const TextStyle& st, int x, int baseline, const char* utf8) {
  FaceInfo& fi = face(st.face);
  if (!fi.ok || !utf8) return x;
  float sc = scaleFor(fi, st.px), pen = (float)x;
  uint32_t prev = 0;
  int first = 1;
  for (const char* s = utf8; *s; ) {
    uint32_t cp = decodeUtf8(s);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&fi.info, (int)cp, &adv, &lsb);
    if (prev) pen += stbtt_GetCodepointKernAdvance(&fi.info, (int)prev, (int)cp) * sc;
    if (!first) pen += st.tracking;
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBoxSubpixel(&fi.info, (int)cp, sc, sc, pen - floorf(pen), 0, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0 && w <= 128 && h <= 128) {
      stbtt_MakeCodepointBitmapSubpixel(&fi.info, glyphBuf, w, h, w, sc, sc, pen - floorf(pen), 0, (int)cp);
      int gx = (int)floorf(pen) + x0, gy = baseline + y0;
      for (int yy = 0; yy < h; yy++) {
        const uint8_t* row = glyphBuf + yy * w;
        int runStart = -1;
        for (int xx = 0; xx <= w; xx++) {
          bool on = xx < w && row[xx] >= fi.threshold;
          if (on && runStart < 0) runStart = xx;
          if (!on && runStart >= 0) { paintSpan(gy + yy, gx + runStart, gx + xx - 1, st.role); runStart = -1; }
        }
      }
    }
    pen += adv * sc;
    prev = cp; first = 0;
  }
  return (int)lroundf(pen);
}

void textDrawCentered(const TextStyle& st, int cx, int baseline, const char* utf8) {
  textDraw(st, cx - textWidth(st, utf8) / 2, baseline, utf8);
}
void textDrawRight(const TextStyle& st, int xRight, int baseline, const char* utf8) {
  textDraw(st, xRight - textWidth(st, utf8), baseline, utf8);
}

int textCapHeight(const TextStyle& st) {
  FaceInfo& fi = face(st.face);
  if (!fi.ok) return (int)(st.px * 0.7f);
  int x0, y0, x1, y1;
  float sc = scaleFor(fi, st.px);
  stbtt_GetCodepointBitmapBox(&fi.info, 'H', sc, sc, &x0, &y0, &x1, &y1);
  return y1 - y0;
}

int textLineHeight(const TextStyle& st) {
  FaceInfo& fi = face(st.face);
  if (!fi.ok) return (int)(st.px * 1.2f);
  int asc, desc, gap;
  stbtt_GetFontVMetrics(&fi.info, &asc, &desc, &gap);
  return (int)lroundf((asc - desc + gap) * scaleFor(fi, st.px));
}

int textWrap(const TextStyle& st, const char* utf8, int maxWidth, char lines[][160], int maxLines) {
  int n = 0;
  char cur[160] = "";
  const char* p = utf8;
  while (*p && n < maxLines) {
    // next word
    const char* ws = p;
    while (*p && *p != ' ') p++;
    char word[160];
    size_t wl = (size_t)(p - ws); if (wl >= sizeof(word)) wl = sizeof(word) - 1;
    memcpy(word, ws, wl); word[wl] = '\0';
    while (*p == ' ') p++;

    char trial[160];
    if (cur[0]) snprintf(trial, sizeof(trial), "%s %s", cur, word); else snprintf(trial, sizeof(trial), "%s", word);
    if (textWidth(st, trial) <= maxWidth || !cur[0]) {
      snprintf(cur, sizeof(cur), "%s", trial);
    } else {
      snprintf(lines[n++], 160, "%s", cur);
      snprintf(cur, sizeof(cur), "%s", word);
    }
  }
  if (cur[0] && n < maxLines) snprintf(lines[n++], 160, "%s", cur);
  return n;
}
