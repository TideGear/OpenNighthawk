#!/usr/bin/env python3
"""d1_check.py - fix D1 holds a fast machine to the frame rate GOG gives.

    py tools/d1_check.py --data DIR [--ips 40000000] [--engine recomp|interp]
                         [--no-fix]

Flies boot_to_flight.args's inputs twice through the machine API: once at
GOG's speed (9 MIPS) without fixes, the reference, and once at --ips with
fix D1, every program-relative input time scaled by ips/9M so the menus get
the same emulated seconds. The VGAME quit keys are dropped and each flight
is watched for 166 emulated seconds; the first 5 are left out while S
settles. It requires the two flights to agree on the settled S ([0x368E],
the divisor of every per-second rate), on the mission clock's advance
([0x9912]) to within 2, and on frames a second (changes of the per-frame
counter [0x3DA6]) to within 0.2. With --no-fix the fast flight runs without
D1 and the figures are printed with no verdict: there S follows the
machine instead, clamped at 15, and the world clock falls behind GOG's.
"""
from __future__ import annotations

import argparse
import collections
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from machine_api import Machine, RouteInputs  # noqa: E402

GOG_IPS = 9_000_000
FLIGHT = 1_500_000_000          # clocks of VGAME watched, at GOG's speed
SETTLE = 5                      # emulated seconds left out at the start
STEP = 90_000                   # one slice; well under every input's spacing


def fly(data, ips, engine, fixes, route="boot_to_flight.args"):
    args = [l.strip() for l in open(os.path.join(HERE, "routes", route))
            if l.strip() and not l.startswith("#")]
    inputs = RouteInputs(args)
    scale = ips / GOG_IPS
    # VGAME's keys are dropped on the default route (its only ones quit); on others only the quit
    # (Alt+Q, then Y) is, so the flight is flown as the route flies it.
    def flown(p, c):
        return p != "VGAME.EXE" or (route != "boot_to_flight.args" and r"\aq" not in str(c) and str(c).strip() != "y")
    inputs.pending = [(p, int(at * scale), o, c) for p, at, o, c in inputs.pending if flown(p, c)]
    samples = []
    with tempfile.TemporaryDirectory() as save, \
            Machine(data, save, ips=ips, engine=engine, fixes=fixes) as m:
        while m.clock < 6_000_000_000 * scale:
            inputs.poll(m)
            m.run_until(m.clock + STEP)
            if m.program.upper() != "VGAME.EXE":
                continue
            t = m.clock - m.start
            if t > FLIGHT * scale:
                break
            ds = (m.psp + 0x10 + 0x1E42) << 4
            samples.append((t, m.read16(ds + 0x368E), m.read16(ds + 0x9912), m.read16(ds + 0x3DA6)))
        final_hash = m.hash
    flight = [s for s in samples if s[0] > SETTLE * ips]
    if len(flight) < 2:
        raise SystemExit(f"{ips} ips: VGAME was not reached or not flown")
    seconds = (flight[-1][0] - flight[0][0]) / ips
    frames = sum(a[3] != b[3] for a, b in zip(flight, flight[1:]))
    s_counts = collections.Counter(s[1] for s in flight)
    return {"ips": ips, "fixes": list(fixes), "S": s_counts.most_common(1)[0][0], "S_counts": dict(s_counts),
            "clock": (flight[-1][2] - flight[0][2]) & 0xFFFF, "seconds": round(seconds, 2),
            "fps": round(frames / seconds, 2), "hash": f"{final_hash:016x}"}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--ips", type=int, default=40_000_000)
    ap.add_argument("--engine", default="recomp", choices=("recomp", "interp"))
    ap.add_argument("--no-fix", action="store_true")
    ap.add_argument("--route", default="boot_to_flight.args",
                    help="a typed-input route in tools/routes (its VGAME keys are dropped; the flight is watched)")
    a = ap.parse_args()
    ref = fly(a.data, GOG_IPS, a.engine, (), a.route)
    fast = fly(a.data, a.ips, a.engine, () if a.no_fix else ("D1",), a.route)
    for r in (ref, fast):
        print(f"{r['ips']:>10} ips {','.join(r['fixes']) or 'no fixes':>8}: S {r['S']} {r['S_counts']}, "
              f"mission clock {r['clock']} in {r['seconds']} s, {r['fps']} frames/s, hash {r['hash']}")
    if a.no_fix:
        return 0
    errors = []
    if fast["S"] != ref["S"]:
        errors.append(f"settled S {fast['S']}, GOG {ref['S']}")
    if abs(fast["clock"] - ref["clock"]) > 2:
        errors.append(f"mission clock {fast['clock']}, GOG {ref['clock']}")
    if abs(fast["fps"] - ref["fps"]) > 0.2:
        errors.append(f"{fast['fps']} frames/s, GOG {ref['fps']}")
    for e in errors:
        print("ERROR:", e)
    print("D1 check:", "FAILED" if errors else "OK")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
