# Publishing Beach Day

Every command you need to save your work, update the boards over Wi-Fi, and
flash a board over USB. All of the commands start from the repo:

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

## 2. Update the boards over Wi-Fi (OTA)

There are two separate updates: the **rules** and the **firmware**.

### Rules only (`day-outcomes.toml`)

Push the commit. That's all.

```sh
git push origin v3-redesign
```

Each board fetches the rules from the `v3-redesign` branch on GitHub once a
day. It rejects a file that fails its checks and keeps the rules it already
has, so run `./flash.sh test` before pushing. The firmware embeds a copy of
the rules, so if a new rules file uses something only newer firmware
understands, older boards keep their current rules until they update.

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

This runs the tests, builds one firmware for both panels, and creates the
GitHub release `fw-v<version>`, marked Latest, with the firmware and its
`manifest.json`. Run `tools/release.sh` without `--publish` to build and
check without releasing.

**When the boards pick it up:** each board checks once a day, and only if its
battery is at 30% or more. To make one board update now, **hold Right Nav for
about 3 seconds**, until the LED comes on, and then let go. The screen says
"Checking for updates" and the board restarts on the new version within a
minute. Tap Right Nav to see the readings screen, whose footer shows the
version the board is running.

If the version already exists as a release, GitHub refuses it
(`a release with the same tag name already exists`). Bump the version and
publish again.

---

## 3. Update a board over USB

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

**Before a board leaves the house:** run `./flash.sh erase` or `./flash.sh gift`.
Your Wi-Fi password is saved on the board and survives a normal reflash.

**If the upload can't find the board:** check the cable is a data cable,
the power switch is on, and press Green (Refresh) to wake it, then run the
command again.

---

## One-time setup on a new Mac

```sh
cd ~/Documents/Dev/beach-day/reterminal-e1001
python3 -m venv .venv && .venv/bin/pip install platformio
cp src/beachday_config.example.h src/beachday_config.h    # then fill in Wi-Fi and location
brew install gh && gh auth login                          # for tools/release.sh --publish
```
