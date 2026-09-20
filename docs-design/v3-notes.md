# v3 redesign — working notes

Branched from `v2.0-reterminal` (`6d5359d`). That tag is the known-good
fallback: `git checkout v2.0-reterminal` gets the working version back at any
point, and it rebuilds byte-for-byte because the toolchain is pinned.

This branch changes **both** layers at once: the visual design and the
underlying outcomes, criteria and logic. That combination is why it needs a
branch rather than incremental commits to `main`.

## Decisions already made (mechanics, not design)

**`shared/rules.json` on this branch is v3.** It is free to change shape
entirely — new conditions, new states, different windows. `main` keeps v2
untouched, so git is holding the two versions rather than two files doing it.

**`hubitat-trmnl/` is frozen on this branch.** It still implements rules v2 and
will visibly disagree with `shared/rules.json` here. That is deliberate:

- The Groovy has no test harness, so rule changes there are unverified by
  construction — the reTerminal has 69 host fixtures, the Hubitat path has zero.
- It is running on real hardware today; breaking it to chase an experiment is a
  bad trade.
- Porting once, at the end, against settled rules is cheaper than tracking a
  moving target through two languages.

If v3 lands, the choice is then: port the Groovy and Liquid, or retire that
target. Do not decide that now.

**The reTerminal is the design surface.** `pio test -e native` for logic,
`./flash.sh` plus `n` to cycle states for visuals. Keep that loop fast.

## What has to move together

Changing the rules touches more than `rules.json`. The full set:

| Change | Also update |
| --- | --- |
| A threshold | `shared/rules.json`, then `tools/gen.py` regenerates `rules_config.h` |
| A new/removed **state** | `states.priority` in rules.json; `State` enum + `STATE_TEXT` in `beachrules.*`; `heroIcon()` in `render.cpp`; `DEMO_COUNT` and `buildDemoView()` in `devmode.cpp` |
| A new/removed **condition** | `beachDayConditions`; `Verdict` flags; the checklist rows in `drawDetails()`; `ViewModel` |
| A new **input** | `inputAggregation` in rules.json; `RawForecast`/`DayInputs`; `aggregate.cpp`; the Open-Meteo query in `weather.cpp` |
| Anything | a fixture in `shared/fixtures/rules-cases.json` that pins the new behaviour |

The checklist is currently hard-coded to five rows in `drawDetails()` and the
hero panel to nine states. Neither is driven by rules.json at runtime — the
generator only emits constants. If v3 makes the state or condition list
genuinely fluid, consider generating the render tables too.

## Open — for you to fill in

- What question should the display answer? (v2: "is today a beach day?")
- What outcomes replace the nine states?
- What criteria, and what are the inputs behind them?
- What does the panel look like? What is the one thing readable across a room?
- Does the night-mode flip to tomorrow survive?
- Do the parking alerts stay, or were they always a separate concern?

## Decisions made while building (2026-09-20)

- **Rules are loaded at run time.** `shared/v3/day-outcomes.json` is the spec
  as written; `lib/dayrules` interprets it (first-match outcomes, `all`/`any`
  conditions, banded stats, `byFailedTest` / near-threshold copy, `{token}`
  templates). It ships on LittleFS, is embedded as a fallback, and is
  re-fetched daily from GitHub — copy and thresholds change on every unit with
  no firmware update.
- **Fonts and icons are vector on the device.** Real Fraunces Black and Nunito
  TTFs (instanced + subset offline, 56 KB) rasterised by `stb_truetype`; icons
  are 48-unit command lists interpreted at any size. One asset per face/icon
  for every panel. Bitmaps were built first and dropped: crisper at 11 px, but
  a second pipeline per target was the wrong trade.
- **Colour is a role, not a pixel.** Everything draws in `Role`s (`paint.h`);
  the E1001 table maps accents to dot patterns, the E1002 will map them to its
  six inks. Icons carry SUN/WATER/LEAF/WARM fills for that day.
- **800×480 re-layout, not a scale.** The 1200×825 mock is a different aspect
  ratio; proportions were kept (35% hero panel, same vertical rhythm).
- **Evening flip kept, pre-dawn flip dropped.** After sunset−1h the screen
  plans tomorrow; at 5 AM it shows today, which v2 got wrong.
- **Parking alert kept**, in the footer pill slot.
- **Hubitat target frozen on v2**, as planned.

## Log

- 2026-09-20 — branch created off `v2.0-reterminal`.
- 2026-09-20 — spec received; runtime engine, vector fonts/icons, v3 layout
  built; firmware 0.3.0 builds; 13 engine tests green. Not yet on glass.
