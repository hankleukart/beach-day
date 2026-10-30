// The two panel drivers, as tables. Both are compiled into every build;
// panel.cpp picks one at boot from panelModel(). Nothing outside panel*.cpp
// needs this - the rest of the firmware only sees panel.h.
#pragma once
#include <cstdint>

struct PanelOps {
  const char* name;
  void (*begin)();
  void (*firstPage)();
  bool (*nextPage)();
  void (*hibernate)();
  void (*fill)(uint16_t color);
  void (*pixel)(int x, int y, uint16_t color);
  void (*hspan)(int x0, int x1, int y, uint16_t color);
  bool isColor;
  uint16_t ink, paper, caption;
  uint16_t (*accent)(int role);
};

extern const PanelOps PANEL_E1001;
extern const PanelOps PANEL_E1002;
