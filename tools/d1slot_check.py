#!/usr/bin/env python3
"""Staged incoming-missile cancellation regression, with normal boot.

    py tools/d1slot_check.py --data DIR [--engine interp] [--fix D1SLOT]

At S=8/15, zero, one, positive signed and unsigned lifetime controls run
before the lost-lock cases. An off-axis missile heading away from the
player is cancelled by seeker 72A5. Its life becomes FFFF without D1SLOT,
and stays zero with it. Slots, S and the launch grace are explicitly staged;
these are countdown-boundary checks, not natural combat flights.
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
    fixed = "D1SLOT" in fixes or "all" in fixes
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
        weapons = ds + 0x3C3A
        cases = [(S, ttl, False, 0) for S in (8, 15) for ttl in (0, 1, 1000, 32800, 36000)]
        cases += [(15, 1, False, 8)]  # the first player slot must also retain its countdown
        cases += [(S, 1000, True, slot) for S in (8, 15) for slot in (0, 7)]
        for S, ttl, cancel, slot in cases:
            for i in range(12):
                m.stage_write16(weapons + i * 28 + 14, 0)
            w = weapons + slot * 28
            m.stage_write16(ds + 0x368E, S)
            m.stage_write16(ds + 0x3DA6, 0)
            m.stage_write16(ds + 0x39E0, 0)
            m.stage_write16(ds + 0x9B34, m.read16(ds + 0x9B34) & ~0x1030)
            m.stage_write16(ds + 0x3D90, (m.read16(ds + 0x9F94) + 100) & 0xFFFF)
            direction = -1 if cancel else 1
            values = (m.read16(ds + 0xC0D0) + direction * 40,
                      m.read16(ds + 0xC0DE) + direction * 120,
                      m.read16(ds + 0x2DF4) + 500, 28, 0, 0, 0, ttl, 2, 0, 0, 0, 0, 0)
            for i, value in enumerate(values):
                m.stage_write16(w + 2 * i, value & 0xFFFF)
            before, frame = m.read16(ds + 0xC5F4), m.read16(ds + 0x3D8E)
            if m.run_until(m.clock + ips // 5) != Machine.SLICE:
                raise RuntimeError("machine stopped during staged check")
            frames = (m.read16(ds + 0x3D8E) - frame) & 0xFFFF
            expected = (0 if fixed else 65535) if cancel else max(0, ttl - frames)
            result = dict(S=S, S_after=m.read16(ds + 0x368E), ttl=ttl, cancel=cancel, slot=slot,
                          remaining=m.read16(w + 14), expected_remaining=expected, frames=frames,
                          damage_increments=(m.read16(ds + 0xC5F4) - before) & 0xFFFF,
                          hash=f"{m.hash:016x}")
            results.append(result)
            if not frames or result["S_after"] != S or result["remaining"] != expected or result["damage_increments"]:
                errors.append(f"countdown mismatch: {result}")
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
