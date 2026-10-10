#!/usr/bin/env python3
"""Normal-input clock, pause and 2x-compression check for optional D1REAL."""
import argparse
import json
import tempfile

from machine_api import Machine, RouteInputs
from threat_profile import front_scale, route_args


def check(data, speed, engine):
    ips = int(speed) * 1_000_000
    inputs = RouteInputs(route_args("boot_to_flight"))
    inputs.pending = [(p, int(at * front_scale(speed)), o, c)
                      for p, at, o, c in inputs.pending if p != "VGAME.EXE"]
    cases, errors = [], []
    with tempfile.TemporaryDirectory() as save, Machine(
            data, save, ips=ips, engine=engine, fixes=("D1REAL",)) as m:
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
            expected_frames = 0 if world_rate == 0 else seconds * 8
            if (abs(delta[0] - expected_frames) > tolerance
                    or abs(delta[1] - seconds * world_rate) > tolerance or seen != {S}):
                errors.append(row)

        observe("normal", 30, 8, 1)
        m.type(m.clock + 1, "Z", hold_ms=40, gap_ms=40)
        m.run_until(m.clock + ips)
        observe("compressed", 10, 4, 2)
        m.type(m.clock + 1, "X", hold_ms=40, gap_ms=40)
        m.run_until(m.clock + ips)
        observe("normal-restored", 10, 8, 1)
        m.type(m.clock + 1, r"\ap", hold_ms=40, gap_ms=40)
        m.run_until(m.clock + ips)
        observe("paused", 3, 8, 0, tolerance=0)
        m.type(m.clock + 1, " ", hold_ms=40, gap_ms=40)
        observe("resumed", 3, 8, 1, tolerance=2)
        m.type(m.clock + 1, r"\aq", hold_ms=40, gap_ms=40)
        m.run_until(m.clock + ips)
        observe("quit-dialog", 3, 8, 0, tolerance=0)
        m.type(m.clock + 1, "n", hold_ms=40, gap_ms=40)
        observe("quit-cancelled", 3, 8, 1, tolerance=2)
    return dict(speed=speed, engine=engine, staged=False, cases=cases, errors=errors)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data", required=True)
    p.add_argument("--speed", choices=("9", "20", "40"), default="20")
    p.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    args = p.parse_args()
    r = check(args.data, args.speed, args.engine)
    print(json.dumps(r, indent=2))
    raise SystemExit(bool(r["errors"]))


if __name__ == "__main__":
    main()
