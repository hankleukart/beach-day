// reTerminal E1001 pin map (Seeed wiki, Arduino cookbooks). ESP32-S3 GPIO numbers.
#pragma once

// ePaper, GDEY075T7 / UC8179 on HSPI. No MISO, no power-enable pin.
constexpr int PIN_EPD_SCK  = 7;
constexpr int PIN_EPD_MOSI = 9;
constexpr int PIN_EPD_CS   = 10;
constexpr int PIN_EPD_DC   = 11;
constexpr int PIN_EPD_RST  = 12;
constexpr int PIN_EPD_BUSY = 13;

// Front buttons, active low with hardware pull-ups. KEY0 is RTC-capable and
// is the documented deep-sleep wake source.
constexpr int PIN_KEY0 = 3;   // right
constexpr int PIN_KEY1 = 4;   // middle
constexpr int PIN_KEY2 = 5;   // left

constexpr int PIN_LED    = 6;   // inverted: LOW = on
constexpr int PIN_BUZZER = 45;

// Battery: enable the divider, read ADC, multiply by 2.
constexpr int PIN_BAT_ADC = 1;
constexpr int PIN_BAT_EN  = 21;
