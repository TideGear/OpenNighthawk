#!/usr/bin/env python3
"""Normal-input clock, pause and 2x-compression check for optional D1REAL."""
import argparse
import json
import tempfile

from machine_api import Machine, RouteInputs
from threat_profile import front_scale, route_args


def check(data, speed, engine, sim_hz=8, fixes=("D1REAL",), transitions=False):
    ips = int(speed) * 1_000_000
    inputs = RouteInputs(route_args("boot_to_flight"))
    inputs.pending = [(p, int(at * front_scale(speed)), o, c)
                      for p, at, o, c in inputs.pending if p != "VGAME.EXE"]
    cases, errors = [], []
    with tempfile.TemporaryDirectory() as save, Machine(
            data, save, ips=ips, engine=engine, fixes=fixes, sim_hz=sim_hz) as m:
        while True:
            inputs.poll(m)
            if m.run_until(m.clock + ips // 100) != Machine.SLICE:
                raise RuntimeError("machine stopped before flight")
            if m.program == "VGAME.EXE" and m.clock - m.start > 30 * ips:
                break
            if m.clock > 1000 * ips:
                raise RuntimeError("flight boot deadline")
        ds = (m.psp + 0x10 + 0x1E42) << 4

        def observe(name, seconds, S, world_rate, tolerance=1):
            before = [m.read16(ds + a) for a in (0x3D8E, 0x9912)]
            until = m.clock + seconds * ips
            seen = set()
            while m.clock < until:
                if m.run_until(min(until, m.clock + ips // 100)) != Machine.SLICE:
                    raise RuntimeError("machine stopped during " + name)
                seen.add(m.read16(ds + 0x368E))
            delta = [(m.read16(ds + a) - b) & 65535
                     for a, b in zip((0x3D8E, 0x9912), before)]
            row = dict(name=name, seconds=seconds, frames=delta[0],
                       mission_seconds=delta[1], S=sorted(seen), hash=f"{m.hash:016x}")
            cases.append(row)
            expected_frames = (0 if world_rate == 0 else
                               seconds * (S * world_rate if sim_hz == 15 else 8))
            if (abs(delta[0] - expected_frames) > tolerance
                    or abs(delta[1] - seconds * world_rate) > tolerance or seen != {S}):
                errors.append(row)

        normal_S, compressed_S = sim_hz, 15 if sim_hz == 15 else 4
        observe("normal", 30, normal_S, 1)
        m.type(m.clock + 1, "Z", hold_ms=40, gap_ms=40)
        m.run_until(m.clock + ips)
        observe("compressed", 70 if sim_hz == 15 else 10, compressed_S, 2)
        m.type(m.clock + 1, "X", hold_ms=40, gap_ms=40)
        m.run_until(m.clock + ips)
        observe("normal-restored", 10, normal_S, 1)
        m.type(m.clock + 1, r"\ap", hold_ms=40, gap_ms=40)
        m.run_until(m.clock + ips)
        observe("paused", 3, normal_S, 0, tolerance=0)
        m.type(m.clock + 1, " ", hold_ms=40, gap_ms=40)
        observe("resumed", 3, normal_S, 1, tolerance=2)
        m.type(m.clock + 1, r"\aq", hold_ms=40, gap_ms=40)
        m.run_until(m.clock + ips)
        observe("quit-dialog", 3, normal_S, 0, tolerance=0)
        m.type(m.clock + 1, "n", hold_ms=40, gap_ms=40)
        observe("quit-cancelled", 3, normal_S, 1, tolerance=2)
        if transitions:
            for cycle in range(6):
                for key, S, rate in (("Z", compressed_S, 2), ("X", normal_S, 1)):
                    m.type(m.clock + 1, key, hold_ms=40, gap_ms=40)
                    m.run_until(m.clock + ips)
                    observe(f"transition-{cycle}-{key}", 5, S, rate)
            start = m.clock
            for n in range(60):
                m.type(start + ips * (n * 800 + 200) // 1000, "Z", hold_ms=40, gap_ms=40)
                m.type(start + ips * (n * 800 + 600) // 1000, "X", hold_ms=40, gap_ms=40)
            before, expected, changes, last = m.read16(ds + 0x9912), 0., 0, 1
            until = start + 48 * ips
            while m.clock < until:
                old, mode = m.clock, m.read16(ds + 0x3DA8)
                if m.run_until(min(until, old + ips // 1000)) != Machine.SLICE:
                    raise RuntimeError("machine stopped during rapid transitions")
                expected += (m.clock - old) / ips * mode
                mode = m.read16(ds + 0x3DA8)
                if mode != last:
                    changes += 1
                    last = mode
            delta = (m.read16(ds + 0x9912) - before) & 65535
            row = dict(name="rapid-transitions", changes=changes, mission_seconds=delta,
                       expected=expected, error=delta - expected, hash=f"{m.hash:016x}")
            cases.append(row)
            if changes != 120 or abs(delta - expected) > 1.25:
                errors.append(row)
    return dict(speed=speed, engine=engine, sim_hz=sim_hz, fixes=list(fixes),
                staged=False, cases=cases, errors=errors)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data", required=True)
    p.add_argument("--speed", choices=("9", "20", "40"), default="20")
    p.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    p.add_argument("--sim-hz", type=int, choices=(8, 15), default=8)
    p.add_argument("--all-fixes", action="store_true")
    p.add_argument("--transitions", action="store_true")
    args = p.parse_args()
    r = check(args.data, args.speed, args.engine, args.sim_hz,
              ("all",) if args.all_fixes else ("D1REAL",), args.transitions)
    print(json.dumps(r, indent=2))
    raise SystemExit(bool(r["errors"]))


if __name__ == "__main__":
    main()
