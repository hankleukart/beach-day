# Beach Day — reTerminal E1001

A self-contained Beach Day display. The board fetches Open-Meteo directly over
Wi-Fi, evaluates the rules in [../shared/rules.json](../shared/rules.json),
renders to the ePaper panel, and deep-sleeps until the next update.

**No Hubitat hub. No TRMNL account or server. No always-on machine anywhere.**

> Status: first flashed 2026-09-18. Rules pass the shared fixture suite on the
> host; setup portal, OTA updates and the dev self-test are in. Layout on the
> real panel still needs a tuning pass.

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

## Updating devices you have given away

Each board checks a small JSON manifest for a newer firmware version and updates
itself. Nothing connects *to* the device, so it works fine behind someone else's
router, and the board can stay asleep most of the day.

### Publishing an update

```sh
# 1. bump FIRMWARE_VERSION in src/version.h
# 2.
tools/release.sh --publish        # needs the gh CLI
```

That runs the rules tests, builds the release firmware, writes
`dist/manifest.json` with the binary's size and MD5, and creates a GitHub
release tagged `fw-v<version>` marked **latest**. Devices fetch
`releases/latest/download/manifest.json`, compare its `version` against their
own, and update when the two differ.

Run it without `--publish` first to see the manifest and confirm the version.
**Bumping `src/version.h` is what triggers the update** — republishing the same
version number does nothing.

### When devices pick it up

- On their own within `CFG_OTA_CHECK_HOURS` (default 24). The check is skipped
  unless that long has passed, so it costs one small HTTPS request a day.
- Immediately if someone **holds the left button (KEY2) while the board wakes**.
  That is the instruction to give a friend: *hold the left button and press the
  right one.*

The check runs **after** the screen has been drawn, so a failed or interrupted
update never leaves a viewer looking at a stale or blank panel.

### Safeguards

- **Battery floor.** No update is started below `CFG_OTA_MIN_BATTERY_PCT`
  (default 30%), so a flash cannot die half-written.
- **MD5 and size.** Both are checked against the manifest. This catches the
  realistic mistake — publishing the wrong or a truncated asset — before the
  image is ever booted.
- **Two OTA slots.** The new image is written to the inactive partition and only
  becomes the boot target once fully written and verified. A power loss mid-flash
  leaves the running firmware untouched.
- **Safe mode.** A counter in NVS is bumped on every boot and cleared when a
  cycle reaches sleep. After 3 consecutive boots that never reached sleep, the
  board stops doing anything except looking for newer firmware, shows
  *"Updating — looking for new software"*, and retries every 30 minutes. This is
  the recovery path if a release is broken: publish a fix and the devices take it
  themselves.

### Known limits

- **No automatic rollback.** The stock Arduino bootloader does not build with
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, so a new image that boots but
  misbehaves stays selected. Safe mode covers the crash case; it does not cover
  firmware that runs happily while drawing the wrong thing. Test on a bench unit
  before publishing.
- **TLS is not authenticated.** `setInsecure()` encrypts the transfer but does
  not verify GitHub's certificate, the same tradeoff as the weather requests —
  it avoids a device in a drawer bricking when a root CA rotates. Someone who
  can actively intercept traffic on a friend's home network could serve their own
  firmware. For a handful of units among friends that is a reasonable trade; if
  this ever goes wider, move to ESP-IDF signed OTA, which verifies the image
  against a public key burned into the firmware and does not depend on the
  transport at all.
- **Version comparison is equality, not ordering.** Any difference triggers an
  update, so the manifest can also be used to deliberately downgrade.

## Giving one to someone else

Flash the board with the example config untouched:

```sh
cp src/beachday_config.example.h src/beachday_config.h   # leave YOUR_WIFI as it is
./flash.sh release
```

The `YOUR_WIFI` placeholder means "not configured", so on first power-up the
board skips straight to the setup portal. Nothing about your friend's Wi-Fi or
address ever needs to be in your build.

### What your friend sees

The e-paper shows the instructions itself, so there is nothing to print out:

1. Join the Wi-Fi network **BeachDay-Setup-xxxx** on a phone (open network;
   the name is on the screen).
2. A setup page pops up on its own — the board answers every captive-portal
   probe iOS, Android and Windows make. If it doesn't, browse to
   `http://192.168.4.1`.
3. Pick their Wi-Fi from the scanned list, type the password, and enter where
   the beach is: a **postal code** (`90401`), a **town** (`Santa Monica`) or
   **town, state** (`Springfield, IL`). Optional: a short label for the screen
   and the street-cleaning parking schedule. Tap **Save and restart**.

The board restarts, joins their Wi-Fi, geocodes the place with Open-Meteo's
free geocoding API, stores the coordinates, and draws the forecast. The place
name it resolved appears at the top of the hero panel — that is the
confirmation. If it could not find the place, the screen says so and how to
retry.

The hotspot times out after 10 minutes. The LED blinks fast while the portal
is up.

### Button gestures

The right button (KEY0) is the wake button. Holding another button while
pressing it changes what the wake does:

| Hold | + press | Result |
| --- | --- | --- |
| — | KEY0 (right) | Refresh the forecast now |
| KEY1 (middle) | KEY0 | Open the setup portal (change Wi-Fi, place, parking) |
| KEY2 (left) | KEY0 | Check for a firmware update immediately |

### Why geocoding happens after the portal, not in it

While the board is a hotspot it has no internet. Captive-portal libraries that
geocode live do it by running station and access point at once, and that is
their flaky part: the board must hop to the router's channel, which drops the
phone mid-form. Saving the text and resolving it on the next connected boot is
slower by one reboot and fails far less. The e-paper does the confirming.

### Where it lives

| File | Role |
| --- | --- |
| `src/portal.*` | Hotspot, DNS catch-all, web page, form handling |
| `src/geocode.*` | Open-Meteo geocoding; `town, ST` prefers a matching state |
| `src/settings.*` | NVS storage; portal values override `beachday_config.h` |
| `renderSetup()` in `src/render.cpp` | The instruction card on the panel |

**Erase all settings** at the bottom of the setup page clears NVS and restarts
into the portal — for handing a board on to someone else.

## Open items after first flash

- Font sizes are chosen from Adafruit GFX's bundled Free Sans Bold set to
  approximate the Liquid's `cqh` values; expect to nudge them.
- Icons are drawn with primitives (circles, thick lines), not bitmaps. They
  are recognisable stand-ins for the SVGs, not copies.
- The panel supports 4-level grey; this build is pure black/white. The
  template's grey "fail" icons and grey footer text render black.
- The onboard SHT4x and RTC are unused. The RTC would let the board skip NTP.
