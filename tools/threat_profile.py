#!/usr/bin/env python3
"""threat_profile.py - combat exposure under adaptive control, orbiting the primary target.

    py tools/threat_profile.py --data DIR --out DIR [--routes R ...] [--speeds 9 16 20 ...]
                               [--offsets-ms 0 3000 ...] [--seconds 600] [--jobs 24]

The nine typed routes used by speed_sweep.py fly a fixed scripted path that passes near the
primary target once (or not at all) and then holds a straight heading for the rest of the window;
most draw no enemy launches at all (docs/speed-sweep.md, "What this does not establish"). This
tool instead drives VGAME with live telemetry, reusing strike_pilot.py's normal-control approach
to steer toward the primary target for as much of --seconds as that takes (the target is often
tens of thousands of map units from the runway, so this can be most of the flight); once within
ORBIT_RANGE of it, or on primary credit, it switches to circling the target's coordinates at a
fixed radius and altitude for the rest of the flight. Repeated passes through whatever is
defending the target give far more opportunities for an enemy launch than one pass-through,
without reading or assuming any enemy site's position: the orbit is centred on the
(already-visible) primary target, the same point the scripted routes also fly toward.

Each ROUTE is an existing tools/routes/*.args file (its front-end clicks are reused verbatim,
scaled like speed_sweep's front_scale; any scripted VGAME.EXE keys in it are dropped since this
tool drives the flight itself). The weapons-at-player telemetry (slots 0-7 of the table at 0x3C3A:
launches, proximity bursts, decoys, misses) is the same reading speed_sweep.py uses. Results go to
OUT/runs.jsonl (reused unless --redo) and a table on stdout.
"""
from __future__ import annotations

import argparse
import collections
import json
import math
import multiprocessing as mp
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

GOG_IPS = 9_000_000
IPS_386 = 33_333_333
BOOT_US = 700_000_000_000_000
ORBIT_ROUTES = ("middle_east_strike", "korea_strike", "kuwait_strike", "north_cape_strike")
ORBIT_RADIUS = 6000
ORBIT_CRUISE = 4500            # above strike_pilot's normal 2500: the orbit visits terrain near the
                                # target the scripted/direct routes never do (north_cape_strike hit
                                # the ground climbing at 2500 orbiting a mountainous target)
ORBIT_RANGE = 6000             # switch from approach to orbit once this close to the target, or on credit


def speed_ips(speed):
    return IPS_386 if speed == "386" else int(round(float(speed) * 1_000_000))


def front_scale(speed):
    """Keep the front-end's emulated seconds at 9 MIPS, as speed_sweep.py does."""
    return speed_ips(speed) / GOG_IPS


def route_args(route):
    path = route if os.path.isfile(route) else os.path.join(HERE, "routes", route + ".args")
    return [l.strip() for l in open(path) if l.strip() and not l.strip().startswith("#")]


def signed(v):
    return v - 0x10000 if v & 0x8000 else v


def orbit_control(machine, state, tick, target_x, target_y, radius=ORBIT_RADIUS, cruise=ORBIT_CRUISE):
    """Fly a circle around (target_x, target_y): a tangent heading, biased inward or outward by
    the radius error, so repeated passes stay near the target without needing its defences' own
    positions."""
    dx = signed(state["x"] - target_x)
    dy = signed(state["y"] - target_y)
    current_radius = math.hypot(dx, dy) or 1.0
    bearing_from_target = math.atan2(dx, -dy) * 32768 / math.pi
    correction = max(-12000, min(12000, (current_radius - radius) * 4))
    desired_heading = bearing_from_target + 16384 - correction
    heading_error = signed(int(desired_heading) - state["heading"])
    bank = max(-6000, min(6000, heading_error * 1.5))
    roll_error = bank - state["roll"]
    want_pitch = max(-1000, min(1800, (cruise - state["altitude"]) * 2)) + state["trim"]
    pitch_error = want_pitch - state["pitch"]
    at = machine.clock + 1
    if abs(pitch_error) > 200:
        machine.type(at, r"\D" if pitch_error > 0 else r"\U", hold_ms=60)
    if abs(roll_error) > 300:
        machine.type(at + machine.ips // 10, r"\R" if roll_error > 0 else r"\L", hold_ms=60)
    if tick % 10 == 0:
        if state["throttle"] < 70:
            machine.type(at + machine.ips * 17 // 100, "=", hold_ms=20)
        elif state["speed"] > 380 and state["throttle"] > 55:
            machine.type(at + machine.ips * 17 // 100, "-", hold_ms=20)


def fly(data, route, speed, seconds=600, offset_ms=0, radius=ORBIT_RADIUS, cruise=ORBIT_CRUISE,
        samples_path=None, engine="recomp", fixes=()):
    if speed == "386":
        os.environ["F117R_TIMING"] = "386"
    else:
        os.environ.pop("F117R_TIMING", None)
    from machine_api import Machine, RouteInputs
    from strike_pilot import strike_state, control as strike_control

    ips = speed_ips(speed)
    inputs = RouteInputs(route_args(route))
    inputs.pending = [(p, int(at * front_scale(speed)), o, c) for p, at, o, c in inputs.pending
                      if p != "VGAME.EXE"]
    samples, live, launches = [], {}, []
    began = time.time()
    exited, flight_start, initialized, orbiting, target_xy = None, None, False, False, None
    orbit_start = None
    tick = 0
    with tempfile.TemporaryDirectory() as save, \
            Machine(data, save, ips=ips, engine=engine, fixes=tuple(fixes),
                    time_us=BOOT_US + offset_ms * 1000) as m:
        step = 90_000
        while True:
            inputs.poll(m)
            status = m.run_until(m.clock + step)
            if m.program.upper() != "VGAME.EXE":
                if flight_start is not None:
                    exited = "left VGAME for " + m.program
                    break
                if status != Machine.SLICE:
                    exited = "machine stopped before VGAME"
                    break
                continue
            if flight_start is None:
                flight_start = m.start
            elapsed = m.clock - flight_start
            if elapsed > seconds * ips:
                break
            ds = (m.psp + 0x10 + 0x1E42) << 4
            r = m.read16
            if not initialized and elapsed > 40_000_000:
                state = strike_state(m)
                if state["flags"] & 8:
                    m.type(flight_start + 80_000_000, "0")
                m.type(flight_start + 100_000_000, "+")
                m.type(flight_start + 170_000_000, r"\D", hold_ms=1000)
                initialized = True
            if elapsed > 190_000_000:
                state = strike_state(m)
                if target_xy is None:
                    target_xy = (state["target_x"], state["target_y"])
                if not orbiting and (state["target_range"] < ORBIT_RANGE or state["flags"] & 0x4000):
                    orbiting = True
                    orbit_start = elapsed
                if orbiting:
                    orbit_control(m, state, tick, *target_xy, radius=radius, cruise=cruise)
                else:
                    strike_control(m, state, tick)
                step = ips // 5
                tick += 1
                samples.append((elapsed, state["x"], state["y"], state["altitude"],
                                state["flags"] & 0x4000 != 0))
                # Weapons fired at the player: the same slot table speed_sweep.py reads.
                S = r(ds + 0x368E)
                px, py, pz = r(ds + 0xC0D0), r(ds + 0xC0DE), r(ds + 0x2DF4)
                for slot in range(8):
                    w = ds + 0x3C3A + slot * 0x1C
                    ttl = max(0, signed(r(w + 0x0E)))
                    rec = live.get(slot)
                    if ttl and rec is None:
                        rec = live[slot] = dict(t=round(elapsed / ips, 2), slot=slot, type=r(w + 0x10),
                                                owner=signed(r(w + 0x16)), S=S, ttl0=ttl, min_slant=None,
                                                outcome=None, prev=ttl)
                        launches.append(rec)
                    if rec is None:
                        continue
                    if ttl:
                        dx, dy = abs(signed((px - r(w)) & 0xFFFF)), abs(signed((py - r(w + 2)) & 0xFFFF))
                        slant = max(dx, dy) + min(dx, dy) // 2 + (abs(signed((pz - r(w + 4)) & 0xFFFF)) >> 5)
                        if rec["min_slant"] is None or slant < rec["min_slant"]:
                            rec["min_slant"] = slant
                        if rec["outcome"] is None and ttl == 2 * S and rec["prev"] > 4 * S:
                            rec["outcome"] = "decoyed" if r(ds + 0x39E0) else "burst"
                            rec["burst_t"] = round(elapsed / ips, 2)
                        rec["prev"] = ttl
                    else:
                        if rec["outcome"] is None:
                            rec["outcome"] = "miss"
                        rec["end_t"] = round(elapsed / ips, 2)
                        del live[slot]
            if status != Machine.SLICE:
                exited = "machine stopped in VGAME"
                break
        final_hash = f"{m.hash:016x}"
        in_vgame = flight_start is not None and m.program.upper() == "VGAME.EXE"
        final_state = strike_state(m) if in_vgame else {}
        damage_hits = m.read16(((m.psp + 0x10 + 0x1E42) << 4) + 0xC5F4) if in_vgame else None
    for rec in launches:
        rec.pop("prev", None)
        if rec["outcome"] is None:
            rec["outcome"] = "in flight"
    if samples_path:
        with open(samples_path, "w") as f:
            f.write("elapsed,x,y,altitude,credit\n")
            for s in samples:
                f.write(",".join(map(str, s)) + "\n")
    outcomes = collections.Counter(l["outcome"] for l in launches)
    result = dict(route=route, speed=str(speed), ips=ips, seconds_asked=seconds, offset_ms=offset_ms,
                  radius=radius, cruise=cruise, exited=exited, hash=final_hash,
                  wall=round(time.time() - began, 1), target_xy=target_xy,
                  orbit_seconds=round((samples[-1][0] - orbit_start) / ips, 1)
                  if orbit_start is not None and samples else 0,
                  objective=final_state.get("objective_type"), credit=bool(final_state.get("flags", 0) & 0x4000),
                  enemy_launches=len(launches), enemy_bursts=outcomes["burst"],
                  enemy_decoyed=outcomes["decoyed"], enemy_misses=outcomes["miss"],
                  damage_hits=damage_hits, launch_list=launches)
    if len(samples) < 2:
        result["error"] = "VGAME was not reached or not flown"
    return result


def tag_of(route, speed, offset_ms):
    return f"{route}@{speed}~t{offset_ms}"


def _job(args):
    data, route, speed, seconds, offset_ms, out = args
    tag = tag_of(route, speed, offset_ms)
    try:
        r = fly(data, route, speed, seconds, offset_ms,
                samples_path=os.path.join(out, "samples", tag.replace("@", "_").replace("~", "_") + ".csv"))
    except Exception as e:  # noqa: BLE001 - one failed flight must not stop the sweep
        r = dict(route=route, speed=str(speed), offset_ms=offset_ms, error=repr(e))
    r["tag"] = tag
    print(f"done {tag}: orbit {r.get('orbit_seconds')}s enemy {r.get('enemy_launches')}/"
          f"{r.get('enemy_bursts')}/{r.get('enemy_misses')} credit {r.get('credit')} "
          f"wall {r.get('wall')} {r.get('error') or r.get('exited') or ''}", flush=True)
    return r


def table(rows):
    print(f"{'route':<22}{'speed':>6}{'orbit s':>8}{'L/B/D/M':>12}{'credit':>7}  mission")
    for r in sorted(rows, key=lambda r: (r["route"], 1 if r["speed"] == "386" else speed_ips(r["speed"]))):
        if "enemy_launches" not in r:
            print(f"{r['route']:<22}{r['speed']:>6} ERROR {r.get('error')}")
            continue
        print(f"{r['route']:<22}{r['speed']:>6}{r.get('orbit_seconds', 0):>8.1f}"
              f"{r['enemy_launches']:>3}/{r['enemy_bursts']}/{r['enemy_decoyed']}/{r['enemy_misses']:<3}"
              f"{str(r.get('credit')):>7}  {r.get('exited') or ''}")
    by_speed = collections.defaultdict(list)
    for r in rows:
        if "enemy_launches" in r:
            by_speed[r["speed"]].append(r)
    print()
    print(f"{'speed':>6}{'n':>4}{'mean launches':>16}{'mean bursts':>14}{'any hit':>10}")
    for speed, rs in sorted(by_speed.items(), key=lambda kv: 1 if kv[0] == "386" else speed_ips(kv[0])):
        n = len(rs)
        print(f"{speed:>6}{n:>4}{sum(r['enemy_launches'] for r in rs) / n:>16.3f}"
              f"{sum(r['enemy_bursts'] for r in rs) / n:>14.3f}"
              f"{sum(r['enemy_bursts'] > 0 for r in rs):>6} of {n}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--routes", nargs="+", default=list(ORBIT_ROUTES))
    ap.add_argument("--speeds", nargs="+", default=["9"])
    ap.add_argument("--seconds", type=int, default=600, help="total flight time watched, approach and orbit together")
    ap.add_argument("--offsets-ms", nargs="+", type=int, default=[0])
    ap.add_argument("--radius", type=int, default=ORBIT_RADIUS)
    ap.add_argument("--cruise", type=int, default=ORBIT_CRUISE)
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
    wanted = [(route, speed, off) for route in a.routes for speed in a.speeds for off in a.offsets_ms]
    jobs = [(a.data, route, speed, a.seconds, off, a.out) for route, speed, off in wanted
            if not a.table_only and (a.redo or tag_of(route, speed, off) not in done
                                     or "error" in done[tag_of(route, speed, off)])]
    if jobs:
        ctx = mp.get_context("spawn")
        with ctx.Pool(min(a.jobs, len(jobs)), maxtasksperchild=1) as pool, open(store, "a") as f:
            for r in pool.imap_unordered(_job, jobs):
                done[r["tag"]] = r
                f.write(json.dumps(r) + "\n")
                f.flush()
    table([done[tag_of(*w)] for w in wanted if tag_of(*w) in done])
    return 0


if __name__ == "__main__":
    sys.exit(main())
