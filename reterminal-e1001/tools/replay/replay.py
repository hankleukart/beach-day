#!/usr/bin/env python3
"""How often would the board have changed its mind?

Replays archived forecast runs through the real engine (tools/replay/replay.cpp)
and reports, per day, every outcome the board would have shown and why it
changed. Driven by tools/replay.sh, which builds the engine binary first.

What it reproduces, and what it can't:

- The board asks for best_match with no model pinned. Here that is the same
  data as gfs_seamless, value for value; gfs_seamless is used because the
  single-runs archive keeps it further back.
- The archive keeps runs at 00/06/12/18Z only. The live API also blends
  hourly HRRR updates in between, so this sees at most four changes a day
  where the board could see more. Treat the counts as a floor.
- A run is assumed usable --lag hours after its nominal time. That moves when
  a change shows up, rarely whether it does; --sensitivity checks that.
- Each wake sees, for every hour of the day, the newest usable run that had
  started by that hour: earlier hours come from earlier runs, the way the
  live API stitches the day's past hours.
- Wakes follow the firmware's schedule (main.cpp secondsToNextWake), with the
  evening flip at sunset + 1h. Parking cutoffs are ignored.
- AQI is the air-quality archive's single series, the same on every wake, so
  AQI never causes a flip here.
- The morning pin runs through the engine's own resolvePin, as on the board.
  --no-pin shows what the screen would do without it.
"""
import argparse
import bisect
import json
import subprocess
import sys
import time
import tomllib
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

VARS = "temperature_2m,relative_humidity_2m,precipitation_probability,weather_code,cloud_cover,wind_speed_10m"
TZ_NAME = "America/Los_Angeles"
WAKE_DAY_MIN, WAKE_NIGHT_MIN = 60, 240
WAKING_FROM = 6 * 60           # "I wake up": what the screen says from 6 AM on
FIELDS = ("tempMax", "tempMin", "swing", "rain", "wind", "cloud", "aqi", "sunHours", "snow")


# ─── fetching ──────────────────────────────────────────────────────────────

def fetch(url, cache: Path):
    if cache.exists():
        return json.loads(cache.read_text())
    for attempt in range(5):
        try:
            with urllib.request.urlopen(url, timeout=90) as r:
                data = json.loads(r.read())
            break
        except urllib.error.HTTPError as e:
            body = e.read()
            try:
                data = json.loads(body)          # an unavailable run is a 400 with a reason
                break
            except ValueError:
                pass
            if e.code == 429 or e.code >= 500:
                time.sleep(5 * (attempt + 1)); continue
            raise
        except (urllib.error.URLError, TimeoutError):
            time.sleep(5 * (attempt + 1))
    else:
        sys.exit(f"replay: giving up on {url}")
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(data))
    time.sleep(0.15)                             # be a polite guest on a free API
    return data


def q(base, **params):
    return base + "?" + urllib.parse.urlencode(params, safe=",:")


def intOr(v):
    return -1 if v is None else int(round(v))


class Run:
    """One archived model run: unix hour -> the row the board would build."""
    def __init__(self, nominal, data):
        self.nominal = nominal
        h = data["hourly"]
        self.rows = {}
        for i, t in enumerate(h["time"]):
            # Mirror weather.cpp exactly, quirks included: a missing temperature
            # or wind becomes 0, the small integers become -1.
            temp, wind = h["temperature_2m"][i], h["wind_speed_10m"][i]
            self.rows[t] = (0.0 if temp is None else temp, 0.0 if wind is None else wind,
                            intOr(h["relative_humidity_2m"][i]), intOr(h["precipitation_probability"][i]),
                            intOr(h["cloud_cover"][i]), intOr(h["weather_code"][i]),
                            temp is None or wind is None)


def load_everything(args, first: date, last: date, cache: Path):
    common = dict(latitude=args.lat, longitude=args.lon, timeformat="unixtime",
                  temperature_unit="fahrenheit", wind_speed_unit="mph")
    # Runs from two days before the first target day, so the first evening's
    # preview has what it needs; up to the last target day itself.
    t = datetime(first.year, first.month, first.day, tzinfo=timezone.utc) - timedelta(days=2)
    stop = datetime(last.year, last.month, last.day, tzinfo=timezone.utc) + timedelta(days=1, hours=12)
    runs, missing = [], []
    total = int((stop - t).total_seconds() // (6 * 3600)) + 1
    n = 0
    while t <= stop:
        n += 1
        stamp = t.strftime("%Y-%m-%dT%H:00")
        print(f"\r  runs {n}/{total} {stamp}", end="", file=sys.stderr, flush=True)
        d = fetch(q("https://single-runs-api.open-meteo.com/v1/forecast", **common, models=args.model,
                    run=stamp, hourly=VARS, forecast_days=3),
                  cache / "runs" / f"{args.model}_{args.lat}_{args.lon}_{stamp}.json")
        if d.get("error"):
            missing.append(stamp)
        else:
            runs.append(Run(int(t.timestamp()), d))
        t += timedelta(hours=6)
    print(file=sys.stderr)

    span = dict(start_date=(first - timedelta(days=2)).isoformat(), end_date=(last + timedelta(days=1)).isoformat())
    daily = fetch(q("https://historical-forecast-api.open-meteo.com/v1/forecast", **common, **span,
                    timezone=TZ_NAME, daily="sunrise,sunset,weather_code"),
                  cache / f"daily_{args.lat}_{args.lon}_{span['start_date']}_{span['end_date']}.json")
    actual = fetch(q("https://historical-forecast-api.open-meteo.com/v1/forecast", **common, **span,
                     models=args.model, hourly=VARS),
                   cache / f"actual_{args.model}_{args.lat}_{args.lon}_{span['start_date']}_{span['end_date']}.json")
    aqi = fetch(q("https://air-quality-api.open-meteo.com/v1/air-quality", latitude=args.lat, longitude=args.lon,
                  timeformat="unixtime", hourly="us_aqi", **span),
                cache / f"aqi_{args.lat}_{args.lon}_{span['start_date']}_{span['end_date']}.json")
    for name, d in (("daily", daily), ("actual", actual), ("aqi", aqi)):
        if d.get("error"):
            sys.exit(f"replay: {name} fetch failed: {d.get('reason')}")
    return runs, missing, daily, Run(0, actual), aqi


# ─── the board's view of the world ─────────────────────────────────────────

class World:
    def __init__(self, runs, daily, actual, aqi, tz, lag_h):
        self.runs = sorted(runs, key=lambda r: r.nominal)
        self.avail = [r.nominal + lag_h * 3600 for r in self.runs]
        self.tz = tz
        self.actual = actual
        self.days = {}
        dd = daily["daily"]
        for i, t in enumerate(dd["time"]):
            d = datetime.fromtimestamp(t, tz).date()
            def mins(x):
                if not x:
                    return 0
                lt = datetime.fromtimestamp(x, tz)
                return lt.hour * 60 + lt.minute
            self.days[d] = (mins(dd["sunrise"][i]), mins(dd["sunset"][i]),
                            dd["weather_code"][i] if dd["weather_code"][i] is not None else 99)
        self.aqi = {}
        for t, v in zip(aqi["hourly"]["time"], aqi["hourly"]["us_aqi"]):
            self.aqi[t] = intOr(v)
        self.zeroed = 0      # rows where weather.cpp's missing->0 quirk kicked in

    def flip(self, d):
        # main.cpp eveningFlipMinute(): an hour after sunset, clamped to the day.
        return min(self.days[d][1] + 60, 1439)

    def hours_of(self, d):
        for h in range(24):
            yield h, int(datetime(d.year, d.month, d.day, h, tzinfo=self.tz).timestamp())

    def rows_at(self, T, d):
        """What a wake at unix time T would have fetched for local day d."""
        k = bisect.bisect_right(self.avail, T)
        usable = self.runs[:k]
        rows = []
        for h, ts in self.hours_of(d):
            pick = None
            for r in reversed(usable):
                if r.nominal <= ts and ts in r.rows:
                    pick = r; break
            if pick is None:
                continue
            row = pick.rows[ts]
            if row[6]:
                self.zeroed += 1
            rows.append((h,) + row[:6])
        return rows

    def actual_rows(self, d):
        return [(h,) + self.actual.rows[ts][:6] for h, ts in self.hours_of(d) if ts in self.actual.rows]

    def aqi_rows(self, d):
        return [(h, self.aqi[ts]) for h, ts in self.hours_of(d) if ts in self.aqi]

    def wakes(self, start_T, end_T):
        """main.cpp secondsToNextWake(), minus parking."""
        T = start_T
        while T < end_T:
            lt = datetime.fromtimestamp(T, self.tz)
            today = lt.date()
            if today not in self.days or today + timedelta(days=1) not in self.days:
                break
            mod = lt.hour * 60 + lt.minute
            sr, ss, _ = self.days[today]
            tomorrow = ss > 0 and mod >= self.flip(today)
            yield T, (today + timedelta(days=1) if tomorrow else today)
            day_period = mod >= sr - 60 and mod < ss
            nxt = mod + (WAKE_DAY_MIN if day_period else WAKE_NIGHT_MIN)
            for b in (sr, ss, self.flip(today), sr + 1440):
                if mod < b and b + 1 < nxt:
                    nxt = b + 1
            T += max((nxt - mod) * 60 - lt.second, 120)


def record(key, d, sunrise, sunset, code, rows, aqi_rows, pinnable=False):
    dow = (d.weekday() + 1) % 7                     # engine: 0 = Sunday
    out = [f"E {key} {(d - date(1970, 1, 1)).days} {d.year} {d.month} {d.day} {dow} "
           f"{sunrise} {sunset} {code} {len(rows)} {len(aqi_rows)} {1 if pinnable else 0}"]
    out += [f"H {h} {t:.2f} {w:.2f} {hu} {pp} {c} {wc}" for h, t, w, hu, pp, c, wc in rows]
    out += [f"A {h} {a}" for h, a in aqi_rows]
    return "\n".join(out)


def run_engine(binary, spec, records):
    p = subprocess.run([binary, spec], input="\n".join(records) + "\n", capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"replay: engine failed:\n{p.stderr}")
    res = {}
    for line in p.stdout.splitlines():
        head, _, title = line.partition(" | ")
        parts = head.split()
        # shown, inputs, title, and what the forecast alone would have said
        res[parts[0]] = (parts[1], dict(zip(FIELDS, map(float, parts[3:]))), title, parts[2])
    return res


# ─── the rules, for explaining a flip ──────────────────────────────────────

OPS = {">=": lambda a, b: a >= b, "<=": lambda a, b: a <= b, ">": lambda a, b: a > b,
       "<": lambda a, b: a < b, "==": lambda a, b: a == b, "!=": lambda a, b: a != b}


def parse_rules(spec_path):
    spec = tomllib.loads(Path(spec_path).read_text())
    outcomes = []
    for o in spec["outcome"]:
        conds = []
        for expr in o.get("when", []):
            f, op, v = expr.split()
            if f not in FIELDS:
                sys.exit(f"replay: {o['id']} uses {f}, which the replay does not print")
            conds.append((expr, f, op, float(v)))
        outcomes.append((o["id"], conds))
    return outcomes


def first_match(outcomes, inp):
    for oid, conds in outcomes:
        if all(OPS[op](inp[f], v) for _, f, op, v in conds):
            return oid
    return None


def why(outcomes, a_out, a_in, b_out, b_in):
    """Every condition, among the outcomes that could have decided either
    side, whose truth changed. That's what crossed a threshold."""
    ids = [o for o, _ in outcomes]
    top = max(ids.index(a_out), ids.index(b_out))
    seen, out = set(), []
    for _, conds in outcomes[:top + 1]:
        for expr, f, op, v in conds:
            if expr in seen:
                continue
            seen.add(expr)
            if OPS[op](a_in[f], v) != OPS[op](b_in[f], v):
                out.append((expr, f, a_in[f], b_in[f]))
    return out


# ─── the report ────────────────────────────────────────────────────────────

def simulate(world, binary, spec, first, last, pin=True):
    start_T = int(datetime(*(first - timedelta(days=1)).timetuple()[:3], tzinfo=world.tz).timestamp()) \
        + world.flip(first - timedelta(days=1)) * 60
    end_T = int(datetime(*last.timetuple()[:3], tzinfo=world.tz).timestamp()) + world.flip(last) * 60
    recs, wakes = [], []
    for T, d in world.wakes(start_T, end_T):
        if not (first <= d <= last):
            continue
        key = f"{d.isoformat()}@{T}"
        sr, ss, code = world.days[d]
        # main.cpp: today, from an hour before sunrise; never tomorrow's preview.
        lt = datetime.fromtimestamp(T, world.tz)
        pinnable = pin and d == lt.date() and lt.hour * 60 + lt.minute >= (sr if sr > 0 else 360) - 60
        recs.append(record(key, d, sr, ss, code, world.rows_at(T, d), world.aqi_rows(d), pinnable))
        wakes.append((key, T, d))
    for d in daterange(first, last):
        sr, ss, code = world.days[d]
        recs.append(record(f"{d.isoformat()}@actual", d, sr, ss, code, world.actual_rows(d), world.aqi_rows(d)))
    res = run_engine(binary, spec, recs)
    per_day = {d: [] for d in daterange(first, last)}
    for key, T, d in wakes:
        per_day[d].append((T,) + res[key])
    actual = {d: res[f"{d.isoformat()}@actual"] for d in daterange(first, last)}
    return per_day, actual, res


def daterange(a, b):
    d = a
    while d <= b:
        yield d
        d += timedelta(days=1)


def changes(seq):
    """[(T, outcome, inputs, title)] -> the transitions in it."""
    out = []
    for prev, cur in zip(seq, seq[1:]):
        if prev[1] != cur[1]:
            out.append((prev, cur))
    return out


def analyse(world, per_day, actual):
    rows = []
    for d, seq in per_day.items():
        if not seq:
            continue
        midnight = int(datetime(d.year, d.month, d.day, tzinfo=world.tz).timestamp())
        wake_T = midnight + WAKING_FROM * 60
        # What the screen showed at 6 AM is the last wake at or before 6 AM;
        # from there, every wake up to the flip is something you could see.
        before = [s for s in seq if s[0] <= wake_T]
        at6 = before[-1] if before else seq[0]
        waking = [at6] + [s for s in seq if s[0] > wake_T]
        rows.append(dict(day=d, seq=seq, evening=seq[0], at6=at6, end=seq[-1], actual=actual[d],
                         all=changes(seq), waking=changes(waking),
                         distinct_waking=len({s[1] for s in waking})))
    return rows


def fmt_time(T, tz):
    lt = datetime.fromtimestamp(T, tz)
    h = lt.hour % 12 or 12
    return f"{h}:{lt.minute:02d}{'p' if lt.hour >= 12 else 'a'}"


def short(oid):
    return oid.removesuffix("_day")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary"); ap.add_argument("spec")
    ap.add_argument("--days", type=int, default=60)
    ap.add_argument("--end", default=None, help="last target day (default: yesterday)")
    ap.add_argument("--lat", type=float, default=33.985)
    ap.add_argument("--lon", type=float, default=-118.4695)
    ap.add_argument("--model", default="gfs_seamless")
    ap.add_argument("--lag", type=float, default=4.0, help="hours from a run's nominal time to usable")
    ap.add_argument("--sensitivity", action="store_true", help="also rerun at lag 2h and 6h")
    ap.add_argument("--no-pin", action="store_true", help="show the forecast at every wake, as before the pin")
    ap.add_argument("--cache", default=".pio/replay/cache")
    args = ap.parse_args()

    tz = ZoneInfo(TZ_NAME)
    last = date.fromisoformat(args.end) if args.end else datetime.now(tz).date() - timedelta(days=1)
    first = last - timedelta(days=args.days - 1)
    print(f"fetching {first} .. {last} ({args.model} at {args.lat}, {args.lon})", file=sys.stderr)
    runs, missing, daily, actual, aqi = load_everything(args, first, last, Path(args.cache))

    outcomes = parse_rules(args.spec)
    world = World(runs, daily, actual, aqi, tz, args.lag)
    per_day, actual_res, res = simulate(world, args.binary, args.spec, first, last, pin=not args.no_pin)

    # The engine decides; this only checks that the explanation agrees with it.
    disagree = [k for k, (_, inp, _, live) in res.items() if first_match(outcomes, inp) != live]

    rows = analyse(world, per_day, actual_res)
    P = print

    P(f"Beach Day replay — {len(rows)} days, {first} to {last}")
    P(f"{args.model} at {args.lat}, {args.lon} · runs every 6h, usable {args.lag:g}h after nominal · "
      f"{len(runs)} runs, {len(missing)} missing")
    P(f"rules: {args.spec} · flip at sunset+1h · morning pin {'off' if args.no_pin else 'on'} · "
      f"{sum(len(r['seq']) for r in rows)} wakes evaluated")
    if disagree:
        P(f"!! {len(disagree)} evaluations where the explanation disagrees with the engine: {disagree[:5]}")
    if world.zeroed:
        P(f"!! {world.zeroed} hour-rows had a missing temp/wind turned into 0 (weather.cpp does the same)")
    if missing:
        P(f"missing runs: {', '.join(missing)}")
    P()

    P("Per day: what the screen said, in order. The first entry is the evening preview,")
    P("the night before. ▸ marks what was on screen at 6 AM.")
    P()
    for r in rows:
        d = r["day"]
        segs = []                       # [start, outcome, on screen at 6 AM?]
        for s in r["seq"]:
            if not segs or segs[-1][1] != s[1]:
                segs.append([s[0], s[1], False])
            if s is r["at6"]:
                segs[-1][2] = True
        parts = [f"{'▸' if m else ''}{short(o)} {fmt_time(T, tz)}" for T, o, m in segs]
        P(f"{d} {d.strftime('%a')}  {len(r['waking'])} after 6a  │ {' → '.join(parts)}"
          f"   [actual: {short(r['actual'][0])}]")
    P()

    n = len(rows)
    def dist(key):
        c = Counter(len(r[key]) for r in rows)
        return "  ".join(f"{k}:{c[k]}" for k in sorted(c))
    stable = sum(1 for r in rows if not r["waking"])
    P("── Summary ─────────────────────────────────────────────")
    P(f"Days the outcome held all day from 6 AM:   {stable}/{n}")
    P(f"Changes after 6 AM, days by count:         {dist('waking')}")
    P(f"Changes incl. the evening preview:         {dist('all')}")
    P(f"Days showing 3+ different outcomes after 6 AM: {sum(1 for r in rows if r['distinct_waking'] >= 3)}")
    P(f"Total changes after 6 AM:                  {sum(len(r['waking']) for r in rows)}")
    P(f"Evening preview differed from 6 AM:        {sum(1 for r in rows if r['evening'][1] != r['at6'][1])}/{n}")
    P(f"Flip-backs (A → B → A after 6 AM):         "
      f"{sum(1 for r in rows for (a, _), (_, e) in zip(r['waking'], r['waking'][1:]) if a[1] == e[1])}")
    P()
    P("How good was the 6 AM call?  (actual = the engine on the archived, stitched day)")
    P(f"  6 AM outcome == actual:        {sum(1 for r in rows if r['at6'][1] == r['actual'][0])}/{n}")
    P(f"  end-of-day outcome == actual:  {sum(1 for r in rows if r['end'][1] == r['actual'][0])}/{n}")
    P(f"  evening preview == actual:     {sum(1 for r in rows if r['evening'][1] == r['actual'][0])}/{n}")
    P()

    pairs, causes = Counter(), Counter()
    for r in rows:
        for a, b in r["waking"]:
            pairs[f"{short(a[1])} → {short(b[1])}"] += 1
            for expr, *_ in why(outcomes, a[1], a[2], b[1], b[2]):
                causes[expr] += 1
    if pairs:
        P("Changes after 6 AM, by pair:")
        for k, v in pairs.most_common():
            P(f"  {v:3d}  {k}")
        P()
        P("Threshold crossed (a change can cross more than one):")
        for k, v in causes.most_common():
            P(f"  {v:3d}  {k}")
        P()
        P("Every change after 6 AM, with the numbers either side:")
        for r in rows:
            for a, b in r["waking"]:
                crossed = "; ".join(f"{f} {x:.0f}→{y:.0f} ({e})" for e, f, x, y in why(outcomes, a[1], a[2], b[1], b[2]))
                P(f"  {r['day']} {fmt_time(b[0], tz):>6}  {short(a[1])} → {short(b[1])}   {crossed}")
        P()

    if args.sensitivity:
        P("Sensitivity to the availability lag:")
        for lag in (2.0, 4.0, 6.0):
            w = World(runs, daily, actual, aqi, tz, lag)
            pd_, act_, _ = simulate(w, args.binary, args.spec, first, last, pin=not args.no_pin)
            rr = analyse(w, pd_, act_)
            P(f"  lag {lag:g}h: held all day {sum(1 for r in rr if not r['waking'])}/{len(rr)}, "
              f"changes after 6 AM {sum(len(r['waking']) for r in rr)}, "
              f"days with 3+ outcomes {sum(1 for r in rr if r['distinct_waking'] >= 3)}")


if __name__ == "__main__":
    main()
