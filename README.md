# Beach Day

A self-contained e-ink display that tells a household what kind of day it is
and what to put on. It runs on a Seeed reTerminal, talks to Open-Meteo over
Wi-Fi, decides, draws, and goes back to sleep for an hour.

No hub, no server, no account, no subscription. Wi-Fi credentials and a
location are the whole setup.

> An earlier version of this project needed a Hubitat hub and a TRMNL
> subscription. It still lives in the tree and still works — see
> [README-v1.md](README-v1.md). Everything below describes the reTerminal
> version only.

---

## What you need

| | |
| --- | --- |
| **Board** | Seeed reTerminal **E1001** (7.5" mono ePaper) or **E1002** (7.3" six-colour) |
| **Power** | USB-C, or the internal battery |
| **Network** | 2.4 GHz Wi-Fi |

Both boards are 800×480 and share a pin map, so one codebase drives both — the
layout, fonts and icons are identical and only the ink differs. They live
together in [reterminal-e1001/](reterminal-e1001/), which is a slightly
unfortunate folder name by now.

On first boot the board has no credentials, so it raises its own Wi-Fi hotspot
and shows the join instructions on screen. You connect a phone, pick a network,
type a password and type a place name. It reboots, resolves the place to
coordinates, fetches, and draws. To change any of it later, press
**Left Nav**.

Build and flash instructions, the OTA update server layout, and the design
notes are in **[reterminal-e1001/README.md](reterminal-e1001/README.md)**.

---

## The screen

The left panel is the verdict: a day name, an icon and a tagline. The right
column is the detail — the weekday, four stat tiles, four things to wear, and
one closing tip. A bar across the bottom means act on something today: street
cleaning, or the weekly reminder — by default, "Take your trash to the street
tonight" all day Wednesday. Street cleaning gets the bar first.

Three buttons, one press each:

| Button | Does |
| --- | --- |
| **Green (Refresh)** | Refresh the forecast now |
| **Left Nav** | Open the setup portal (Wi-Fi, place, parking, trash reminder) |
| **Right Nav** | Show the hourly readings behind the dashboard |

**To force a firmware and rules update:** hold Right Nav, then press Green,
and keep holding Right Nav until the screen starts to refresh.

The readings screen is the "why did it decide that?" answer: every hourly row
inside the daylight window with the derived figures along the bottom. Its
footer also shows the firmware version the board is running.

---

## How it decides the day

Five steps, in order.

### 1. Fetch

One request to Open-Meteo for the hourly forecast — temperature, humidity,
precipitation probability, cloud cover, wind, weather code — plus a second for
hourly air quality, covering today and tomorrow. Nothing is cached between
wakes; every wake fetches fresh.

### 2. Reduce the hours to one day

The hourly rows are aggregated over **that day's own daylight window**: the
whole hour containing sunrise through the whole hour containing sunset, both
ends included. Sunrise at 6:42 makes the 6am row part of the day; sunset at
7:08 PM makes the 7pm row the last one.

That window produces one set of figures — `tempMin`, `tempMax`, `swing`,
`rain`, `wind`, `cloud`, `aqi`, `snow`, humidity range, the longest run of
sunny hours, the rain window — which is what the rules actually see. Today and tomorrow are
computed identically.

### 3. Pick the outcome

[`shared/v3/day-outcomes.toml`](shared/v3/day-outcomes.toml) answers two
questions, in order.

**How warm does it get?** The day's high picks a band:

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
| 1 | Snowy | `snow >= 1`: any daylight hour forecast as snow |
| 2 | Rainy | `rain >= 50`: some hour is 50% or likelier |
| 3 | Windy | `wind >= 20`: what the WIND tile calls "Gusty" |
| 4 | Swingy | `swing >= 20`: cold now, warm later |
| 5 | Very sunny | `cloud <= 15`: near-cloudless all day |
| 6 | Clear | none of the above: the band's plain day |

That is one outcome per square: five bands by six conditions, less the three
squares too warm to snow — 27 in all, from `hot_rain` to `freezing_clear`.
Each square names its four things to wear, so a 72° rainy day gets a rain
jacket and rain boots, and a 40° rainy day gets a waterproof coat, waterproof
boots and gloves.

Underneath, it is still the same engine: an ordered list where **every
condition in a `when` must pass** and the **first outcome in file order**
wins. The grid is just written band by band, conditions in the order above,
so the file order *is* that precedence. The very last outcome,
`freezing_clear`, has no `when` at all — it is the catch-all, and since every
band above it ends on its own "clear" square, it can only ever be reached by a
freezing day with nothing else going on.

The beach day is the hot, very sunny square, `hot_sunny`. A hot day with a
big morning chill is `hot_swing` instead, because a hoodie for the walk is
the more useful thing to say at 7 AM.

### 4. Pick the name

An outcome does not have one name. It has a pool, and every name in it is one
of three kinds — each written in its own place:

| Kind | Written as | Means | Example |
| --- | --- | --- | --- |
| **place** | `places = ["Beach"]` | somewhere you normally can't go, that today you should | Beach Day, Ice Rink Day |
| **thing** | `things = ["Kite"]` | what you need for something only this weather allows | Kite Day, Sled Day |
| **wear** | `"*Umbrella"` in `wear` | the most important thing you'll wear for the weather | Umbrella Day |

Places and things are the bare noun; " Day" is added. A wear name is not
written at all: you star the item in the outcome's own wear list, and the day
is named after **whatever that tile says today**. So the name and the tiles
can never disagree — and on a whimsy day when the starred tile is the one that
swaps, the day becomes `Duck Umbrella Day`. The hero panel shows that tile's
icon too; the outcome's own `icon` is used for its places and things.

One is chosen per day:

```
index = (epochDay + hash(outcome id)) % pool size
```

That is seeded by the date and the outcome's id, so it **holds steady all day**,
**differs tomorrow**, and **two outcomes don't rotate in lockstep**. Nothing is
random — the board redraws every hour, and anything random would churn under
whoever is reading it.

So `hot_sunny` — places `Beach` and `Pool`, and a starred `Sun hat` — rotates
through `Beach Day`, `Pool Day` and `Sun Hat Day`. A square with one starred
item and nothing else, like the morning-chill `Hoodie Day`, never rotates.

The clothing catalogue is keyed the same way the tiles read — `"Rain boots" =
{ icon = "rain_boot" }` — and an outcome lists its four items by those names.

If a board ever shows a name this file could not produce, it is not running
this file. The board prefers the copy in its own filesystem over the compiled-in
fallback, so check what it actually fetched before assuming the rules are wrong.

If a `[[holiday]]` entry matches the date, its label is prefixed:
`Thanksgiving Rain Boots Day`.

### 5. Now and then, whimsy

About one day in three, one of the four tiles swaps for a sillier take on the
same thing: **Bellbottoms** for light pants, a **Retro trucker hat** for the
sun hat, **Frog rain boots** for rain boots. Every item in the catalogue has
two.

The rule for writing one is that it has to do the original's job. Bellbottoms
are still light pants; a duck umbrella still keeps you dry; a parasol is not
an umbrella. The tile keeps the original's icon, so the picture stays true
either way, and only one tile swaps, so the other three stay plain advice. If
the tile that swaps is the one naming the day, the name follows it.

Like the name, it is seeded by the date — which day, which tile, which
alternative — so it holds all day. `[whimsy] one_in = 3` sets the rate;
delete the table to turn it off.

---

## When it re-evaluates

Every wake runs the whole pipeline again — fetch, reduce, pick, draw — so the
numbers on the tiles are always fresh. The one thing kept between wakes is the
day's pinned outcome; see below.

**Scheduled wakes:**

| Period | Interval |
| --- | --- |
| From one hour before sunrise until sunset | every **60 min** |
| Overnight | every **240 min** |

**Extra wakes at the moments the screen would change anyway:** sunrise, sunset,
one hour after sunset, the next sunrise, and the end of any enabled
street-cleaning window.

**On failure:** it retries after **10 min**, and after six consecutive failures
backs off to 60 min. The last good screen stays up, restamped `OFFLINE since
…`, so the display is never blank and never silently stale.

**On a button:** Green (Refresh) forces an immediate refresh.

**Separately,** the board checks for a new rules file and new firmware every
**24 hours** each. Both are validated before being accepted — a rules file that
does not parse or fails the engine's checks is rejected and the old one is
kept, so a bad publish cannot take the display down.

---

## When the screen can change

This is the part that surprises people, so it is worth being precise.

**The outcome is pinned for the day at the first look from an hour before
sunrise.** After that, every wake still fetches and redraws — the tiles, the
temperatures, the rain window all stay current — but the outcome, its name
and its four things to wear hold. The pin lives in the board's flash, so a
reboot or a flat battery mid-morning does not re-decide the day.

The reason is that the forecast moves in small wobbles, not just real
surprises: the high drifts a degree or two between model runs, and a day
sitting at 77° or 78° would flip between Warm and Hot under whoever is
reading it.

**Only rain or snow can break the pin.** Outcomes marked `breaks_pin = true`
— every rainy and snowy square — replace a pinned day the moment the forecast
reaches them, because "take the raincoat after all" is worth interrupting
for. Once broken, the pin is the wet outcome: drying out later does not
un-say it.

**Before the pin, the screen is live.** The evening preview of tomorrow and
the small hours of the night are never pinned; tomorrow decides itself in the
morning, on the freshest forecast.

Measured on 60 days of real archived forecasts (`tools/replay.sh` in
[reterminal-e1001/](reterminal-e1001/), with `--no-pin` for the comparison):

| | Without the pin | With it |
| --- | --- | --- |
| Days with one outcome from 6 AM on | 36 of 60 | **59 of 60** |
| Changes after 6 AM | 30 | **1** — a real afternoon rain |

The cost is that a pinned morning call cannot correct itself: it matched the
day as it actually turned out 39 times in 60, the same as the 6 AM screen
always did, where the unpinned screen reached 50 by evening by changing its
mind.

**The name cannot change within a day — unless rain or snow breaks the pin.**
The name is seeded by the date, so it holds for the whole calendar day. But
the pool belongs to the outcome, so if an afternoon rain moves the day from
`warm_clear` to `warm_rain`, the name changes with it. Whimsy works the same way: pinned for
the day, but it rides on whichever four items the outcome picked.

**One hour after sunset, the screen flips to tomorrow.** It evaluates tomorrow's
daylight window instead of today's, and the heading reads `WHAT TO WEAR
TOMORROW`. The date used for the name is also tomorrow's, so the name you see
on Wednesday evening is the one Thursday will keep all day — and a Christmas
Eve evening already says Christmas.

Today's screen therefore stays up through sunset and for an hour after it,
which is the window in which someone is most likely to glance at it and still
be acting on today.

**It flips back at midnight,** not at dawn. Showing "Thursday" at 5 AM on a
Thursday is correct.

**A holiday prefix appears and disappears on its own date.**

---

## The rules are a file, not code

[`shared/v3/day-outcomes.toml`](shared/v3/day-outcomes.toml) is read at run
time. The thresholds, the outcomes, the names, the variants, the holidays, the
wear items and the four stat tiles are all authored there — in TOML, by hand,
with no build step.

A copy is compiled into the firmware as a fallback, but the board prefers the
one in its filesystem and will fetch a newer one. **Changing what the display
says does not require reflashing.**

To see a change before it reaches a board:

```bash
cd reterminal-e1001
tools/preview.sh
```

That compiles the real rendering code against a panel that draws into memory
and writes PNGs to `docs-design/preview/` — same fonts, same icons, same
dithering, same layout decisions as the hardware.

---

## Where things live

```
shared/v3/day-outcomes.toml    the rules: outcomes, names, variants, holidays, tiles
reterminal-e1001/              firmware for both boards (E1001 mono, E1002 colour)
  src/                         fetch, decide, draw, sleep, OTA, setup portal
  lib/dayrules/                the engine: parse + validate TOML, derive, evaluate
  test/                        host tests — pio test -e native
  tools/preview.sh             render the layout to PNGs on a Mac
  tools/replay.sh              replay 60 days of real forecasts, count the changes
hubitat-trmnl/                 the v1 system (see README-v1.md)
```

The deep documentation — build, flash, OTA hosting, panel abstraction,
typography, colour strategy, layout review checklist — is in
**[reterminal-e1001/README.md](reterminal-e1001/README.md)**.
