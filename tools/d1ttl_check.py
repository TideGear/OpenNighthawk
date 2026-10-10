#!/usr/bin/env python3
"""Staged SA-5 proximity-hit regression for D1TTL, under either engine.

Boots normally at 20 MIPS, then plants one guided missile beside the parked
aircraft for each case. These are synthetic hit-boundary checks, not combat
flights: weapon slots, launch grace, countermeasures, training immunity and
the damage mask are explicitly staged. No game files are changed.

    py tools/d1ttl_check.py --data DIR [--engine interp] [--fix D1TTL]

Positive signed lifetimes must hit in both modes; lifetimes with the sign
bit set must hit only with D1TTL. The final 4*S frames remain harmless.
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
    fixed = "D1TTL" in fixes or "all" in fixes
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
        w = ds + 0x3C3A
        # The margin above 32768 keeps life negative for the whole observation.
        for ttl in (60, 1000, 32760, 32800, 33600, 36000, 65535):
            for slot in range(12):
                m.stage_write16(w + slot * 28 + 14, 0)
            m.stage_write16(ds + 0x9B34, m.read16(ds + 0x9B34) & ~0x1030)
            m.stage_write16(ds + 0x3664, 0)
            m.stage_write16(ds + 0x3D90, (m.read16(ds + 0x9F94) + 100) & 0xFFFF)
            values = (m.read16(ds + 0xC0D0), m.read16(ds + 0xC0DE) + 4,
                      m.read16(ds + 0x2DF4) + 500, 200, 0, 0, 0, ttl, 2, 0, 0, 3, 0, 0)
            for i, value in enumerate(values):
                m.stage_write16(w + 2 * i, value & 0xFFFF)
            before, frame = m.read16(ds + 0xC5F4), m.read16(ds + 0x3D8E)
            if m.run_until(m.clock + ips // 5) != Machine.SLICE:
                raise RuntimeError("machine stopped during staged check")
            S = m.read16(ds + 0x368E)
            hits = (m.read16(ds + 0xC5F4) - before) & 0xFFFF
            expected_hit = ttl > 4 * S and (ttl < 32768 or (fixed and ttl <= 36000))
            result = dict(ttl=ttl, S=S, remaining=m.read16(w + 14),
                          damage_increments=hits, expected_hit=expected_hit,
                          frames=(m.read16(ds + 0x3D8E) - frame) & 0xFFFF,
                          hash=f"{m.hash:016x}")
            results.append(result)
            if S != 15 or not result["frames"] or bool(hits) != expected_hit:
                errors.append(f"ttl {ttl}: expected hit={expected_hit}, got {result}")
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
