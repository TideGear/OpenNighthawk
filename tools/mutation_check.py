#!/usr/bin/env python3
"""mutation_check.py - prove the parity check can see a translation defect.

A check that passes on everything proves nothing. This plants one defect at
a time in the recompiled code (recompiler --mutate: flip CF, flip ZF, or
flip bit 0 of AX after one instruction), builds that mutant into its own
build directory, plays a route, and compares the final machine state with
the interpreter's. A mutant the comparison does not notice is reported with
how often the planted defect actually ran: never (the route does not reach
the instruction) or N times (the defect was masked - an equivalent mutant,
e.g. a flag the next instruction overwrites).

    py tools/mutation_check.py --data DIR [--route ROUTE.args]
                               [--target NAME:OFFSET:KIND ...]
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VCVARS = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

# Instructions the boot-to-flight route executes, by image offset:
# VGAME's frame-rate controller (D1: imul ax,[0x368E],0x3C0), START's
# mission generator entry, F117.COM's overlay loader.
DEFAULT_TARGETS = [
    # Removing an instruction outright: should always be seen if it runs.
    "VGAME.EXE:446C:skip",
    "START.EXE:4239:skip",
    # A flipped flag or register bit: may be masked when the value is dead
    # (the report says so, with how often the defect ran).
    "VGAME.EXE:446C:ax",
    "START.EXE:4239:zf",
]


def route_args(path):
    return [l.strip() for l in open(path) if l.strip() and not l.strip().startswith("#")]


def run_route(exe, engine, data, workdir, args):
    os.makedirs(workdir, exist_ok=True)
    cmd = [exe, "--engine", engine, "--data", data, "--save", os.path.join(workdir, "save"),
           "--log", os.path.join(workdir, "run.log")] + args
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    m = re.search(r"stopped at icount (\d+).*final hash ([0-9a-f]+)", out)
    hits = re.search(r"the planted mutation ran (\d+) times", out)
    return (m.group(1), m.group(2)) if m else None, int(hits.group(1)) if hits else 0


def build_mutant(gen, build_dir):
    bat = os.path.join(build_dir + "-build.bat")
    os.makedirs(build_dir, exist_ok=True)
    with open(bat, "w") as f:
        f.write('@echo off\r\ncall "%s" >nul\r\n' % VCVARS)
        f.write('cmake -S "%s" -B "%s" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DF117R_BUILD_APP=OFF '
                '-DF117R_BUILD_TESTS=OFF -DF117R_GEN_DIR="%s" >nul\r\n' % (ROOT, build_dir, gen.replace("\\", "/")))
        f.write('cmake --build "%s" --target f117run\r\n' % build_dir)
    r = subprocess.run([bat], capture_output=True, text=True, shell=True)
    exe = os.path.join(build_dir, "f117run.exe")
    if r.returncode != 0 or not os.path.exists(exe):
        print(r.stdout[-2000:])
        sys.exit("mutant build failed")
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--route", default=os.path.join(HERE, "routes", "boot_to_flight.args"))
    ap.add_argument("--target", action="append", default=[])
    ap.add_argument("--random", type=int, default=0,
                    help="also N random 'skip' mutants among instructions the routes are known to run "
                         "(those in the coverage files)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--work", default=os.path.join(os.path.expanduser("~"), "f117-recomp-local"))
    a = ap.parse_args()
    targets = list(a.target) or (DEFAULT_TARGETS if not a.random else [])
    args = route_args(a.route)
    covs = [os.path.join(a.work, "coverage", f) for f in sorted(os.listdir(os.path.join(a.work, "coverage")))
            if f.endswith(".cov")]
    if a.random:
        # Coverage lines name instructions the routes ran: NAME HASH SEG IP.
        # Image offset = SEG*16 + IP (the .COM's origin aside, which these
        # programs do not have).
        import random
        pool = set()
        for c in covs:
            for line in open(c):
                p = line.split()
                if len(p) >= 4 and p[0] in ("VGAME.EXE", "START.EXE", "END.EXE", "MGRAPHIC.EXE", "ASOUND.117"):
                    pool.add("%s:%X:skip" % (p[0], int(p[2], 16) * 16 + int(p[3], 16)))
        rng = random.Random(a.seed)
        targets += rng.sample(sorted(pool), min(a.random, len(pool)))

    ref, _ = run_route(os.path.join(ROOT, "build", "f117run.exe"), "interp", a.data,
                       os.path.join(a.work, "runs", "mutation_ref"), args)
    print("reference (interpreter): clock %s, hash %s" % ref)
    gen = os.path.join(a.work, "gen_mutant")
    bdir = os.path.join(a.work, "build-mutant")
    detected = 0
    for t in targets:
        cmd = [sys.executable, os.path.join(ROOT, "recompiler", "recomp.py"), "--data", a.data,
               "--out", gen, "--no-comments", "--mutate", t]
        for c in covs:
            cmd += ["--coverage", c]
        if subprocess.run(cmd, capture_output=True, text=True).returncode != 0:
            print("%-22s could not plant (no translated instruction there)" % t)
            continue
        exe = build_mutant(gen, bdir)
        got, hits = run_route(exe, "recomp", a.data, os.path.join(a.work, "runs", "mutation_" + t.replace(":", "_")), args)
        if got != ref:
            detected += 1
            print("%-22s DETECTED   (ran %d times; final %s/%s)" % (t, hits, got[0] if got else "?", got[1] if got else "?"))
        else:
            why = "never ran on this route" if hits == 0 else "ran %d times, masked: an equivalent mutant" % hits
            print("%-22s not seen   (%s)" % (t, why))
    print("%d of %d mutants detected" % (detected, len(targets)))


if __name__ == "__main__":
    main()
