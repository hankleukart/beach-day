# Beach Day — reTerminal E1001

A self-contained Beach Day display. The board fetches Open-Meteo directly over
Wi-Fi, evaluates the rules in [../shared/rules.json](../shared/rules.json),
renders to the ePaper panel, and deep-sleeps until the next update.

**No Hubitat hub. No TRMNL account or server. No always-on machine anywhere.**

> Status: scaffolding. The folder structure and plan are in place; the firmware
> is not written yet.

## Hardware

[Seeed reTerminal E1001](https://www.seeedstudio.com/reTerminal-E1001-p-6534.html)

| | |
| --- | --- |
| Display | 7.5" monochrome ePaper, 800×480, 4-level grayscale, 2–5 s full refresh, partial refresh supported |
| MCU | ESP32-S3R8 (Wi-Fi + BLE) |
| Flash | 32 MB SPI |
| Battery | 2000 mAh, up to ~3 months |
| Also onboard | microSD slot, temp/humidity sensor, microphone, buzzer, buttons, LEDs, 8-pin GPIO header |

The panel is 800×480 — the same aspect ratio as the TRMNL OG, so the existing
layout proportions carry over. The Liquid template already sizes everything in
container-query units, which makes it a usable reference for the draw code even
though none of it can run here.

## How it will work

```
deep sleep  ──wake──▶  Wi-Fi  ──▶  Open-Meteo forecast + air quality
                                            │
                                            ▼
                                   aggregate active beach hours
                                   (9 AM – 6 PM: max precip %, max AQI,
                                    first sunny hour, daylight weather code)
                                            │
                                            ▼
                                   evaluate shared/rules.json
                                   → one state + six condition flags
                                            │
                                            ▼
                                   render 800×480 ──▶ deep sleep
```

Two Open-Meteo endpoints, both keyless:

- `api.open-meteo.com/v1/forecast` — hourly + daily temp, precipitation
  probability, wind, UV, weather code, sunrise/sunset
- `air-quality-api.open-meteo.com/v1/air-quality` — hourly `us_aqi`

This collapses the work currently split across
[open-meteo-weather-enhanced.groovy](../hubitat-trmnl/open-meteo-weather-enhanced.groovy)
(fetch), [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy)
(aggregate), and [beach_day.liquid](../hubitat-trmnl/beach_day.liquid)
(decide + render) into a single firmware image.

## Planned layout

```
reterminal-e1001/
├── platformio.ini          build config, board + lib pins
├── src/
│   ├── main.cpp            wake → fetch → decide → draw → sleep
│   ├── config.h            Wi-Fi, lat/lon, timezone, wake interval
│   ├── weather.cpp/.h      Open-Meteo fetch + active-hours aggregation
│   ├── rules.cpp/.h        shared/rules.json, transcribed to constants
│   ├── parking.cpp/.h      street-cleaning schedule evaluation
│   └── render.cpp/.h       800×480 draw
└── test/                   rules tested against shared/sample_data.json
```

`rules.cpp` is a hand-transcription of `shared/rules.json` — the ESP32 should
not be parsing JSON rules at boot. The test suite is what keeps the
transcription honest: it replays `shared/sample_data.json` through `rules.cpp`
and asserts the state ids match.

## Open questions

- **Framework** — Arduino (faster to write, `GxEPD2` / `Seeed_GFX` are
  documented for this board) vs ESP-IDF (better sleep control). Leaning Arduino.
- **Wake cadence** — the tradeoff against the 3-month battery figure. Hourly
  during the day and a single overnight wake is the likely shape, since the
  display flips to tomorrow's forecast after sunset and nothing changes until
  morning.
- **Partial refresh** — worth using for the clock line, but ghosting on mono
  ePaper means a periodic full refresh is still needed.
- **Config at runtime** — hardcode Wi-Fi/location in `config.h` first. A
  captive-portal setup screen is a later nicety, not a v1 requirement.
- **Onboard sensors** — the board knows its own indoor temp and humidity. Not
  part of the beach-day rules, but it's free data if the layout has room.
