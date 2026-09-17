# Shared Rules

This folder is the source of truth for **what Beach Day decides**. Each hardware
target implements these rules in its own language; none of them should invent
thresholds of their own.

| File | Purpose |
| --- | --- |
| [rules.json](rules.json) | Thresholds, the active-hours window, the state priority table, and the parking-alert schedule. |
| [wmo-codes.json](wmo-codes.json) | WMO weather code → display text, as returned by Open-Meteo. |
| [sample_data.json](sample_data.json) | Fixture payloads covering each visual state. Used for template previews and as firmware test vectors. |

## The contract

Every implementation takes the same inputs and must produce the same verdict:

**Inputs** — daily high temp (°F), max precipitation probability (%) during
active beach hours, daily max wind (mph), max US AQI during active beach hours,
UV index, the daylight-hours weather code, the hourly forecast rows, and
sunrise/sunset.

**Outputs** — one state id from `rules.json → states.priority`, the six
individual condition pass/fail flags, and the parking alert (active + text).

## Rule of thumb for changes

1. Edit [rules.json](rules.json) first.
2. Update every implementation listed below.
3. Add or adjust a fixture in [sample_data.json](sample_data.json) that covers the change.

## Where each rule is implemented

| Rule | Hubitat + TRMNL | reTerminal E1001 |
| --- | --- | --- |
| Condition thresholds | [beach_day.liquid](../hubitat-trmnl/beach_day.liquid) (`cond_temp`, `cond_precip`, `cond_wind`, `cond_aqi`, `cond_forecast`, `cond_time`) | not yet built |
| State priority | [beach_day.liquid](../hubitat-trmnl/beach_day.liquid) (`is_beach_day` → `is_grey_day`) | not yet built |
| Hourly aggregation | [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy) (`updateTrmnl`) | not yet built |
| Parking alert | [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy) (`evaluateParkingAlert`) | not yet built |
| WMO code table | [beach-day-trmnl-integrator.groovy](../hubitat-trmnl/beach-day-trmnl-integrator.groovy) (`wmoCodeToDescription`) | not yet built |

Note that the Hubitat/TRMNL split puts aggregation in Groovy and the decision in
Liquid. A self-contained firmware target does both in one place, so expect its
code to look nothing like the originals even though the verdicts must match.

## Two things the extraction turned up

These are recorded as-is; `rules.json` describes current behavior, not a fix.

- **`hubitat-trmnl/README.md` documents 8 visual states; the template has 9.**
  There is a final `Just A Day` / *Not beachy* fallback that renders when no
  other state matches.
- **`nice_day` and `chilly_day` sit at the same precedence level.** Both are
  gated only on beach/night/indoor/rain/wind having not matched, rather than
  `nice_day` being gated on `chilly_day` too. Their temperature ranges are
  disjoint (`< 65` vs `65–74`), so they cannot both fire — but the priority list
  reads as if `nice_day` were strictly lower, and it is not.
