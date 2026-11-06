# Beach Day

A self-contained e-ink display that tells a household what kind of day it is
and what to put on. It runs on a Seeed reTerminal, talks to Open-Meteo over
Wi-Fi, decides, draws, and goes back to sleep.

No hub, no server, no account, no subscription. Wi-Fi credentials and a
location are the whole setup.

This file explains how the product works. Every command — committing,
publishing an update, flashing over USB, editing the rules — is in
**[README-publish.md](README-publish.md)**.

---

## What you need

| | |
| --- | --- |
| **Board** | Seeed reTerminal **E1001** (7.5" black-and-white ePaper) or **E1002** (7.3" six-colour) |
| **Power** | USB-C, or the internal 2000 mAh battery |
| **Network** | 2.4 GHz Wi-Fi |

Both boards are 800×480 on the same pins, so one firmware drives both — the
layout, fonts and icons are identical and only the ink differs. Each board
remembers which panel it has; flashing it over USB sets that once, and
updates over Wi-Fi keep it. If a board is ever set to the wrong panel (its
screen stays blank), press Left Nav: the setup page has a **Display panel**
section.

| | E1001 | E1002 |
| --- | --- | --- |
| Panel | 7.5" mono, GDEY075T7 (UC8179) | 7.3" six-colour, GDEP073E01 (ED2208) |
| Inks | black, white | black, white, red, green, blue, yellow |
| Also onboard | ESP32-S3, 8 MB PSRAM, 3 buttons, LED, buzzer, RTC, temp/humidity sensor | same |

---

## Setting one up

On first boot the board has no Wi-Fi, so it raises its own hotspot and shows
the instructions on screen:

1. Join the Wi-Fi network **BeachDay-Setup-xxxx** on a phone (the name is on
   the screen).
2. A setup page opens on its own. If it doesn't, browse to `http://192.168.4.1`.
3. Pick a Wi-Fi network, type the password, and type where the beach is: a
   **postal code** (`90401`), a **town** (`Santa Monica`) or **town, state**
   (`Springfield, IL`). Optionally: a short label for the screen, when day and
   evening start, the street-cleaning schedule, and the trash reminder. Tap **Save and restart**.

The board restarts, joins the Wi-Fi, looks the place up, and draws the
forecast. The place it found appears at the top of the left panel — that is
the confirmation. If it couldn't find the place, the screen says so. The
hotspot times out after 10 minutes; the LED blinks fast while it is up.

To change any of it later, press **Left Nav**. The page opens with your current
settings, so change only what you need and save. The saved Wi-Fi password is
never shown; leave the box blank to keep it. **Erase all settings** at the
bottom of the setup page wipes Wi-Fi, location and reminders, for handing a
board on.

---

## The screen

The left panel is the verdict: a day name, an icon and a tagline. The right
column is the detail — the weekday, six stat tiles, four things to wear, and
one closing tip.

A bar across the bottom means act on something:

- **Street cleaning** shows from the start of Evening Mode the evening before
  until the cleaning window ends.
- **The weekly reminder** — by default "Take your trash to the street
  tonight" — shows on **Wednesday evening**, for the whole of Evening Mode.
  The setup page can change the day, the wording, and whether it shows that
  evening or that day.

Street cleaning gets the bar first. On the colour panel street cleaning is
red and the reminder yellow; on the black-and-white one both are black.

### Buttons

| Button | Press | Hold about 3 seconds, until the LED lights |
| --- | --- | --- |
| **Green (Refresh)** | Refresh the forecast now | — |
| **Left Nav** | Open the setup page (Wi-Fi, place, day and evening, panel, parking, trash reminder) | — |
| **Right Nav** | Show the hourly readings behind the screen | Check for a firmware update now |

The **readings screen** answers "why did it decide that?". It lists every
hourly row the rules read — by day, the hour containing sunrise through the
hour containing sunset; in the evening ("This evening's readings"), the hour
containing sunset through 11 PM — with temperature, rain chance, cloud,
wind, AQI and sky, and the figures the rules ran on along the bottom. Its
footer shows the firmware version. Any button returns to the forecast.

The **update check** shows "Checking for updates", then either installs a
new firmware and restarts within a minute, or says why not (already up to
date, battery under 30%, no Wi-Fi).

### Colour (E1002)

Six flat inks, no greys, a 30-second refresh — so colour is used in large
fields with one meaning each:

| Left panel | Outcome | Means |
| --- | --- | --- |
| **yellow** | the beach day (`hot_sunny`) | go to the beach |
| **blue** | every rainy or snowy square | take the wet-weather gear |
| **red** | every other cold or freezing square | it is genuinely cold |

Everything else stays black and white. Stat icons keep their own inks —
yellow sun, blue raindrop, green leaf, red thermometer — because there the
colour *is* the data. On the black-and-white panel, colour accents become a
light dot pattern on large icons and plain line art on small ones.

---

## How it decides the day

Five steps, in order.

### 1. Fetch

One request to Open-Meteo for the hourly forecast — temperature, humidity,
precipitation probability, cloud cover, wind, weather code — plus a second for
hourly air quality, covering today and tomorrow. Nothing is cached between
wakes; every wake fetches fresh.

### 2. Reduce the hours to one set of figures

By day, the hourly rows are aggregated over **that day's own daylight
window**: the whole hour containing sunrise through the whole hour containing
sunset. Sunrise at 6:42 makes the 6am row part of the day; sunset at 7:08 PM
makes the 7pm row the last one. In the evening it is sunset to midnight
instead (see [Evening](#evening)). Either window is fixed: a look at 3 PM
still counts the morning, and a look at 11 PM still counts from sunset, so the
figures always describe the same hours the pinned outcome was decided on.

That window produces one set of figures — `tempMin`, `tempMax`, `swing`,
`rain`, `wind`, `cloud`, `aqi`, `snow`, humidity range, the longest run of
sunny hours, the rain window — which is what the rules actually see.

### 3. Pick the outcome

[`shared/v3/day-outcomes.toml`](shared/v3/day-outcomes.toml) answers two
questions, in order.

**How warm does it get?** The high picks a band:

| Band | High |
| --- | --- |
| Hot | 78° and up |
| Warm | 65° to 77° |
| Cool | 50° to 64° |
| Cold | 32° to 49° |
| Freezing | under 32° |

**What else is going on?** Inside the band, the first of these that holds
decides the extras:

| | Condition | Means |
| --- | --- | --- |
| 1 | Snowy | `snow >= 1`: any hour forecast as snow |
| 2 | Rainy | `rain >= 50`: some hour is 50% or likelier |
| 3 | Windy | `wind >= 20`: what the WIND tile calls "Gusty" |
| 4 | Swingy | `swing >= 20`: cold now, warm later |
| 5 | Very sunny | `cloud <= 15`: near-cloudless all day (days only) |
| 6 | Clear | none of the above: the band's plain day |

That is one outcome per square: five bands by six conditions, less the three
squares too warm to snow — 27 in all, from `hot_rain` to `freezing_clear`.
Each square names its four things to wear, so a 72° rainy day gets a rain
jacket and rain boots, and a 40° rainy day gets a waterproof coat, waterproof
boots and gloves.

Underneath, it is an ordered list where **every condition in a `when` must
pass** and the **first outcome in file order** wins. The grid is written band
by band, conditions in the order above, so the file order *is* that
precedence. The last outcome, `freezing_clear`, has no `when` at all — it is
the catch-all.

The beach day is the hot, very sunny square, `hot_sunny`. A hot day with a
big morning chill is `hot_swing` instead, because a hoodie for the walk is
the more useful thing to say at 7 AM.

### 4. Pick the name

An outcome does not have one name. It has a pool, and every name in it is one
of three kinds:

| Kind | Written as | Means | Example |
| --- | --- | --- | --- |
| **place** | `places = ["Beach"]` | somewhere you normally can't go, that today you should | Beach Day, Ice Rink Day |
| **thing** | `things = ["Kite"]` | what you need for something only this weather allows | Kite Day, Sled Day |
| **wear** | `"*Umbrella"` in `wear` | the most important thing you'll wear for the weather | Umbrella Day |

Places and things are the bare noun; " Day" is added. A wear name is not
written at all: you star the item in the outcome's own wear list, and the day
is named after **whatever that tile says today** — so on a whimsy day when
the starred tile swaps, the day becomes `Duck Umbrella Day`. The left panel
shows the icon of whatever named the day: the tile's, or a place's or
thing's own picture from `[day_icon]` (Kite Day shows a kite), falling back
to the outcome's own `icon`.

One is chosen per day, seeded by the date and the outcome's id, so it **holds
steady all day**, **differs tomorrow**, and **two outcomes don't rotate in
lockstep**. Nothing is random — the board redraws every hour, and anything
random would churn under whoever is reading it.

If a `[[holiday]]` entry matches the date, its label is prefixed:
`Thanksgiving Rain Boots Day`.

### 5. Now and then, whimsy

About one day in ten, one of the four tiles swaps for a sillier take on the
same thing: **Bellbottoms** for light pants, a **Retro trucker hat** for the
sun hat, **Frog rain boots** for rain boots. Every item in the catalogue has
two.

The rule for writing one is that it has to do the original's job. Bellbottoms
are still light pants; a duck umbrella still keeps you dry; a parasol is not
an umbrella. The tile keeps the original's icon, and only one tile swaps, so
the other three stay plain advice. Which day, which tile and which
alternative are all seeded by the date, so it holds all day.

---

## Evening

**By default the screen turns to Evening Mode at sunset and back to Day Mode
at midnight.** The setup page can move either one:

| | Choices (default first) |
| --- | --- |
| **Day Mode start** | Midnight, Sunrise, Sunrise - 30m, Sunrise - 1h, Sunrise - 2h, Sunrise - 3h |
| **Evening Mode start** | Sunset, Sunset - 30m, Sunset + 30m |

These move only the switch, not the hours each mode reads. Day Mode always
reads sunrise to sunset, even before the sun is up. Evening Mode always reads
the hour containing sunset through the 11 PM hour (7pm to 11pm, say), the same
at 8 PM as at 11:50 PM; nothing after midnight counts. The hour containing
sunset counts for both. The heading reads `Wednesday evening`, the name ends
in Evening (`Rain Jacket Evening`), the wear heading reads `WHAT TO WEAR OUT
THIS EVENING`, and the footer shows tomorrow's sunrise.

A Day Mode start later than midnight leaves the evening on screen until then,
still reading sunset to midnight: at 2 AM Friday it says `Thursday evening`.

Evening runs on the same grid, with three differences, all set in the rules
file:

- **No sunny squares.** They carry `evening = 0`, so a clear evening falls
  through to its band's plain outcome rather than recommending sunglasses.
- **Named after what you wear.** Places and things are for daylight, so an
  evening's name comes from a starred wear item. An outcome with nothing
  starred falls back to its places and things.
- **Reworded where daylight was assumed.** An outcome's `evening = { tagline,
  also }` replaces lines like "comes off by lunch". The sun tile becomes
  **SKY** (Clear, Partly or Cloudy, with "Starry" underneath), the temperature
  word reads the low before midnight, and `[screen.evening]` rewords the
  subline and footer.

---

## When the screen can change

Every wake runs the whole pipeline again, so the numbers on the tiles are
always fresh. What is held is the outcome.

**The day is pinned at sunrise.** From Day Mode's start until then (midnight
to sunrise, by default) the screen follows the forecast as it changes. From
sunrise on, the tiles, the temperatures and the rain window stay current, but the
outcome, its name and its four things to wear hold. The pin lives in the
board's flash, so a reboot or a flat battery mid-morning does not re-decide
the day. The forecast moves in small wobbles — the high drifts a degree or
two between model runs — and a day sitting at 77° or 78° would otherwise flip
between Warm and Hot under whoever is reading it.

**Only rain or snow can break the pin.** Outcomes marked `breaks_pin = true`
— every rainy and snowy square — replace a pinned day the moment the forecast
reaches them, because "take the raincoat after all" is worth interrupting
for. Once broken, the pin is the wet outcome: drying out later does not
un-say it.

**The evening has its own pin**, set at its first look and broken the same
way. It is kept apart from the day's.

Measured on 60 days of real archived forecasts (`tools/replay.sh`, with
`--no-pin` for the comparison). These figures are from before 0.14.0, when
the day was pinned at 3 AM; `tools/replay.sh` now replays the sunrise pin and
needs a fresh run:

| | Without the pin | With it |
| --- | --- | --- |
| Days with one outcome from 6 AM on | 37 of 60 | **59 of 60** |
| Changes after 6 AM | 29 | **1** — a real afternoon rain |

The cost is that a pinned morning call cannot correct itself: it matched the
day as it actually turned out about two times in three, the same as the
6 AM screen always did.

**A holiday prefix appears and disappears on its own date.**

---

## Wakes and battery

The board sleeps between wakes and only wakes when something could change:

| Period | Interval |
| --- | --- |
| Daylight (sunrise until Evening Mode starts) | every **60 min** |
| Dark (Evening Mode, and Day Mode before sunrise) | every **240 min** |

Plus extra wakes at the start of the day, at sunrise (when the day is
pinned), at the start of the evening (which is also when a street-cleaning
alert for the next morning goes up), and at the end of any street-cleaning
window. Green (Refresh) wakes it at any time.

With the defaults that is roughly 19 wakes a day of about 12 seconds each —
call it 7–9 mAh a day from a 2000 mAh battery, so **months** on a charge.

**When the network is down,** the screen keeps its last good image (e-paper
needs no power to hold it), restamped `OFFLINE since 2:05 PM`. The board
retries every 10 minutes, backing off to hourly after six misses. If only the
air-quality request fails, the forecast still draws and AQI reads 0.

---

## Updates

The board checks once a day for new firmware, and the long press of Right
Nav checks right away.

- **Rules** (`shared/v3/day-outcomes.toml`) are built into the firmware, so a
  rules change goes out as a firmware release like any other. The tests run
  the file through the engine before every release, and the board checks it
  again at boot.
- **Firmware** comes from the latest GitHub release. The board compares the
  version, downloads, checks size and checksum, writes to its spare slot, and
  only then switches over — a power cut mid-update leaves the old firmware
  running.
- **Signed.** Every release is signed with a key only the publisher holds,
  and the board installs nothing that isn't signed by it. Whoever sits
  between the board and GitHub can't slip in other firmware. It never starts below 30% battery, and it always draws the screen
  first, so a failed update never leaves a blank or stale panel.
- **Safe mode.** If a new firmware crashes three boots in a row, the board
  stops doing anything but looking for newer firmware, shows "Updating —
  looking for new software", and retries every 30 minutes. Publishing a fix
  repairs it remotely.

Known limits: there is no automatic rollback for firmware that runs but draws
the wrong thing, so check a release on a bench board first. Downloads are
encrypted but don't verify the server's certificate — a deliberate trade so a
board in a drawer doesn't break when a certificate rotates. The signature
covers firmware; the forecast and air-quality data are not covered, and
someone on the network could feed a board a false forecast or an older signed
release.

---

## Where things live

```
README.md                      this file: how it works
README-publish.md              every command: commit, publish, flash, edit the rules
shared/v3/day-outcomes.toml    the rules: outcomes, names, wear, whimsy, evening, tiles
reterminal-e1001/              firmware for both boards (E1001 and E1002)
docs-design/                   design notes, icon sheet, layout previews
hubitat-trmnl/, shared/*.json  the retired v1 (Hubitat + TRMNL) system
```

The v1 setup guide is in git history (`git show v0.12.0-source:README-v1.md`).
