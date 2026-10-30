// reTerminal E1001: 7.5" monochrome, GDEY075T7 panel, UC8179 controller.
#include "panel_ops.h"
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include "pins.h"

namespace {
SPIClass hspi(HSPI);
// Full-height page buffer (800*480/8 = 48 KB): one pass per refresh. Static,
// like the E1002's, so both always fit and nothing depends on heap at boot.
GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>
    display(GxEPD2_750_GDEY075T7(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

void begin() {
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS, OUTPUT);
  hspi.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
  display.epd2.selectSPI(hspi, SPISettings(2000000, MSBFIRST, SPI_MODE0));
  display.init(0, true, 10, false);
  display.setRotation(0);
  display.setFullWindow();
  Serial.println(F("[panel] E1001 mono 800x480 (GDEY075T7)"));
}
void firstPage() { display.setFullWindow(); display.firstPage(); }
bool nextPage()  { return display.nextPage(); }
void hibernate() { display.hibernate(); }
void fill(uint16_t c) { display.fillScreen(c); }
void pixel(int x, int y, uint16_t c) { display.drawPixel(x, y, c); }
void hspan(int x0, int x1, int y, uint16_t c) { display.drawFastHLine(x0, y, x1 - x0 + 1, c); }
uint16_t accent(int) { return GxEPD_BLACK; }   // unused: accents dither here
}  // namespace

const PanelOps PANEL_E1001 = {
  "E1001", begin, firstPage, nextPage, hibernate, fill, pixel, hspan,
  /*isColor=*/false, GxEPD_BLACK, GxEPD_WHITE, /*caption=*/GxEPD_BLACK, accent,
};
