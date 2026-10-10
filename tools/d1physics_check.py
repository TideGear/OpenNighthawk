#!/usr/bin/env python3
"""Staged S-dependent weapon physics, after normal boot at 20 MIPS.

Acceleration uses the SA-5's normal initial1/terminal28 speeds. Proximity
uses an SA-12 with heading and speed held constant, comparing a complete
straight pass at S4/8/9/15. Guest state and table steering/speed are staged;
these are isolated physics checks, not natural combat or balance evidence.

    py tools/d1physics_check.py --data DIR --fix D1PROX --fix D1ACCEL
"""
import argparse
import json
import tempfile

from machine_api import Machine, RouteInputs
from threat_profile import front_scale, route_args


def check(data, engine="recomp", fixes=()):
    ips = 20_000_000
    inputs = RouteInputs(route_args("boot_to_flight"))
    inputs.pending = [(p, int(at * front_scale("20")), o, c)
                      for p, at, o, c in inputs.pending if p != "VGAME.EXE"]
    results, errors = [], []
    accel = "D1ACCEL" in fixes or "all" in fixes
    prox = "D1PROX" in fixes or "all" in fixes
    with tempfile.TemporaryDirectory() as save, \
            Machine(data, save, ips=ips, engine=engine, fixes=fixes) as m:
        while True:
            inputs.poll(m)
            if m.run_until(m.clock + 90_000) != Machine.SLICE:
                raise RuntimeError("machine stopped before staging")
            if m.program == "VGAME.EXE" and m.clock - m.start > 600_000_000:
                break
            if m.clock > 10_000_000_000:
                raise RuntimeError("front end did not reach flight")
        ds = (m.psp + 0x10 + 0x1E42) << 4
        base = ds + 0x3C3A

        def stage(S, slot, type_, speed, dx, dy, dz):
            for i in range(12):
                m.stage_write16(base + i * 28 + 14, 0)
            m.stage_write16(ds + 0x368E, S)
            m.stage_write16(ds + 0x3DA6, 0)
            m.stage_write16(ds + 0x39E0, 0)
            m.stage_write16(ds + 0x9B34, m.read16(ds + 0x9B34) & ~0x1030)
            m.stage_write16(ds + 0x3664, 0)
            m.stage_write16(ds + 0x3D90, (m.read16(ds + 0x9F94) + 100) & 65535)
            m.stage_write16(ds + 0x33A2 + type_ * 18, 0)
            values = (m.read16(ds + 0xC0D0) + dx, m.read16(ds + 0xC0DE) + dy,
                      m.read16(ds + 0x2DF4) + dz, speed, 0, 0, 0, 1000, type_, 0, 0, 3, 0, 0)
            w = base + slot * 28
            for i, value in enumerate(values):
                m.stage_write16(w + 2 * i, value & 65535)
            return w

        def advance(S, w, steps):
            start_life, clock = m.read16(w + 14), m.clock
            while start_life - m.read16(w + 14) < steps:
                m.stage_write16(ds + 0x368E, S)
                m.stage_write16(ds + 0x3DA6, 0)
                if m.run_until(m.clock + ips // 1000) != Machine.SLICE:
                    raise RuntimeError("machine stopped during acceleration")
                if m.clock - clock > 10 * ips:
                    raise RuntimeError("acceleration frame deadline")
            return start_life - m.read16(w + 14)

        # Controls precede every case that changes behavior, so their hashes
        # can be compared directly against a separate options-off boot.
        for S, slot, type_ in ((9, 0, 2), (15, 8, 0x1D), (4, 0, 2), (8, 0, 2), (15, 0, 2), (15, 7, 2)):
            w = stage(S, slot, type_, 1, 0, 10000, 2000)
            frame = m.read16(ds + 0x3D8E)
            steps = advance(S, w, 2 * S)
            expected = 10 if accel else 1 + S
            r = dict(kind="acceleration", S=S, slot=slot, type=type_, steps=steps,
                     frames=(m.read16(ds + 0x3D8E) - frame) & 65535,
                     speed=m.read16(w + 6), hash=f"{m.hash:016x}")
            results.append(r)
            if steps != 2 * S or (slot < 8 and r["speed"] != expected):
                errors.append(f"acceleration mismatch, expected {expected}: {r}")

        w = stage(8, 0, 2, 1, 0, 10000, 2000)
        steps = [advance(8, w, 8), advance(15, w, 15)]
        r = dict(kind="acceleration-changing-S", steps=steps, speed=m.read16(w + 6), hash=f"{m.hash:016x}")
        results.append(r)
        allowed = (10,) if accel else (12, 13)
        if steps != [8, 15] or r["speed"] not in allowed:
            errors.append(f"changing-S acceleration mismatch: {r}")

        # Freeze both agility and terminal speed: neither steering nor
        # acceleration can account for differences in the pass results.
        m.stage_write16(ds + 0x339E + 6 * 18, 28 << 6)
        passes = [(S, distance, 0) for S in (4, 8, 9, 15) for distance in (20, 22, 28)]
        passes += [(15, 20, 7), (15, 22, 7)]
        for S, distance, slot in passes:
            w = stage(S, slot, 6, 28, distance, 70, 100)
            before = m.read16(ds + 0xC5F4)
            for _ in range(5):
                m.stage_write16(ds + 0x368E, S)
                m.stage_write16(ds + 0x3DA6, 0)
                if m.run_until(m.clock + ips // 5) != Machine.SLICE:
                    raise RuntimeError("machine stopped during proximity pass")
            hit = ((m.read16(ds + 0xC5F4) - before) & 65535) != 0
            expected = distance == 20 if prox else (S == 4 or (S == 8 and distance < 28))
            r = dict(kind="proximity", S=S, slot=slot, distance=distance, hit=hit,
                     speed=m.read16(w + 6), life=m.read16(w + 14), hash=f"{m.hash:016x}")
            results.append(r)
            if hit != expected or r["speed"] != 28:
                errors.append(f"pass mismatch, expected hit={expected}: {r}")
    return dict(engine=engine, fixes=list(fixes), staged=True, cases=results, errors=errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    parser.add_argument("--fix", action="append", default=[])
    args = parser.parse_args()
    result = check(args.data, args.engine, tuple(args.fix))
    print(json.dumps(result))
    return bool(result["errors"])


if __name__ == "__main__":
    raise SystemExit(main())
