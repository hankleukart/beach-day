// Text through stb_truetype: the design's real typefaces, rasterised on the
// device at whatever pixel size the layout asks for. Coverage is thresholded
// to 1-bit and painted through roles.
#pragma once
#include "paint.h"

// Archivo Black for headlines - a uniform heavy grotesque, because a 1-bit
// panel cannot render a hairline - and Atkinson Hyperlegible for everything
// else, drawn for low vision and so still distinguishable when detail is lost.
enum class Face : uint8_t { Display, Body, BodyLight };

struct TextStyle {
  Face  face = Face::Body;
  float px = 14;           // CSS-style font size in pixels (em)
  float tracking = 0;      // extra px between glyphs
  Role  role = Role::Ink;
  // Thickens strokes by a pixel before thresholding. At caption sizes a stem
  // lands near half a pixel and survives or vanishes by luck; this makes the
  // choice consistently "survives". Costs a little letter spacing.
  bool  embolden = false;
};

void textInit();
int  textWidth(const TextStyle& st, const char* utf8);
int  textDraw(const TextStyle& st, int x, int baseline, const char* utf8);     // returns pen x after
void textDrawCentered(const TextStyle& st, int cx, int baseline, const char* utf8);
void textDrawRight(const TextStyle& st, int xRight, int baseline, const char* utf8);
int  textCapHeight(const TextStyle& st);
int  textLineHeight(const TextStyle& st);
// Greedy word wrap into up to maxLines; returns the count. Lines are NUL-terminated copies.
int  textWrap(const TextStyle& st, const char* utf8, int maxWidth, char lines[][160], int maxLines);
