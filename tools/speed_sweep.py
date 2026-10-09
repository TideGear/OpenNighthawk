#!/usr/bin/env python3
"""speed_sweep.py - what the emulated CPU's speed does to VGAME's flight.

    py tools/speed_sweep.py --data DIR --out DIR [--routes R ...] [--speeds 9 13 386 ...]
                            [--fix none D1 ...] [--seconds 166] [--jobs 24]

Flies each typed-input route (tools/routes/*.args) at each speed through the machine API. A speed
is millions of instructions a second, or `386` for the 386DX/33 cycle profile (src/cpu/timing386.h,
its clock 33,333,333 cycles a second). Front-end inputs are scaled to the emulated seconds they have
at 9 MIPS. The mission generated depends on the machine's speed below 9 MIPS and under the 386
profile (the table flags it); above 9 MIPS it is GOG's. VGAME's keys are timed from the first drawn frame, at the emulated second
into the flight they have at 9 MIPS (each route's 9 MIPS run is flown first to find that frame);
the quit keys (Alt+Q, Y) are dropped. The flight is watched for --seconds emulated seconds from the
first frame, the first 5 left out while S settles. Per run, from the flight engine's data segment:

  fps        frames drawn a second: the free-running frame counter [0x3D8E] (0x4359)
  S          [0x368E], the controller's frames-a-second estimate (0x441D, clamp at 0x0D441):
             mode, frame-weighted mean, histogram; `swings` counts falls from 13+ to 5 or less
  corr       [0x43E8], the controller's overrun correction (non-zero only once S was above 15)
  clock      the mission clock [0x9912] (once every S frames, 0x436C), per emulated second
  ticks      the game tick counter [0x2648] per second (what the controller counts)
  fps/S      frames drawn per frame S assumes: the per-frame world step against real time
  enemy      weapons fired at the player (slots 0-7 of the table at 0x3C3A): launches, proximity
             bursts (0x6F15: TTL set to 2S from above 4S; `decoyed` when [0x39E0] is set), misses
             (TTL ran out, hit the ground or gave up), each launch's closest approach in the
             game's own metric (0x6ED3); the flight block's launch count (+0x2C)
  damage     the player's damaged-systems mask [0x3664] and hit count [0xC5F4] (2 at start)
  events     the mission event log (DS:0xBA56, [0x951A] records of 6 bytes) by kind

Results go to OUT/runs.jsonl (one JSON object a run, re-used by later invocations unless --redo)
and a table on stdout.
"""
from __future__ import annotations

import argparse
import collections
import json
import multiprocessing as mp
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

GOG_IPS = 9_000_000
IPS_386 = 33_333_333
SETTLE = 5
FLIGHT_ROUTES = ("boot_to_flight", "middle_east_strike", "central_europe_airair", "korea_strike",
                 "kuwait_strike", "north_cape_strike", "central_america_ground_training",
                 "persian_gulf_air_training", "vietnam_airair")
FRONT_LIMIT = 12_000_000_000    # front-end clocks (at 9 MIPS) allowed to reach the first frame


def speed_ips(speed):
    return IPS_386 if speed == "386" else int(round(float(speed) * 1_000_000))


def front_scale(speed):
    """Front-end inputs keep the emulated seconds they have at 9 MIPS (keeping the clocks instead
    was tried below 9 MIPS: one speed in three never reached VGAME, and the missions still differed)."""
    return speed_ips(speed) / GOG_IPS


def route_args(route):
    path = route if os.path.isfile(route) else os.path.join(HERE, "routes", route + ".args")
    return [l.strip() for l in open(path) if l.strip() and not l.strip().startswith("#")]


def is_quit(program, content):
    return program == "VGAME.EXE" and (r"\aq" in content or content.strip().lower() == "y")


def sgn(v):
    return v - 0x10000 if v & 0x8000 else v


def fly(data, route, speed, fixes=(), seconds=166, engine="recomp", samples_path=None, first9=None):
    """One flight; returns its figures. Opens the machine in this process (one per process).
    `first9` is the first frame's clock offset from VGAME's start in this route's 9 MIPS run (None
    in that run itself)."""
    if speed == "386":
        os.environ["F117R_TIMING"] = "386"
    else:
        os.environ.pop("F117R_TIMING", None)
    from machine_api import Machine, RouteInputs
    ips = speed_ips(speed)
    scale = ips / GOG_IPS
    inputs = RouteInputs(route_args(route))
    flight_keys = [(at, o, c) for p, at, o, c in inputs.pending if p == "VGAME.EXE" and not is_quit(p, c)]
    inputs.pending = [(p, int(at * front_scale(speed)), o, c) for p, at, o, c in inputs.pending
                      if p != "VGAME.EXE"]
    step = int(90_000 * scale)
    samples, events, final, launches, live = [], [], {}, [], {}
    began = time.time()
    exited, first_frame, frame0 = None, None, None
    with tempfile.TemporaryDirectory() as save, \
            Machine(data, save, ips=ips, engine=engine, fixes=tuple(fixes)) as m:
        while m.clock < FRONT_LIMIT * front_scale(speed) + seconds * ips:
            inputs.poll(m)
            status = m.run_until(m.clock + step)
            if m.program.upper() != "VGAME.EXE":
                if first_frame is not None:
                    exited = "left VGAME for " + m.program
                    break
                if status != Machine.SLICE:
                    exited = "machine stopped before VGAME"
                    break
                continue
            ds = (m.psp + 0x10 + 0x1E42) << 4
            r = m.read16
            if first_frame is None:
                # The first drawn frame: [0x3D8E] steps by one with S in the controller's range
                # (the data segment holds load-time values before VGAME initialises it).
                now = r(ds + 0x3D8E) if 3 <= r(ds + 0x368E) <= 15 else None
                was, frame0 = frame0, now
                if was is None or now != (was + 1) & 0xFFFF:
                    continue
                first_frame = m.clock
                base = first9 if first9 is not None else first_frame - m.start
                for at, option, content in flight_keys:
                    when = first_frame + int((at - base) * scale)
                    if when <= m.clock:
                        when = m.clock + 1
                    hold = inputs.hold_ms
                    if content.startswith("~"):
                        d, content = content[1:].split(":", 1)
                        hold = int(d)
                    m.type(when, content, hold_ms=hold, gap_ms=inputs.hold_ms)
            t = m.clock - first_frame
            if t > seconds * ips:
                break
            S = r(ds + 0x368E)
            samples.append((t, S, r(ds + 0x9912), r(ds + 0x3D8E), r(ds + 0x2648),
                            r(ds + 0x43E8), r(ds + 0x3664), r(ds + 0xC5F4), r(ds + 0x951A)))
            # Weapons fired at the player: slots 0-7 of the table at 0x3C3A, 0x1C bytes each.
            px, py, pz = r(ds + 0xC0D0), r(ds + 0xC0DE), r(ds + 0x2DF4)
            for slot in range(8):
                w = ds + 0x3C3A + slot * 0x1C
                ttl = max(0, sgn(r(w + 0x0E)))       # a free slot reads 0 or below
                rec = live.get(slot)
                if ttl and rec is None:
                    rec = live[slot] = dict(t=round(t / ips, 2), slot=slot, type=r(w + 0x10),
                                            owner=sgn(r(w + 0x16)), S=S, ttl0=ttl, min_slant=None,
                                            outcome=None, prev=ttl)
                    launches.append(rec)
                if rec is None:
                    continue
                if ttl:
                    dx, dy = abs(sgn((px - r(w)) & 0xFFFF)), abs(sgn((py - r(w + 2)) & 0xFFFF))
                    slant = max(dx, dy) + min(dx, dy) // 2 + (abs(sgn((pz - r(w + 4)) & 0xFFFF)) >> 5)
                    if rec["min_slant"] is None or slant < rec["min_slant"]:
                        rec["min_slant"] = slant
                    if rec["outcome"] is None and ttl == 2 * S and rec["prev"] > 4 * S:
                        rec["outcome"] = "decoyed" if r(ds + 0x39E0) else "burst"
                        rec["burst_t"] = round(t / ips, 2)
                    rec["prev"] = ttl
                else:
                    if rec["outcome"] is None:
                        rec["outcome"] = "miss"
                    rec["end_t"] = round(t / ips, 2)
                    del live[slot]
            fb = (r(ds + 0xE576) << 4) + r(ds + 0xE574)
            tgt = r(ds + 0xE306)
            final = dict(objective=r(ds + 0xE304), target=tgt, ejection=r(ds + 0xC09A),
                         exit=m.read8(ds + 0xE57E), reload=r(ds + 0x2632), divider=r(ds + 0x2636),
                         quality=r(ds + 0x3686), launched_at_player=r(fb + 0x2C),
                         target_xy=[r(ds + 0xB2CE + tgt * 16 + 2), r(ds + 0xB2CE + tgt * 16 + 4)],
                         first_frame=first_frame - m.start)
            events = [(m.read8(ds + 0xBA5A + i * 6), m.read8(ds + 0xBA5B + i * 6))
                      for i in range(min(r(ds + 0x951A), 255))]
            if status != Machine.SLICE:
                exited = "machine stopped in VGAME"
                break
        final_hash = f"{m.hash:016x}"
    for rec in launches:
        rec.pop("prev", None)
        if rec["outcome"] is None:
            rec["outcome"] = "in flight"
    if samples_path:
        with open(samples_path, "w") as f:
            f.write("t,S,clock,frame,tick,corr,damage,hits,events\n")
            for s in samples:
                f.write(",".join(map(str, s)) + "\n")
    flight = [s for s in samples if s[0] > SETTLE * ips]
    result = dict(route=route, speed=str(speed), ips=ips, fixes=list(fixes), seconds_asked=seconds,
                  exited=exited, hash=final_hash, wall=round(time.time() - began, 1), **final)
    if len(flight) < 2:
        result["error"] = "VGAME was not reached or not flown"
        return result
    secs = (flight[-1][0] - flight[0][0]) / ips

    def advance(i):
        return sum((b[i] - a[i]) & 0xFFFF for a, b in zip(flight, flight[1:]))

    s_hist = collections.Counter(s[1] for s in flight)
    swings = sum(a[1] >= 13 and b[1] <= 5 for a, b in zip(flight, flight[1:]))
    changes = sum(a[1] != b[1] for a, b in zip(flight, flight[1:]))
    frames, clock, ticks = advance(3), advance(2), advance(4)
    s_frames = collections.Counter()          # S weighted by frames: the divisor each frame used
    for a, b in zip(flight, flight[1:]):
        s_frames[a[1]] += (b[3] - a[3]) & 0xFFFF
    s_mean = sum(k * v for k, v in s_frames.items()) / max(1, sum(s_frames.values()))
    outcomes = collections.Counter(l["outcome"] for l in launches)
    result.update(
        flight_seconds=round(secs, 2), fps=round(frames / secs, 3), S_mode=s_hist.most_common(1)[0][0],
        S_mean=round(s_mean, 3), S_hist={str(k): v for k, v in sorted(s_hist.items())},
        corr_hist={str(k): v for k, v in sorted(collections.Counter(s[5] for s in flight).items())},
        swings=swings, S_changes=changes, clock=clock, clock_per_s=round(clock / secs, 4),
        ticks_per_s=round(ticks / secs, 3), fps_over_S=round(frames / secs / s_mean, 4) if s_mean else None,
        damage_mask=samples[-1][6], damage_hits=samples[-1][7],
        damage_first=next((round(s[0] / ips, 1) for s in samples
                           if s[6] != samples[0][6] or s[7] != samples[0][7]), None),
        enemy_launches=len(launches), enemy_bursts=outcomes["burst"], enemy_decoyed=outcomes["decoyed"],
        enemy_misses=outcomes["miss"], launch_list=launches,
        events=dict(collections.Counter(f"{k:02x}" for k, _ in events)), event_list=events)
    return result


def tag_of(route, speed, fixes):
    return f"{route}@{speed}{'+' + '+'.join(fixes) if fixes else ''}"


def _job(args):
    data, route, speed, fixes, seconds, engine, out, first9 = args
    tag = tag_of(route, speed, fixes)
    try:
        r = fly(data, route, speed, fixes, seconds, engine,
                os.path.join(out, "samples", tag.replace("@", "_") + ".csv"), first9)
    except Exception as e:  # noqa: BLE001 - one failed run must not stop the sweep
        r = dict(route=route, speed=str(speed), fixes=list(fixes), error=repr(e))
    r["tag"] = tag
    print(f"done {tag}: fps {r.get('fps')} S {r.get('S_hist')} clock/s {r.get('clock_per_s')} "
          f"enemy {r.get('enemy_launches')}/{r.get('enemy_bursts')} wall {r.get('wall')} "
          f"{r.get('error') or r.get('exited') or ''}", flush=True)
    return r


def table(rows):
    print(f"{'route':<32}{'speed':>6}{'fix':>4}{'fps':>7}{'S':>4}{'Smean':>6}{'swg':>4}{'chg':>4}"
          f"{'clk/s':>7}{'fps/S':>7}{'tick/s':>7}{'vs9':>6} {'L/B/D/M':>11} dmg  mission")
    ref = {r["route"]: r for r in rows if r.get("speed") == "9" and not r.get("fixes") and "fps" in r}
    for r in sorted(rows, key=lambda r: (r["route"], tuple(r.get("fixes", ())),
                                        1 if r["speed"] == "386" else speed_ips(r["speed"]))):
        if "fps" not in r:
            print(f"{r['route']:<32}{r['speed']:>6} ERROR {r.get('error')}")
            continue
        base = ref.get(r["route"])
        ratio = r["clock_per_s"] / base["clock_per_s"] if base else float("nan")
        same = "" if not base or (base.get("objective"), base.get("target_xy")) == \
            (r.get("objective"), r.get("target_xy")) else "DIFFERENT MISSION "
        print(f"{r['route']:<32}{r['speed']:>6}{'+'.join(r['fixes']) or '-':>4}{r['fps']:>7.2f}{r['S_mode']:>4}"
              f"{r['S_mean']:>6.2f}{r['swings']:>4}{r['S_changes']:>4}{r['clock_per_s']:>7.3f}"
              f"{r['fps_over_S'] or 0:>7.3f}{r['ticks_per_s']:>7.2f}{ratio:>6.3f} "
              f"{r['enemy_launches']:>3}/{r['enemy_bursts']}/{r['enemy_decoyed']}/{r['enemy_misses']:<3}"
              f"{r['damage_hits'] - 2:>4}  {same}{r.get('exited') or ''}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--routes", nargs="+", default=list(FLIGHT_ROUTES))
    ap.add_argument("--speeds", nargs="+", default=["9"])
    ap.add_argument("--fix", nargs="+", default=["none"], help="fix sets: none, D1, or D1+D5 ...")
    ap.add_argument("--seconds", type=int, default=166)
    ap.add_argument("--engine", default="recomp", choices=("recomp", "interp"))
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 8) - 8))
    ap.add_argument("--redo", action="store_true")
    ap.add_argument("--table-only", action="store_true")
    a = ap.parse_args()
    os.makedirs(os.path.join(a.out, "samples"), exist_ok=True)
    store = os.path.join(a.out, "runs.jsonl")
    done = {}
    if os.path.exists(store):
        for line in open(store):
            r = json.loads(line)
            done[r["tag"]] = r
    wanted = [(route, speed, () if fs == "none" else tuple(fs.split("+")))
              for route in a.routes for speed in a.speeds for fs in a.fix]

    def todo(route, speed, fixes):
        r = done.get(tag_of(route, speed, fixes))
        return not a.table_only and (a.redo or r is None or "error" in r)

    def run(jobs):
        if not jobs:
            return
        ctx = mp.get_context("spawn")
        with ctx.Pool(min(a.jobs, len(jobs)), maxtasksperchild=1) as pool, open(store, "a") as f:
            for r in pool.imap_unordered(_job, jobs):
                done[r["tag"]] = r
                f.write(json.dumps(r) + "\n")
                f.flush()

    # Each route's 9 MIPS flight first: the others time their flight keys from its first frame.
    run([(a.data, route, "9", (), a.seconds, a.engine, a.out, None)
         for route in a.routes if todo(route, "9", ())])
    first9 = {route: done.get(tag_of(route, "9", ()), {}).get("first_frame") for route in a.routes}
    run([(a.data, route, speed, fixes, a.seconds, a.engine, a.out, first9[route])
         for route, speed, fixes in wanted if (speed, fixes) != ("9", ()) and todo(route, speed, fixes)
         and first9[route] is not None])
    table([done[tag_of(*w)] for w in wanted if tag_of(*w) in done])
    return 0


if __name__ == "__main__":
    sys.exit(main())
