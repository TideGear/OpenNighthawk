#!/usr/bin/env python3
"""Staged proximity-distance check for D1PROX (an explicit S=9 balance floor).

Boot normally at 20 MIPS, then place a guided SA-12 beside the parked jet.
S and its measurement window are staged along with the weapon and damage
state. Equal geometry hits at S=8/9 but misses at S=15 without the option;
the option restores the nearby hit while a farther missile still misses.
The abandoned FFFF lifetime remains harmless. This is not a natural flight.

    py tools/d1prox_check.py --data DIR [--engine interp] [--fix D1PROX]
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
    fixed = "D1PROX" in fixes or "all" in fixes
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
        for S, distance, ttl in ((8, 18, 1000), (9, 18, 1000), (15, 18, 1000),
                                 (15, 40, 1000), (15, 18, 65535)):
            for slot in range(12):
                m.stage_write16(w + slot * 28 + 14, 0)
            m.stage_write16(ds + 0x368E, S)
            m.stage_write16(ds + 0x3DA6, 0)
            m.stage_write16(ds + 0x9B34, m.read16(ds + 0x9B34) & ~0x1030)
            m.stage_write16(ds + 0x3664, 0)
            m.stage_write16(ds + 0x3D90, (m.read16(ds + 0x9F94) + 100) & 0xFFFF)
            values = (m.read16(ds + 0xC0D0) + distance, m.read16(ds + 0xC0DE) + 4,
                      m.read16(ds + 0x2DF4) + 100, 28, 0, 0, 0, ttl, 6, 0, 0, 3, 0, 0)
            for i, value in enumerate(values):
                m.stage_write16(w + 2 * i, value & 0xFFFF)
            before = m.read16(ds + 0xC5F4)
            if m.run_until(m.clock + ips // 5) != Machine.SLICE:
                raise RuntimeError("machine stopped during staged check")
            hits = (m.read16(ds + 0xC5F4) - before) & 0xFFFF
            expected_hit = distance == 18 and ttl == 1000 and (S <= 9 or fixed)
            result = dict(S_before=S, S_after=m.read16(ds + 0x368E), distance=distance,
                          ttl=ttl, remaining=m.read16(w + 14), damage_increments=hits,
                          expected_hit=expected_hit, hash=f"{m.hash:016x}")
            results.append(result)
            if result["S_after"] != S or bool(hits) != expected_hit:
                errors.append(f"expected hit={expected_hit}, got {result}")
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
