#!/usr/bin/env python3
"""Probe the historical fast-machine startup report with normal inputs.

One fresh process per case. --skip-intro presses Enter in the logo/player;
it exercises startup and driver initialization, not complete movie playback.
Example: py tools/startup_probe.py --data DIR --ips 1000000000 --sound 2
         --engine interp --skip-intro
"""
import argparse
import json
import tempfile
import time

from machine_api import Machine, RouteInputs


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", required=True)
    ap.add_argument("--ips", type=int, required=True)
    ap.add_argument("--sound", choices=("1", "2", "3"), default="2",
                    help="1 speaker, 2 AdLib, 3 Roland")
    ap.add_argument("--engine", choices=("interp", "recomp"), default="interp")
    ap.add_argument("--skip-intro", action="store_true")
    ap.add_argument("--log")
    a = ap.parse_args()
    if a.ips < 1000:
        ap.error("--ips must be at least 1000")
    inputs = ["--type", f"SETUP.EXE+{a.ips // 5}:N",
              "--type", f"SETUP.EXE+{a.ips}:{a.sound}"]
    if a.skip_intro:
        for program in ("MPS_LOGO.EXE", "PLAYER.EXE"):
            inputs += ["--type", f"{program}+{a.ips // 5}:\\r"]
    events, programs, started = RouteInputs(inputs), [], time.monotonic()
    with tempfile.TemporaryDirectory(prefix="f117-startup-") as save, \
            Machine(a.data, save, ips=a.ips, engine=a.engine, log=a.log) as m:
        status = Machine.SLICE
        while m.clock < a.ips * 180 and time.monotonic() - started < 180:
            events.poll(m)
            status = m.run_until(m.clock + a.ips // 1000)
            if not programs or programs[-1]["program"] != m.program:
                programs.append(dict(program=m.program, clock=m.clock))
            if m.program == "START.EXE" or status != Machine.SLICE:
                break
        reached = m.program == "START.EXE"
        print(json.dumps(dict(ips=a.ips, engine=a.engine, sound=a.sound,
                              skip_intro=a.skip_intro, reached_start=reached,
                              programs=programs, status=status, clock=m.clock,
                              hash=f"{m.hash:016x}",
                              seconds=round(time.monotonic() - started, 1))))
    return 0 if reached else 1


if __name__ == "__main__":
    raise SystemExit(main())
