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

Needs [PlatformIO](https://platformio.org/). The board profile, PSRAM mode and
libraries are all pinned in [platformio.ini](platformio.ini) — no Arduino IDE
setup. One-time:

```sh
python3 -m venv .venv && .venv/bin/pip install platformio
cp src/beachday_config.example.h src/beachday_config.h   # then edit: Wi-Fi, lat/lon, name
```

`beachday_config.h` is gitignored so your Wi-Fi password stays out of the repo.

Then [flash.sh](flash.sh) wraps the usual commands:

```sh
./flash.sh          # dev build: stays awake, buttons run a self-test
./flash.sh release  # real build: renders once, then deep-sleeps
./flash.sh test     # rules tests on your Mac, no board needed
```

## Testing on the bench

**Use the `dev` build first.** The release build deep-sleeps at the end of
`setup()`, and because serial runs over the ESP32-S3's native USB, sleeping
tears the serial port down a few seconds after boot — `pio device monitor`
drops and the next upload has nothing to talk to. The `dev` build stays awake
instead, so serial stays up and re-flashing just works.

In the dev build the three front buttons do:

| Button | Action |
| --- | --- |
| KEY2 (left) | Step through all 9 visual states with plausible numbers |
| KEY1 (middle) | Toggle the parking-alert bar on the current screen |
| KEY0 (right) | Go back to the real forecast fetched at boot |

That state cycling is the point: otherwise you can only ever see whichever
state today's actual weather produces, and Grey Day or Indoor Day might not
show up for weeks. The green LED blinks every 2 s so you can tell it is awake.

**If the upload can't find the board**, press KEY0 — it is the configured
deep-sleep wake button, so it brings the board (and its USB port) back up.
`ls /dev/cu.*` should then show a new entry.

What to check on the panel, in order:

1. **Serial first.** The boot log prints the verdict and every condition flag:
   `beach_day temp 78 precip 10 wind 9 aqi 42 code 1 sun[6 AM] temp[1] rain[1] ...`
   If that line is right, the rules are working and anything wrong is layout.
2. **Geometry.** Hero panel and details panel should fill the 800×480 with even
   margins, nothing clipped at the right edge.
3. **Text fit.** The long subtitles (`Air Purifier On Max`, `But not quite
   beachy`) and a long `NO PARKING LEFT SIDE: 11:30AM-1PM` are the ones most
   likely to overflow.
4. **Icons.** They are drawn from primitives, not bitmaps — recognisable
   stand-ins for the SVGs rather than copies.
5. **Contrast.** Pure black/white only; the template's greys render black.

When the layout looks right, flash `./flash.sh release` and let it sleep.

## How it works

```
deep sleep ──wake──▶ Wi-Fi ──▶ NTP ──▶ Open-Meteo forecast + air quality
                                              │
                                              ▼
                                  aggregate  (lib/beachrules/aggregate.cpp)
                                  each day's own sunrise→sunset hours:
                                  max precip, AQI min/max, humidity min/max,
                                  weather-code mode, sunny-hour scan
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
