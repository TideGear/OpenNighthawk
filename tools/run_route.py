#!/usr/bin/env python3
"""run_route.py - play a scripted route headless, without a shell in between.

    py tools/run_route.py ROUTE.args --data DIR [--engine recomp|interp]
                          [--out DIR] [-- extra f117run options]

Route files hold one f117run option per line (see tools/routes/). Passing
them through a shell mangles the backslash escapes in key scripts (\\r, \\D),
which silently changes the inputs; this passes them as they are written.

Optional comments declare milestones checked after the run:
    # expect-world CU
    # expect-exit VGAME.EXE 0 1000000000
    # expect-save Roster.Fil 244 434845434b00
These require world files, the program's exit code after a minimum elapsed
clock count, or hexadecimal bytes at a saved file offset. They do not prove a landing.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def route_args(path):
    return [l.strip() for l in open(path) if l.strip() and not l.strip().startswith("#")]


def check_route(path, log, save_dir=None):
    """Check declared milestones, so equal early crashes cannot pass parity."""
    errors = []
    with open(path) as source:
        for line in source:
            if line.startswith("# expect-save "):
                _, _, name, offset, expected = line.split()
                offset, expected = int(offset, 0), bytes.fromhex(expected)
                try:
                    with open(os.path.join(save_dir, name), "rb") as saved:
                        saved.seek(offset)
                        actual = saved.read(len(expected))
                except (OSError, TypeError):
                    errors.append(f"saved {name} is missing")
                    continue
                if actual != expected:
                    errors.append(f"saved {name} at {offset}: {actual.hex()}, expected {expected.hex()}")
            elif line.startswith("# expect-world "):
                world = line.split()[2].lower()
                for suffix, program in (("wld", "START.EXE"), ("3dg", "VGAME.EXE")):
                    pattern = rf"^\[file\] open '{re.escape(world)}\.{suffix}' -> \d+ @\d+ {re.escape(program)}$"
                    if not re.search(pattern, log, re.M | re.I):
                        errors.append(f"{world}.{suffix} was not opened by {program}")
            elif line.startswith("# expect-exit "):
                _, _, program, code, minimum = line.split()
                start = re.search(rf"^\[exec\] {re.escape(program)}\s+.* @(\d+)$", log, re.M)
                end = re.search(rf"^\[exit\] {re.escape(program)} terminated with code (\d+) .* @(\d+)$", log, re.M)
                if not start or not end:
                    errors.append(f"{program} did not run and exit")
                elif end[1] != code or int(end[2]) - int(start[1]) < int(minimum):
                    errors.append(f"{program} exited with code {end[1]} after {int(end[2]) - int(start[1])} clocks; "
                                  f"expected code {code} after at least {minimum}")
    return errors


def main():
    argv = sys.argv[1:]
    extra = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("route")
    ap.add_argument("--data", required=True)
    ap.add_argument("--engine", default="recomp")
    ap.add_argument("--out", default=None, help="run directory (save/, run.log)")
    a = ap.parse_args(argv)
    name = os.path.splitext(os.path.basename(a.route))[0]
    out = a.out or os.path.join(os.path.expanduser("~"), "f117-recomp-local", "runs", name + "_" + a.engine)
    os.makedirs(out, exist_ok=True)
    cmd = [os.path.join(ROOT, "build", "f117run.exe"), "--engine", a.engine, "--data", a.data,
           "--save", os.path.join(out, "save"), "--log", os.path.join(out, "run.log")]
    cmd += route_args(a.route) + extra
    result = subprocess.call(cmd)
    if result:
        return result
    log_path, save_path = os.path.join(out, "run.log"), os.path.join(out, "save")
    for i, option in enumerate(cmd[:-1]):
        if option == "--log":
            log_path = cmd[i + 1]
        elif option == "--save":
            save_path = cmd[i + 1]
    with open(log_path) as log:
        errors = check_route(a.route, log.read(), save_path)
    for error in errors:
        print("route failed: " + error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
