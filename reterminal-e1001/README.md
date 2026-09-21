# Beach Day — reTerminal E1001

A self-contained Beach Day display. The board fetches Open-Meteo directly over
Wi-Fi, evaluates the rules in [../shared/rules.json](../shared/rules.json),
renders to the ePaper panel, and deep-sleeps until the next update.

**No Hubitat hub. No TRMNL account or server. No always-on machine anywhere.**

> Status (v3-redesign branch): firmware 0.4.0. Drives both the E1001 (mono)
> and the E1002 (6-colour); same layout, same 800x480, same pins. Colour has
> been built but not yet seen on the E1002 panel.

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
./flash.sh                  # dev build, E1001: stays awake, buttons cycle outcomes
./flash.sh release          # real build: renders once, then deep-sleeps
./flash.sh dev e1002        # the 6-colour panel
./flash.sh release e1002
./flash.sh gift e1002       # a board for someone else - see below
./flash.sh rules            # push only the rules JSON
./flash.sh test             # rules tests on your Mac, no board needed
```

### Flashing a batch for other people

`./flash.sh gift <panel>` is the one to use for boards that leave the house.
A `release` build compiles in whatever is in your `beachday_config.h`, so a
board flashed that way arrives with **your** Wi-Fi and coordinates and never
opens the setup portal. The gift build compiles in none of it: no network, no
location, no label. It also erases NVS first, so a board that was previously
set up or tested starts genuinely blank, and it skips the serial monitor so
you can unplug and move to the next one.

```sh
./flash.sh gift e1002     # plug in, run, unplug, repeat
./flash.sh gift e1001
```

Each board then starts as its own `BeachDay-Setup-xxxx` hotspot and the person
who receives it enters their own Wi-Fi and town.

## Two panels, one layout

| | E1001 | E1002 |
| --- | --- | --- |
| Panel | 7.5" mono, GDEY075T7 (UC8179) | 7.3" 6-colour ACeP, GDEP073E01 (ED2208) |
| Inks | black, white | black, white, red, green, blue, yellow — **no orange** |
| Resolution / pins | 800×480, SCK 7 MOSI 9 CS 10 DC 11 RST 12 BUSY 13 | identical |
| Build env | `reterminal_e1001`, `dev` | `reterminal_e1002`, `dev_e1002` |

Because both are 800×480 on the same pins, **the layout, fonts and icons are
shared verbatim**. Only two files differ: `src/panel_e1002.cpp` (the driver)
and the role table it feeds.

### How colour is used

Six pigment inks, no grey, no tints, no blending, and a 30-second refresh. That
rewards **large flat fields** and punishes small coloured detail, so the colour
design is deliberately narrow:

**The hero panel takes one flat ink, chosen by the outcome.** It is a third of
the screen, so the verdict is readable from across the room before a word is.
Only three outcomes are coloured, which is what keeps each one learnable:

| | | |
| --- | --- | --- |
| **yellow** | Beach Day | go to the beach |
| **blue** | Rain Boots Day | take rain gear |
| **red** | Big Coat Day | it is genuinely cold |

Everything else stays black or white. Seven coloured outcomes would be
confetti; three are a vocabulary. Type colour follows automatically — black on
yellow, white on the darker inks.

**The parking bar is red**, because a solid bar already means "act on this" and
red says it in one glance.

**Stat icons keep their own inks** — yellow sun, blue raindrop, green leaf, red
thermometer — because there the colour *is* the data.

Everything else is black on white. Two competing colour zones would fight, and
the right-hand column is for reading, not for glancing.

Everything draws in *roles* — ink, paper, caption, band, and the accent fills
`Sun` / `Water` / `Leaf` / `Warm`. `src/paint.cpp` is the only place a role
becomes a colour:

- **E1001** has two inks. Accents become a 25% dot pattern and only above
  40 px — inside a 22 px icon a dither is noise, so small icons draw as plain
  line art. The hero tint is ignored entirely and the panel uses the outcome's
  `heroPanel: light|dark`, so **the monochrome design is unchanged** by any of
  the colour work: nothing on mono is ever asked to fake an ink.
- **E1002** has six, so accents are real ink at every size, and the hero can
  carry a flat tint.

On a *coloured* hero the icon draws as a clean silhouette in the foreground
colour rather than its own inks — a yellow sun on a yellow panel would vanish,
and four inks in one corner would clash.

Neither panel has a light grey, so the stat strip's `#F2F2F2` is an outline on
both. A dot field there destroyed the type sitting on it, and the colour panel
has no grey ink to fill it with.

The binaries are **not** interchangeable — an E1001 image on an E1002 runs
perfectly and never touches the screen, because it is talking to a controller
that isn't there. Each panel gets its own OTA tag (`PANEL=e1002
tools/release.sh --publish`) and each board's `CFG_OTA_MANIFEST_URL` must point
at its own.

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
| `n` | KEY2 (left) | Step through the outcomes with preset numbers, run through the real rules |
| `p` | KEY1 (middle) | Toggle the parking-alert bar |
| `l` | KEY0 (right) | Back to the real forecast fetched at boot |
| `d` | — | Dump the current view as text |
| `R` | — | Reboot and re-fetch |
| `?` | — | Reprint the key list |

The cycling is the point: today's real weather only ever produces one
outcome, and Rain Boots Day might not turn up for months. The green LED
blinks every 2 s to show the board is awake.

The live forecast refreshes every `CFG_DEV_REFRESH_MINUTES` (default 20) by
restarting, which re-runs the normal boot path. It never interrupts you while a
demo state is on screen — press `l` to go back to the live view and the timer
applies again. `R` refreshes immediately.

### If the panel has stopped updating

E-paper holds its last image with **no power at all**, so a frozen display, a
crashed board and a flat battery all look identical. Work down this list:

1. **Which build is it running?** Open the serial monitor and press `R`. The
   banner says `DEV MODE` or the release build prints
   `[beach] firmware x.y.z, boot N`. A dev build older than 0.2.1 never
   re-fetched at all — it drew once at boot and stayed there.
2. **Is it powered?** Plug in USB. If the screen was showing a stale image
   because the battery ran down, it will boot and redraw within ~20 seconds.
3. **Check the header.** The live view prints `Updated 2:05 PM` top-right, and
   `OFFLINE since …` when a fetch has been failing. The latter means Wi-Fi or
   the API, not the board.
4. **Watch a full cycle on serial.** `R` restarts; you should see Wi-Fi connect,
   the window/sun line, the verdict, then either `deep sleep for N s` (release)
   or the dev banner.

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
3. **Text fit.** The long strings are the risk: the sun-hat "also grab" lines,
   `PEEL THEM OFF LATER`, a three-line `RAIN / BOOTS / DAY!`, and the footer
   with a long suffix. Headline and footer shrink to fit; watch for it.
4. **Small type.** The 10–12 px labels are the one place on-device
   rasterising can look rougher than the mock. If they do, nudge sizes in the
   type ramp at the top of `render.cpp`.
5. **Icons.** Compare against `docs-design/icons-preview.png`; the leaf fill
   and umbrella scallops are already on the list.
6. **The band.** The stat strip is a 12.5% dot field standing in for `#F2F2F2`.
   If it fights the text, switch it to an outline in `paint.cpp`.

When the layout looks right, `./flash.sh release` and let it sleep.

## How it works

```
deep sleep ──wake──▶ Wi-Fi ──▶ NTP ──▶ Open-Meteo forecast + air quality
                                              │
                                              ▼
                                  derive   (lib/dayrules/derive.cpp)
                                  that day's sunrise→sunset hours: temp
                                  min/max/swing, rain, wind, AQI, humidity,
                                  cloud average, first clear hour
                                              │
                                              ▼
                                  rules     (lib/dayrules/dayspec.cpp)
                                  shared/v3/day-outcomes.json, loaded at
                                  run time: one outcome, four wear tiles,
                                  the also-grab line, five stats, all copy
                                              │
                                              ▼
                                  render 800×480 ──▶ hibernate ──▶ deep sleep
```

### The rules are data

[shared/v3/day-outcomes.json](../shared/v3/day-outcomes.json) is the whole
personality of the display: the outcomes and their order, every threshold,
every line of copy, which icon goes in which tile. The firmware **interprets**
it; nothing about a Rain Boots Day is compiled in. Three places it can come
from, in order:

1. **LittleFS** on the board — `./flash.sh rules` (or any full flash) puts the
   current file there. Edit the JSON, run that, done. No rebuild.
2. **Fetched** — once a day (`CFG_RULES_CHECK_HOURS`) the board pulls
   `CFG_RULES_URL`. A fetched file replaces the stored one only after it
   parses and passes the engine's checks (four wear tiles per outcome, a
   catch-all last), so a bad edit can't take a friend's display down.
3. **Embedded** — a copy baked in at build time, the fallback if both above
   are missing.

`./flash.sh test` replays the JSON through the engine on your Mac: outcome
precedence (wet beats cold beats hot), the `byFailedTest` copy for Sun Hat Day,
the near-threshold line for T-shirt Day, stat bands and the `swingy` override.

### Fonts and icons are vectors

The design's typefaces — Fraunces Black and Nunito — ship as real TTFs
(instanced to the exact weights and subset to the characters the copy can use,
56 KB total; `tools/prep_fonts.py`) and are rasterised on the device by
`stb_truetype` at whatever size the layout asks for. Icons are small command
lists in a 48-unit box (`tools/gen_icons.py` is the source of truth and
renders an aliased preview to `docs-design/icons-preview.png`), interpreted at
100 px for the hero, 44 px in tiles, 22 px in the stat strip. One asset per
face and per icon, for any panel size.

### Colour is a role

Everything draws in roles — ink, paper, caption, band, and the accent fills
SUN / WATER / LEAF / WARM that icons carry — and [src/paint.cpp](src/paint.cpp)
is the only place a role becomes a pixel. On this mono panel accents are a 25%
dot pattern and the stat band is 12.5%. A reTerminal E1002 gets a second table
mapping the same roles to its six inks; the layout, fonts and icons don't
change.

### Files

```
platformio.ini            board, LittleFS, pre-build hook, host test env
src/
  main.cpp                wake → fetch → derive → rules → draw → sleep
  render.*                the 800×480 layout (hero panel, tiles, stat strip, footer pill)
  paint.*                 roles → pixels; the mono table lives here
  text.*                  stb_truetype text: width, draw, wrap
  icons.* icons_data.h    vector icon interpreter + GENERATED command tables
  fontdata.h              GENERATED: the three TTFs as byte arrays
  specstore.*             rules from LittleFS / fetch / embedded, with validation
  spec_embedded.h         GENERATED at build time from shared/v3/day-outcomes.json
  weather.*               Open-Meteo → day::Raw (temp, humidity, rain, code, cloud, wind, AQI)
  settings.* portal.* geocode.* ota.* power.* net.* devmode.* pins.h
lib/dayrules/             the engine (dayspec.*) and derivation (derive.*) — no Arduino
lib/beachrules/           parking alert + WMO table, shared with v2
lib/stb/                  stb_truetype.h (public domain)
test/test_dayrules/       host tests over the real JSON
tools/
  gen_icons.py            icon vectors → icons_data.h + preview
  prep_fonts.py           TTF instance + subset → fontdata.h    (fetch_fonts.sh gets sources)
  pio_prebuild.py         copies the rules to data/ and embeds them
  release.sh              OTA publish
```

## Changing the rules

Edit [shared/v3/day-outcomes.json](../shared/v3/day-outcomes.json), then:

```sh
./flash.sh test      # engine still agrees with the file
./flash.sh rules     # push just the JSON to the board on your desk
git push             # every other board picks it up within a day
```

Adding an outcome means adding an entry with exactly four `wear` items and an
icon name that exists (see `icons` in the JSON; unknown names draw a crossed
box). New icon → add a few lines to `tools/gen_icons.py`, rerun it, rebuild.

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

### The first flash has to be over USB

OTA cannot bootstrap itself. A board only looks for updates if the firmware
already on it contains the OTA code, so any board flashed before that existed
has to be connected once:

```sh
./flash.sh release
```

After that it stays current over Wi-Fi. Same applies to any change to the OTA
mechanism itself, the manifest URL, or the Wi-Fi handling — if the running
firmware cannot fetch, only a cable fixes it.

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
