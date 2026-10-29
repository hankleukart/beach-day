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
coordinates, fetches, and draws. To change any of it later, press the **middle**
button.

Build and flash instructions, the OTA update server layout, and the design
notes are in **[reterminal-e1001/README.md](reterminal-e1001/README.md)**.

---

## The screen

The left panel is the verdict: a day name, an icon and a tagline. The right
column is the detail — the weekday, four stat tiles, four things to wear, and
one closing tip. A black bar across the bottom means street cleaning.

Three buttons, one press each:

| Button | Does |
| --- | --- |
| **Right** | Refresh the forecast now |
| **Middle** | Open the setup portal (Wi-Fi, place, parking) |
| **Left** | Show the hourly readings behind the dashboard |

The readings screen is the "why did it decide that?" answer: every hourly row
inside the daylight window with the derived figures along the bottom.

---

## How it decides the day

Four steps, in order.

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
`rain`, `wind`, `cloud`, `aqi`, humidity range, the longest run of sunny hours,
the rain window — which is what the rules actually see. Today and tomorrow are
computed identically.

### 3. Pick the outcome

[`shared/v3/day-outcomes.toml`](shared/v3/day-outcomes.toml) holds an ordered
list of outcomes. Each has a `when` list of conditions. **Every condition in a
`when` must pass**, and the **first outcome in file order** that passes wins.
Order is the precedence — it is not scored, and nothing is weighted.

As the file stands today:

| # | Outcome | Passes when |
| --- | --- | --- |
| 1 | `rain_day` | `rain >= 50` |
| 2 | `big_coat_day` | `tempMax < 52` |
| 3 | `beach_day` | `tempMax >= 78` **and** `rain <= 20` **and** `wind <= 15` **and** `aqi < 100` **and** `cloud <= 35` |
| 4 | `sun_hat_day` | `tempMax >= 78` |
| 5 | `layers_day` | `swing >= 22` |
| 6 | `tshirt_day` | `tempMax >= 68` |
| 7 | `jacket_day` | *(no conditions — the catch-all)* |

The order is doing real work. Rain and cold sit above Beach Day, so a hot
rainy day is a Rain Day, not a near-miss Beach Day. Beach Day sits above Sun
Hat Day, so Sun Hat Day is "hot, but something disqualified the beach".

The last outcome has no `when` at all, which makes it the catch-all. **Any day
that matches nothing else lands on `jacket_day`** — mild, dry, unremarkable.

### 4. Pick the name

An outcome does not have one name. It has a pool:

- each string in its `names` list, and
- each `[[outcome.variant]]` block, which can also override the icon, tagline,
  colour, the four wear items and the closing tip.

One is chosen per day:

```
index = (epochDay + hash(outcome id)) % pool size
```

That is seeded by the date and the outcome's id, so it **holds steady all day**,
**differs tomorrow**, and **two outcomes don't rotate in lockstep**. Nothing is
random — the board redraws every hour, and anything random would churn under
whoever is reading it.

So `jacket_day` has a pool of five: the names `Jacket Day`, `In Between Day`,
`Regular Day`, plus the variants `Long Sleeves Day` and `Sneakers Day`. A
screen reading **"Regular Day"** is `jacket_day` on one of its five days — it is
not a fallback or an error.

Finally, if a `[[holiday]]` entry matches the date, its label is prefixed:
`Thanksgiving Rain Boots Day`.

---

## When it re-evaluates

Every wake runs the whole pipeline again — fetch, reduce, pick, draw. There is
no partial update and no cached verdict.

**Scheduled wakes:**

| Period | Interval |
| --- | --- |
| Between sunrise − 1h and sunset | every **60 min** |
| Overnight | every **240 min** |

**Extra wakes at the moments the screen would change anyway:** sunrise,
sunset − 1h, sunset, the next sunrise, and the end of any enabled street-cleaning
window.

**On failure:** it retries after **10 min**, and after six consecutive failures
backs off to 60 min. The last good screen stays up, restamped `OFFLINE since
…`, so the display is never blank and never silently stale.

**On a button:** the right button forces an immediate refresh.

**Separately,** the board checks for a new rules file and new firmware every
**24 hours** each. Both are validated before being accepted — a rules file that
does not parse or fails the engine's checks is rejected and the old one is
kept, so a bad publish cannot take the display down.

---

## When the screen can change

This is the part that surprises people, so it is worth being precise.

**The outcome can change at any wake.** It is a function of the forecast, and
the forecast moves. A morning that evaluated to `tshirt_day` can be `rain_day`
by noon. This is working as intended.

**The name cannot change within a day — unless the outcome does.** The name is
seeded by the date, so it is pinned for the whole calendar day. But the pool
belongs to the outcome, so if the forecast pushes the day from `tshirt_day` to
`rain_day`, the name changes with it.

**At sunset − 1h the screen flips to tomorrow.** It evaluates tomorrow's
daylight window instead of today's, and the heading reads `WHAT TO WEAR
TOMORROW`. The date used for the name is also tomorrow's, so the name you see
on Wednesday evening is the one Thursday will keep all day — and a Christmas
Eve evening already says Christmas.

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
hubitat-trmnl/                 the v1 system (see README-v1.md)
```

The deep documentation — build, flash, OTA hosting, panel abstraction,
typography, colour strategy, layout review checklist — is in
**[reterminal-e1001/README.md](reterminal-e1001/README.md)**.
