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

### Serial

The USB-C port goes to a **hardware USB-to-UART bridge**, so it enumerates as
`/dev/cu.usbserial-*` and esptool resets the board via RTS. Two things follow:

- The port stays present no matter what the firmware does, deep sleep included.
- `Serial` must map to **UART0** (GPIO43/44), which is why
  [platformio.ini](platformio.ini) sets `ARDUINO_USB_CDC_ON_BOOT=0`. With that
  flag at `1`, `Serial` goes to the ESP32-S3's native USB peripheral, which
  nothing here is wired to — the board looks dead on serial while ESP-IDF's own
  log lines still come through, which is a confusing way to lose an hour.

`Ctrl-C` quits the monitor; the board keeps running.

### The dev build

`./flash.sh` builds `[env:dev]`, which stays awake instead of deep-sleeping and
turns the board into a layout test rig. Drive it from the monitor by typing, or
from the three front buttons:

| Key | Button | Action |
| --- | --- | --- |
| `n` | KEY2 (left) | Step through all 9 visual states with plausible numbers |
| `p` | KEY1 (middle) | Toggle the parking-alert bar |
| `l` | KEY0 (right) | Back to the real forecast fetched at boot |
| `d` | — | Dump the current view as text |
| `R` | — | Reboot and re-fetch |
| `?` | — | Reprint the key list |

The state cycling is the point: today's real weather only ever produces one
state, and Grey Day or Indoor Day might not turn up for weeks. The green LED
blinks every 2 s to show the board is awake.

### What to check, in order

1. **Serial first.** The boot log prints the verdict, every condition flag, and
   the daylight window it derived:
   ```
   [beach] local 14:22 dow 4 dom 18 (offset -25200, ntp)
   [beach] window today 06:38-18:57  sun 9/13 hrs first 9  |  tomorrow 7/13 hrs first 11
   [beach] beach_day  temp 78 precip 10 wind 9 aqi 42 code 1  sun[9 AM] temp[1] ... parking[-]
   ```
   If those are right, the rules work and anything wrong is layout. `d` reprints
   the same detail for whatever is on screen.
2. **Geometry.** Hero and details panels fill 800×480 with even margins, nothing
   clipped at the right edge.
3. **Text fit.** The long strings are the risk: `Air Purifier On Max`,
   `But not quite beachy`, and a worst-case `NO PARKING LEFT SIDE: 11:30AM-1PM`.
4. **Icons.** Drawn from primitives, not bitmaps — recognisable stand-ins for
   the SVGs rather than copies.
5. **Contrast.** Pure black/white; the template's greys render black.

When the layout looks right, `./flash.sh release` and let it sleep.

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
