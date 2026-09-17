# Beach Day — reTerminal E1001

A self-contained Beach Day display. The board fetches Open-Meteo directly over
Wi-Fi, evaluates the rules in [../shared/rules.json](../shared/rules.json),
renders to the ePaper panel, and deep-sleeps until the next update.

**No Hubitat hub. No TRMNL account or server. No always-on machine anywhere.**

> Status: firmware written and compiling; the decision logic passes the shared
> fixture suite on the host. Not yet flashed to hardware — expect a round of
> layout tweaks once it is.

## Hardware

[Seeed reTerminal E1001](https://www.seeedstudio.com/reTerminal-E1001-p-6534.html)

| | |
| --- | --- |
| Display | 7.5" monochrome ePaper, 800×480, GDEY075T7 panel / UC8179 controller |
| MCU | ESP32-S3R8 (Wi-Fi + BLE), 8 MB PSRAM, 32 MB flash |
| Battery | 2000 mAh LiPo |
| Also onboard | 3 front buttons, LED, buzzer, microSD, SHT4x temp/humidity, PCF8563 RTC |

The panel is the same 800×480 as the TRMNL OG, so the layout proportions carry
straight over from [beach_day.liquid](../hubitat-trmnl/beach_day.liquid).

## Build and flash

Needs [PlatformIO](https://platformio.org/) (`pip install platformio` or the
VS Code extension). No Arduino IDE setup required; the board profile, PSRAM
mode, and libraries are all pinned in [platformio.ini](platformio.ini).

```sh
cp src/beachday_config.example.h src/beachday_config.h   # then edit: Wi-Fi, lat/lon, name
pio run -t upload                        # build + flash over USB-C
pio device monitor                       # 115200 baud
```

`beachday_config.h` is gitignored so your Wi-Fi password stays out of the repo.

## How it works

```
deep sleep ──wake──▶ Wi-Fi ──▶ NTP ──▶ Open-Meteo forecast + air quality
                                              │
                                              ▼
                                  aggregate  (lib/beachrules/aggregate.cpp)
                                  active hours 9–18: first sunny hour, code mode,
                                  max precip; sunrise→sunset hours: AQI min/max
                                              │
                                              ▼
                                  evaluate   (lib/beachrules/beachrules.cpp)
                                  six conditions → one of nine states
                                              │
                                              ▼
                                  render 800×480 ──▶ hibernate panel ──▶ deep sleep
```

Two keyless Open-Meteo endpoints, requested with `timezone=auto` and
`timeformat=unixtime` so the API tells us the local offset (DST included) and
every timestamp is unambiguous:

- `api.open-meteo.com/v1/forecast` — hourly humidity / precip probability /
  weather code, daily highs, lows, wind, UV, sunrise, sunset
- `air-quality-api.open-meteo.com/v1/air-quality` — hourly `us_aqi`

### Files

```
platformio.ini            board, PSRAM mode, libraries, host test env
src/
  main.cpp                the wake→fetch→decide→draw→sleep cycle
  beachday_config.example.h  copy to beachday_config.h; Wi-Fi, location, parking, intervals
  settings.*              beachday_config.h defaults, overridable from NVS (for a future setup portal)
  weather.*               Open-Meteo requests, JSON → RawForecast; no rules here
  render.*                GxEPD2 drawing: hero panel, checklist, footer, icons
  power.*                 battery ADC, deep sleep with KEY0 as a wake button
  net.*                   Wi-Fi, NTP, HTTPS GET
  pins.h                  E1001 GPIO map
lib/beachrules/           pure C++ — no Arduino — shared logic
  rules_config.h          GENERATED from shared/rules.json
  beachrules.*            six conditions, nine-state priority, sun scan
  aggregate.*             Open-Meteo samples → rule inputs (the asymmetries live here)
  parking.*               street-cleaning alert window
  wmo.*                   WMO code → text
test/test_rules/
  test_rules.cpp          Unity tests
  fixtures.h              GENERATED from shared/fixtures/rules-cases.json
tools/gen.py              regenerates both GENERATED files
```

## Keeping the rules in sync

The rules exist once, in `shared/`. This target does not hand-copy them:

```sh
# after editing ../shared/rules.json or ../shared/fixtures/rules-cases.json
python3 tools/gen.py        # rewrites rules_config.h and fixtures.h
pio test -e native          # replays every fixture through lib/beachrules on your Mac
```

If a threshold in `rules.json` changes and the firmware does not agree, the
test names the case. The Liquid template still has to be edited by hand — it
runs on TRMNL's servers and cannot read this repo.

## Wake schedule and battery

The display only changes at a handful of moments, so the board does not poll
blindly. Each wake it sleeps until the sooner of:

- the regular interval — `CFG_WAKE_DAY_MINUTES` (default 60) between sunrise−1h
  and sunset, `CFG_WAKE_NIGHT_MINUTES` (default 240) overnight
- the next moment the screen would change: sunrise, sunset−1h (night mode
  flips on), sunset (parking alert can appear), or a parking cutoff time

Rough budget at those defaults: ~17 wakes a day at ~12 s each on Wi-Fi, plus
14 µA asleep. Call it 6–8 mAh/day against a 2000 mAh cell — comfortably in the
"months" range Seeed quotes, and far more sensitive to the interval you choose
than to anything in the code.

Pressing the right-hand button (KEY0) wakes the board for an immediate refresh.

## When the network is down

The panel keeps its image with no power, so a failed fetch does not blank it.
The last good screen is redrawn once with an `OFFLINE since 2:05 PM` stamp in
the header, then the board retries every `CFG_RETRY_MINUTES` (10), backing off
to hourly after six misses. If the air-quality request alone fails, the
forecast still renders and AQI reads 0.

## Setup for other people (not built yet)

Today configuration is compile-time (`beachday_config.h`). `settings.cpp` already reads
NVS overrides for Wi-Fi and location, which is the hook for a captive-portal
flow: hold a button at wake → the board becomes a `BeachDay-Setup` hotspot → a
phone page collects Wi-Fi, a location (Open-Meteo's geocoding API accepts
postal codes), a label, and the parking rules → saved to NVS. That is the next
piece of work if this is going to be handed to someone who won't run PlatformIO.

## Open items after first flash

- Font sizes are chosen from Adafruit GFX's bundled Free Sans Bold set to
  approximate the Liquid's `cqh` values; expect to nudge them.
- Icons are drawn with primitives (circles, thick lines), not bitmaps. They
  are recognisable stand-ins for the SVGs, not copies.
- The panel supports 4-level grey; this build is pure black/white. The
  template's grey "fail" icons and grey footer text render black.
- The onboard SHT4x and RTC are unused. The RTC would let the board skip NTP.
