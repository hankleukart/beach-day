// reTerminal E1002: 7.3" 6-colour ACeP, GDEP073E01 panel, ED2208 controller.
// Black, white, red, green, blue, yellow - and deliberately no orange: the
// driver would silently map it to the nearest ink and render the wrong colour.
#include "panel_ops.h"
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_7C.h>
#include "pins.h"

namespace {
SPIClass hspi(HSPI);
// 4 bits per pixel, so a full-height buffer would be 187 KB - more internal
// RAM than is free. 80 rows (32 KB) gives six passes per refresh, which costs
// re-running the draw code but never re-runs the panel's ~30 s update.
#define MAX_DISPLAY_BUFFER_SIZE 32000u
#define MAX_HEIGHT(EPD) \
    (EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2) \
         ? EPD::HEIGHT : (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2))
GxEPD2_7C<GxEPD2_730c_GDEP073E01, MAX_HEIGHT(GxEPD2_730c_GDEP073E01)>
    display(GxEPD2_730c_GDEP073E01(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

void begin() {
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  hspi.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
  display.epd2.selectSPI(hspi, SPISettings(2000000, MSBFIRST, SPI_MODE0));
  display.init(0, true, 10, false);
  display.setRotation(0);
  display.setFullWindow();
  Serial.println(F("[panel] E1002 6-colour 800x480 (GDEP073E01)"));
}
void firstPage() { display.setFullWindow(); display.firstPage(); }
bool nextPage()  { return display.nextPage(); }
void hibernate() { display.hibernate(); }
void fill(uint16_t c) { display.fillScreen(c); }
void pixel(int x, int y, uint16_t c) { display.drawPixel(x, y, c); }
void hspan(int x0, int x1, int y, uint16_t c) { display.drawFastHLine(x0, y, x1 - x0 + 1, c); }
uint16_t accent(int role) {
  switch (role) {
    case 2:  return GxEPD_YELLOW;   // Sun
    case 3:  return GxEPD_BLUE;     // Water
    case 4:  return GxEPD_GREEN;    // Leaf
    default: return GxEPD_RED;      // Warm
  }
}
}  // namespace

const PanelOps PANEL_E1002 = {
  "E1002", begin, firstPage, nextPage, hibernate, fill, pixel, hspan,
  /*isColor=*/true, GxEPD_BLACK, GxEPD_WHITE, /*caption: no grey ink*/ GxEPD_BLACK, accent,
};
