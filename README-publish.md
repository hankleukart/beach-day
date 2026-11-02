# Publishing Beach Day

Every command you need to save your work, change the rules, update the boards
over Wi-Fi, and flash a board over USB. How the product itself behaves is in
[README.md](README.md).

All of the commands start from the repo:

```sh
cd ~/Documents/Dev/beach-day
```

The firmware lives in `reterminal-e1001/` (it serves both panels, E1001 and
E1002). The rules live in `shared/v3/day-outcomes.toml`.

---

## 1. Commit and push to GitHub

Check before you commit. Both should pass:

```sh
cd reterminal-e1001
./flash.sh test                    # the rules tests, on this Mac
tools/preview.sh                   # optional: renders every screen to docs-design/preview/
cd ..
```

Commit and push:

```sh
git status                         # see what changed
git add -A
git commit -m "What changed, in a sentence"
git push origin v3-redesign
```

`src/beachday_config.h` holds your Wi-Fi password and is ignored by git, so
`git add -A` never picks it up.

**Checkpoint.** To mark a version you might want to come back to, add a tag
and push it:

```sh
git tag -a v0.12.0-source -m "Source of firmware 0.12.0"
git push origin v0.12.0-source
```

To go back to it later: `git checkout v0.12.0-source`. To return to the latest
version afterwards: `git checkout v3-redesign`.

---

## 2. Change the rules

Everything the display says — outcomes, thresholds, names, wear, whimsy,
night wording, holidays, the stat tiles — is in
`shared/v3/day-outcomes.toml`. The comments in that file explain each
section. Edit it, then:

```sh
cd reterminal-e1001
./flash.sh test                    # the engine still agrees with the file
tools/preview.sh                   # see the screens as PNGs in docs-design/preview/
```

Then commit and push (section 1). Every board picks the new rules up within a
day — no firmware release needed. To try them on the board on your desk
first: `./flash.sh rules e1001`.

Things the checks enforce, so you don't have to remember them:

- Every outcome has exactly four `wear` items, each one in the `[wear]`
  catalogue, and at least one way to be named (a place, a thing, or a
  `*starred` wear item).
- Every icon name is in the `icons` list at the top of the file.
- Every field in a `when` exists (`tempMax`, `rain`, `wind`, `swing`, `cloud`,
  `snow`, `night`, …). A misspelt field would otherwise never pass, silently.
- The last outcome has no `when` (it is the catch-all); no other outcome may
  leave it out.
- A `night` table only rewords: `tagline`, `also` or `icon` on an outcome;
  parts of a stat in `[stat.night]`; `subline` or `footer` in `[screen.night]`.

**A new icon** means drawing it in `reterminal-e1001/tools/gen_icons.py`,
then:

```sh
cd reterminal-e1001
.venv/bin/python tools/gen_icons.py     # regenerates src/icons_data.h and docs-design/icons-preview.png
```

and adding its name to `icons` in the rules. That needs a firmware release
(section 3), because the drawings live in the firmware.

**Rules that need new firmware.** If a rules change uses something only a
newer firmware understands, older boards reject the file and keep their
current rules until they update — safe, just not immediate. The new firmware
carries the new rules built in. Push the rules and publish the firmware
together: commit and push, then run the release straight after.

### See how often a rules change would flip the screen

```sh
cd reterminal-e1001
tools/replay.sh                    # the last 60 days of real forecasts, Venice Beach
tools/replay.sh --no-pin           # the same, without the morning pin
tools/replay.sh --sensitivity      # and again at 2h and 6h forecast lags
```

It replays archived forecasts through the real engine on the board's own
wake schedule, and prints every outcome change and the threshold that caused
it. Downloads are cached in `.pio/replay/cache`, so a second run is instant.

---

## 3. Update the boards over Wi-Fi (OTA)

### Rules only

Push the commit. That's all. Each board fetches the rules from the
`v3-redesign` branch on GitHub once a day.

### Firmware

1. Bump the version in `reterminal-e1001/src/version.h` (e.g. `0.12.0` →
   `0.12.1`). Boards update only when this string differs from the one they
   run, so publishing without a bump does nothing.
2. Commit and push (section 1). The release script refuses to publish
   uncommitted or unpushed code, so that the release tag points at the
   source it was built from.
3. Publish:

```sh
cd reterminal-e1001
tools/release.sh --publish
```

This runs the tests (and stops if any fail), builds one firmware for both
panels, and creates the GitHub release `fw-v<version>`, marked Latest, with
the firmware and its `manifest.json`. Run `tools/release.sh` without
`--publish` to build and check without releasing.

To confirm what the boards will see:

```sh
curl -sL https://github.com/hankleukart/beach-day/releases/latest/download/manifest.json
```

**When the boards pick it up:** each board checks once a day, and only if its
battery is at 30% or more. To make one board update now, **hold Right Nav for
about 3 seconds**, until the LED comes on, and then let go. The screen says
"Checking for updates" and the board restarts on the new version within a
minute. Tap Right Nav to see the readings screen, whose footer shows the
version the board is running.

If the version already exists as a release, GitHub refuses it
(`a release with the same tag name already exists`). Bump the version and
publish again.

**A broken release** can be fixed the same way: publish a fixed version. A
board whose firmware keeps crashing goes into safe mode and looks for new
firmware every 30 minutes. A firmware that runs but draws the wrong thing
has no automatic rollback, so try a release on a bench board over USB first.

---

## 4. Update a board over USB

Plug the board in with a data cable and switch it on. If it's asleep, press
Green (Refresh) to wake it. Then:

```sh
cd reterminal-e1001
```

| Command | What it does |
| --- | --- |
| `./flash.sh release e1001` | Your everyday firmware for the black-and-white board. Keeps saved settings. |
| `./flash.sh release e1002` | The same, for the colour board. |
| `./flash.sh dev e1001` | Bench build: stays awake, and the buttons cycle through outcomes. |
| `./flash.sh rules e1001` | Puts only the rules file on the board; the firmware is left alone. |
| `./flash.sh erase e1001` | Wipes saved settings (Wi-Fi, location, reminders), then flashes. |
| `./flash.sh gift e1001` | For a board you're giving away: erases it and builds with no Wi-Fi or location. |
| `./flash.sh test` | Runs the rules tests. No board needed. |

Swap `e1001` for `e1002` for the colour board. Flashing over USB also tells
the board which panel it has; after that, updates over Wi-Fi keep it.

`release`, `dev` and `erase` open the serial monitor afterwards, showing
what the board decides on each wake. `Ctrl-C` quits it; the board keeps running.

**Before a board leaves the house:** run `./flash.sh erase` or `./flash.sh gift`.
Your Wi-Fi password is saved on the board and survives a normal reflash.
`gift` is the one for a batch: plug in, run, unplug, repeat. Each board then
starts as its own `BeachDay-Setup-xxxx` hotspot for its new owner.

**If the upload can't find the board:** check the cable is a data cable,
the power switch is on, and press Green (Refresh) to wake it, then run the
command again.

### The dev build

`./flash.sh dev` stays awake instead of sleeping. Type into the serial
monitor, or use the buttons:

| Key | Button | Does |
| --- | --- | --- |
| `n` | Left Nav | Step through the outcomes with preset numbers |
| `h` | Right Nav | The hourly readings |
| `p` | — | Toggle the parking bar |
| `l` | Green | Back to the real forecast |
| `w` | — | Open the setup page |
| `d` | — | Print the current screen as text |
| `R` | — | Restart and fetch again |
| `?` | — | List the keys |

### If a panel has stopped updating

E-paper holds its last image with no power at all, so a frozen screen, a
crashed board and a flat battery look the same. In order:

1. **Plug in USB.** If the battery ran down, it redraws within about 20
   seconds.
2. **Look at the top-right corner.** `Updated 2:05p` is the last good update;
   `OFFLINE since …` means Wi-Fi or the weather service, not the board.
3. **Watch it on serial:** `./flash.sh release e1001` (or plug in and run
   `.venv/bin/pio device monitor`), then press Green. You should see Wi-Fi
   connect, the outcome it chose, and how long it will sleep.

---

## One-time setup on a new Mac

```sh
cd ~/Documents/Dev/beach-day/reterminal-e1001
python3 -m venv .venv && .venv/bin/pip install platformio
cp src/beachday_config.example.h src/beachday_config.h    # then fill in Wi-Fi and location
brew install gh && gh auth login                          # for tools/release.sh --publish
```

---

## Where the code is

```
shared/v3/day-outcomes.toml   the rules
reterminal-e1001/
  flash.sh                    USB: build, flash, monitor
  platformio.ini              build settings for every board and the tests
  src/version.h               the firmware version — bump before a release
  src/beachday_config.h       your Wi-Fi and location (not in git)
  src/main.cpp                wake → fetch → decide → draw → sleep
  src/render.cpp              the screen layout
  src/portal.cpp              the setup page
  lib/dayrules/               the rules engine, its checks, and the day/night figures
  lib/beachrules/             street cleaning and the weekly reminder
  test/test_dayrules/         the tests behind ./flash.sh test
  tools/release.sh            OTA publish
  tools/preview.sh            render the screens to PNGs
  tools/replay.sh             replay real forecasts through the rules
  tools/gen_icons.py          the icon drawings
```
