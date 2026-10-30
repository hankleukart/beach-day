# Beach Day v1 — Hubitat + TRMNL

The original Beach Day: a high-contrast, e-ink optimised dashboard that decides
whether it's a good day for the beach from real-time weather, UV and air
quality. It renders on a TRMNL device and is driven by a Hubitat hub.

> **This is the superseded version.** It still works and is still maintained,
> but it needs two things the current version does not: a Hubitat hub and a
> TRMNL account. For the self-contained reTerminal version — no hub, no server,
> no subscription — see [README.md](README.md).
>
> The two share nothing at run time. v1's rules live in
> [shared/rules.json](shared/rules.json); the reTerminal version has its own
> rules file and its own engine.

---

## What you need

- A **Hubitat** hub (for the data layer)
- A **TRMNL** account and device — TRMNL OG or TRMNL X (for rendering)

---

## How it works

The system splits in two, so that neither half has to know how the other does
its job.

**1. The data layer — Hubitat.** An open-source, keyless
[Open-Meteo Weather Enhanced](hubitat-trmnl/open-meteo-weather-enhanced.groovy)
driver gathers weather, UV and AQI. The driver also does all daylight-window
aggregation — per-day sunrise-to-sunset max rain chance, AQI and humidity
ranges, the dominant weather code, and the sunny-hour scan — because only it
holds the full 48-hour hourly arrays. A
[Beach Day TRMNL Integrator](hubitat-trmnl/beach-day-trmnl-integrator.groovy)
app then forwards those values, evaluates the street-parking schedule, and
POSTs one clean JSON payload to TRMNL.

**2. The presentation layer — TRMNL.** The
[beach_day.liquid](hubitat-trmnl/beach_day.liquid) template renders the layout
on TRMNL's servers. It uses container-query units (`cqh`/`cqw`), so the same
template scales natively on both the OG and the X.

The display itself stays dumb. It receives a rendered image.

---

## Beach Day criteria

All hourly aggregates use **that day's own sunrise-to-sunset hours**. Every
rule must pass:

| Rule | Threshold |
| --- | --- |
| **Sun** | At least **3 sunny hours** (WMO 0, 1 or 2) in the window, *and* either the day's dominant condition is sunny or the sun arrives by **2:00 PM** |
| **High temp** | ≥ **75 °F** |
| **Rain chance** | < **20 %** (max hourly probability in the window) |
| **Max wind** | ≤ **15 mph** (daily max) |
| **Air quality** | US AQI < **100** (max in the window) |
| **Time of day** | Between sunrise and one hour before sunset |

---

## Visual states

The hero panel shows one of nine states, chosen by top-down priority — the
first match wins:

| | State | Means |
| --- | --- | --- |
| 🌌 | **Night Time** | After sunset. Flips the whole dashboard, checklist and forecast text to tomorrow so you can plan ahead |
| 🏡 | **Indoor Day** | AQI ≥ 100 during the day. Bypasses every other weather fallback to warn about wildfire smoke or heavy smog |
| 🏖️ | **Beach Day!** | All comfort rules pass |
| 🌧️ | **Rain Day** | Rain chance ≥ 40 % |
| 💨 | **Wind Day** | Max wind > 15 mph |
| 🧥 | **Brrr Day** | Below 65 °F |
| 🌤️ | **Nice Day** | 65–74 °F and the sun condition passes — but not quite beachy |
| ☁️ | **Grey Day** | Dry and mild, but the sun condition fails: under 3 sunny hours, or no sun until after 2 PM |
| 🌥️ | **Just A Day** | Final fallback, when nothing else matches |

Note that the priority order is doing real work here. Night and hazardous air
sit above everything, so neither can be masked by a pleasant temperature; and
Beach Day is evaluated before the milder fallbacks, so Nice Day genuinely means
"warm and sunny, but something fell short".

The payload contract and the setup walkthrough are in
**[hubitat-trmnl/README.md](hubitat-trmnl/README.md)**.

---

## Where things live

```
hubitat-trmnl/
  open-meteo-weather-enhanced.groovy    Hubitat driver: fetch + daylight aggregation
  beach-day-trmnl-integrator.groovy     Hubitat app: rules, parking, POST to TRMNL
  beach_day.liquid                      TRMNL template: the layout
  docs/trmnl/                           TRMNL framework notes
shared/
  rules.json                            thresholds and the state priority table
```

[shared/](shared/) is the source of truth for v1's thresholds and state
priority. The Liquid template is edited by hand to match it — there is no
generation step, so a change there has to be mirrored deliberately.
