#!/usr/bin/env python3
"""run_route.py - play a scripted route headless, without a shell in between.

    py tools/run_route.py ROUTE.args --data DIR [--engine recomp|interp]
                          [--out DIR] [-- extra f117run options]

Route files hold one f117run option per line (see tools/routes/). Passing
them through a shell mangles the backslash escapes in key scripts (\\r, \\D),
which silently changes the inputs; this passes them as they are written.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def route_args(path):
    return [l.strip() for l in open(path) if l.strip() and not l.strip().startswith("#")]


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
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
