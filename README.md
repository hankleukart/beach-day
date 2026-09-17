# Beach Day

A high-contrast, e-ink optimized dashboard that decides whether it's a good day
to go to the beach, based on real-time weather, UV, and air quality.

The beach-day *decision* is one set of rules. How it gets rendered depends on
what hardware is on hand, so each target lives in its own folder and shares the
rules rather than reinventing them.

## Targets

| Folder | Hardware | Needs | Status |
| --- | --- | --- | --- |
| [hubitat-trmnl/](hubitat-trmnl/) | TRMNL OG / TRMNL X | Hubitat hub + TRMNL account | Working |
| [reterminal-e1001/](reterminal-e1001/) | Seeed reTerminal E1001 (7.5" mono ePaper, ESP32-S3) | Wi-Fi only | Firmware written, not yet flashed |

## Shared rules

[shared/](shared/) holds the thresholds, the state priority table, the WMO code
map, and the test fixtures. It is the source of truth — when a rule changes,
it changes there first. The reTerminal firmware generates its constants and
tests from it; the TRMNL template is edited by hand to match.

See [shared/README.md](shared/README.md) for the input/output contract every
target must satisfy.

## Which one should I use?

**hubitat-trmnl** if you already run a Hubitat hub and a TRMNL device. The hub
does the data work and TRMNL's servers render the layout, so the display itself
stays dumb.

**reterminal-e1001** if you want the whole thing self-contained. The board talks
to Open-Meteo directly over Wi-Fi, decides, draws, and sleeps. No hub, no
account, no server — just Wi-Fi credentials and a location.
