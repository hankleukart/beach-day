// The only file that knows which physical panel this build drives.
//
// Both reTerminal panels are 800x480 and share a pin map, so the layout above
// is identical; what differs is the controller and whether colour exists.
// One panel_*.cpp is compiled per build environment.
#pragma once
#include <cstdint>

constexpr int PANEL_W = 800;
constexpr int PANEL_H = 480;

void panelBegin();
void panelFirstPage();
bool panelNextPage();
void panelHibernate();

void panelFill(uint16_t color);
void panelPixel(int x, int y, uint16_t color);
void panelHSpan(int x0, int x1, int y, uint16_t color);   // inclusive

// True on a panel with real inks, so accents are painted rather than dithered.
bool panelIsColor();
// What this panel can actually show for each paint role.
uint16_t panelInk();
uint16_t panelPaper();
uint16_t panelCaption();
uint16_t panelAccent(int role);   // 2 sun, 3 water, 4 leaf, 5 warm
