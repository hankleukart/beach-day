# Shared Rules

> **On the `v3-redesign` branch.** The reTerminal now runs
> [v3/day-outcomes.json](v3/day-outcomes.json) — a different schema, loaded at
> run time. `rules.json` below is v2 and still describes `hubitat-trmnl/`, which
> is frozen. See [docs-design/v3-notes.md](../docs-design/v3-notes.md).

This folder is the source of truth for **what Beach Day decides**. Each hardware
target implements these rules in its own language; none of them should invent
thresholds of their own.

| File | Purpose |
| --- | --- |
| [rules.json](rules.json) | Thresholds, the active-hours window, the state priority table, and the parking-alert schedule. |
| [wmo-codes.json](wmo-codes.json) | WMO weather code → display text, as returned by Open-Meteo. |
| [fixtures/rules-cases.json](fixtures/rules-cases.json) | Test vectors: post-aggregation inputs with hand-derived expected verdicts, plus parking-window and aggregation cases. The firmware's host tests replay every one. |
| [sample_data.json](sample_data.json) | Legacy TRMNL preview payloads (the pre-Open-Meteo merge-variable names). Useful for the HTML previews, not a rules fixture. |

## The contract

Every implementation takes the same inputs and must produce the same verdict:

**Inputs** — daily high/low temp (°F), daily max wind (mph), UV index, and then,
aggregated over **that day's own sunrise→sunset hours**: max precipitation
probability (%), US AQI min/max, humidity min/max, the mode weather code, and
the hour-by-hour rows that feed the sun scan. Plus sunrise/sunset themselves.

There is **one** window and it is used by every time-narrowed aggregate. Today
and tomorrow are computed identically, each with its own sun times. The exact
derivations are in `rules.json → inputAggregation`.

**Outputs** — one state id from `rules.json → states.priority`, the six
individual condition pass/fail flags, the sun scan (sunny-hour count, first
sunny hour, clearing time), and the parking alert (active + text).

## Rule of thumb for changes

1. Edit [rules.json](rules.json) first.
2. Add or adjust a case in [fixtures/rules-cases.json](fixtures/rules-cases.json) that covers the change.
3. `cd reterminal-e1001 && python3 tools/gen.py && pio test -e native` — the firmware constants and test fixtures are generated from the two files above, so this both applies the change and checks it.
4. Edit [beach_day.liquid](../hubitat-trmnl/beach_day.liquid) (and the integrator app if aggregation changed) by hand. TRMNL renders the template on their servers; nothing can generate it for you.

## Where each rule is implemented

| Rule | Hubitat + TRMNL | reTerminal E1001 |
| --- | --- | --- |
| Thresholds | [beach_day.liquid](../hubitat-trmnl/beach_day.liquid) (`cond_*` assigns, hand-copied) | [rules_config.h](../reterminal-e1001/lib/beachrules/rules_config.h) (generated) |
| Condition evaluation | [beach_day.liquid](../hubitat-trmnl/beach_day.liquid) (`cond_temp` … `cond_time`) | [beachrules.cpp](../reterminal-e1001/lib/beachrules/beachrules.cpp) `evaluate()` |
| State priority | [beach_day.liquid](../hubitat-trmnl/beach_day.liquid) (`is_beach_day` → `is_grey_day`) | [beachrules.cpp](../reterminal-e1001/lib/beachrules/beachrules.cpp) `evaluate()` |
| Sun scan / clearing time | [beach_day.liquid](../hubitat-trmnl/beach_day.liquid) (hourly loop) | [beachrules.cpp](../reterminal-e1001/lib/beachrules/beachrules.cpp) `scanSun()` |
| The daylight window | [open-meteo-weather-enhanced.groovy](../hubitat-trmnl/open-meteo-weather-enhanced.groovy) `daylightWindowFor` | [aggregate.cpp](../reterminal-e1001/lib/beachrules/aggregate.cpp) `daylightWindow` |
| All daylight aggregation | [open-meteo-weather-enhanced.groovy](../hubitat-trmnl/open-meteo-weather-enhanced.groovy) `computeDaylightAggregates` | [aggregate.cpp](../reterminal-e1001/lib/beachrules/aggregate.cpp) `fillDay` |
| AQI window | [open-meteo-weather-enhanced.groovy](../hubitat-trmnl/open-meteo-weather-enhanced.groovy) `aqiResponse` | [aggregate.cpp](../reterminal-e1001/lib/beachrules/aggregate.cpp) `fillDay` |
| Parking alert | [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy) `evaluateParkingAlert` | [parking.cpp](../reterminal-e1001/lib/beachrules/parking.cpp) |
| WMO code table | [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy) `wmoCodeToDescription` | [wmo.cpp](../reterminal-e1001/lib/beachrules/wmo.cpp) |

Note that the Hubitat/TRMNL split puts aggregation in Groovy and the decision in
Liquid. A self-contained firmware target does both in one place, so expect its
code to look nothing like the originals even though the verdicts must match.

## Version 2: one window, symmetric days

Version 1 recorded what the Groovy and Liquid happened to do. Version 2 is a
deliberate rule change: **every** aggregate that narrows by time uses that day's
own sunrise→sunset hours, and today and tomorrow are computed the same way.
The old 9 AM–6 PM "active beach hours" window is gone.

What changed, and what it means in practice:

- **One window everywhere.** Rain chance, AQI, humidity, the weather-code mode
  and the sun scan all use sunrise→sunset. Previously rain/code/sun used 9–18,
  AQI used sunrise→sunset, and humidity used all 24 hours.
- **Tomorrow uses tomorrow's sun.** Previously tomorrow's AQI window was built
  from *today's* sunrise and sunset.
- **Rain chance is symmetric.** Previously today's came from hourly rows and
  tomorrow's was the API's whole-day `precipitation_probability_max`.
- **Minimum 3 sunny hours** (`minSunHours`) is now part of the forecast
  condition. Without it, widening the window to sunrise meant a single sunny
  hour at dawn could qualify an otherwise overcast day.
- **Rounding happens once.** A single half-up round straight to an integer.
  The Hubitat path used to round to one decimal and then to an integer, which
  promoted 74.45 °F to 75 and let it pass the `>= 75` threshold. It now reads 74
  and fails, which is the honest answer.

### Consequences worth knowing

- **Humidity ranges will look tighter.** Overnight is when humidity peaks, and
  those hours are now excluded.
- **The marine layer now counts.** In a coastal location, foggy 6–8 AM hours are
  inside the window, so the weather-code mode tilts toward Fog/Overcast more
  often than it did on a 9–18 window. `minSunHours` and the 2 PM clearing
  deadline are what keep that from being decisive.
- **Sun row wording changed.** "All day" now appears only when the sun covers
  all but at most one hour of the window; partial sun shows a count ("5 hrs").
  Without that, a 3-hour dawn-only day would have read "All day".

## Things the extraction turned up

These are recorded as-is; `rules.json` describes current behavior, not a fix.

- **`hubitat-trmnl/README.md` documents 8 visual states; the template has 9.**
  There is a final `Just A Day` / *Not beachy* fallback that renders when no
  other state matches.
- **`nice_day` and `chilly_day` sit at the same precedence level.** Both are
  gated only on beach/night/indoor/rain/wind having not matched, rather than
  `nice_day` being gated on `chilly_day` too. Their temperature ranges are
  disjoint (`< 65` vs `65–74`), so they cannot both fire — but the priority list
  reads as if `nice_day` were strictly lower, and it is not.
- **`sample_data.json` predates the Open-Meteo refactor.** Its keys
  (`probablityofPrecipitation`, epoch `sunrise`) are the old template aliases.
  The current contract's fixtures are in `fixtures/rules-cases.json`.
