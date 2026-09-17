# Shared Rules

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

**Inputs** — daily high temp (°F), max precipitation probability (%) — from
active beach hours today but the daily max tomorrow — daily max wind (mph),
US AQI min/max over the sunrise→sunset hour window, UV index, the mode weather
code over active hours, the active-hour rows, and sunrise/sunset. The exact
derivations are spelled out in `rules.json → inputAggregation`; they are not
symmetric between today and tomorrow, and implementations must copy the
asymmetry rather than tidy it.

**Outputs** — one state id from `rules.json → states.priority`, the six
individual condition pass/fail flags, and the parking alert (active + text).

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
| Aggregation (precip, code mode) | [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy) `updateTrmnl` | [aggregate.cpp](../reterminal-e1001/lib/beachrules/aggregate.cpp) |
| Aggregation (AQI window, humidity) | [open-meteo-weather-enhanced.groovy](../hubitat-trmnl/open-meteo-weather-enhanced.groovy) `aqiResponse` | [aggregate.cpp](../reterminal-e1001/lib/beachrules/aggregate.cpp) |
| Parking alert | [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy) `evaluateParkingAlert` | [parking.cpp](../reterminal-e1001/lib/beachrules/parking.cpp) |
| WMO code table | [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy) `wmoCodeToDescription` | [wmo.cpp](../reterminal-e1001/lib/beachrules/wmo.cpp) |

Note that the Hubitat/TRMNL split puts aggregation in Groovy and the decision in
Liquid. A self-contained firmware target does both in one place, so expect its
code to look nothing like the originals even though the verdicts must match.

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
- **Today and tomorrow are aggregated differently.** Today's rain chance is the
  max over the 9–18 active hours; tomorrow's is Open-Meteo's daily
  `precipitation_probability_max` (whole day). The AQI min/max window is the
  *sunrise hour to sunset hour*, not 9–18, and tomorrow's AQI uses today's
  sunrise/sunset hours. `rules.json → inputAggregation` records all of this and
  the fixtures pin it.
- **Rounding happens twice on the Hubitat path.** The driver rounds to one
  decimal (HALF_UP), then the app rounds to an integer, so 74.45 °F displays as
  75. The firmware reproduces the double rounding; there is a fixture for it.
- **`sample_data.json` predates the Open-Meteo refactor.** Its keys
  (`probablityofPrecipitation`, epoch `sunrise`) are the old template aliases.
  The current contract's fixtures are in `fixtures/rules-cases.json`.
