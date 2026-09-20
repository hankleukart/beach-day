// Draws the vector icons from icons_data.h at any size, through paint roles.
#pragma once
#include "paint.h"

struct IconStyle {
  Role ink = Role::Ink;       // strokes
  Role paper = Role::Paper;   // knockouts (inside of outlines)
  bool accents = true;        // paint the colour fills (as dither on mono)
};

bool iconExists(const char* name);
// Centred on (cx, cy); size is the box the 48-unit design maps to.
void drawIcon(const char* name, int cx, int cy, int size, const IconStyle& style = IconStyle{});
