#!/usr/bin/env python3
"""build_recomp.py - from your own copy of the game to a recompiled build.

    py tools/build_recomp.py --data "D:\\GOG\\F-117A" [--work DIR] [--no-parity]

1. Translate every code file of the install (recompiler/recomp.py), seeded
   with any coverage already gathered.
2. Build with the translation (build.cmd -DF117R_GEN_DIR=...).
3. Play the scripted routes in tools/routes/ headless and record every
   instruction the build still had to interpret inside a known module.
4. Translate again with that coverage, and rebuild.
5. Parity: replay each route under the interpreter and the recompiled code
   and require the same checkpoints every 50 million clocks and final
   state - memory, registers and output - on the same clock counts.
6. Run every translated instruction, routes or not, from random states
   through the generated code and the interpreter (tests/insn_lockstep.c).

Everything this writes is derived from your copy of the game and goes to the
work directory (default %USERPROFILE%\\f117-recomp-local), never into the
repository.
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

from run_route import check_route, route_args, prepare_roster

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def run(cmd, **kw):
    print("  $ " + " ".join(('"%s"' % c) if " " in c else c for c in cmd))
    return subprocess.run(cmd, **kw)


def recompile(data, gen, coverage_files):
    cmd = [sys.executable, os.path.join(ROOT, "recompiler", "recomp.py"), "--data", data, "--out", gen]
    for c in coverage_files:
        cmd += ["--coverage", c]
    if run(cmd).returncode != 0:
        sys.exit("recompile failed")


def build(gen):
    r = run(["cmd", "/c", os.path.join(ROOT, "build.cmd"), "-DF117R_GEN_DIR=" + gen.replace("\\", "/")],
            capture_output=True, text=True)
    tail = (r.stdout or "")[-1500:]
    if r.returncode != 0 or "BUILD OK" not in tail:
        print(tail)
        sys.exit("build failed")
    print("  build OK")


def headless(engine, data, work, name, args, extra, route):
    exe = os.path.join(ROOT, "build", "f117run.exe")
    rundir = os.path.join(work, "runs", "%s_%s" % (name, engine))
    os.makedirs(rundir, exist_ok=True)
    # Each replay starts from the install's roster, including after a
    # coverage run or an earlier pipeline that edited/saved a pilot.
    save = tempfile.mkdtemp(prefix="save-", dir=rundir)
    prepare_roster(route, data, engine, save, rundir)
    cmd = [exe, "--engine", engine, "--data", data, "--save", save,
           "--log", os.path.join(rundir, "run.log")] + args + extra
    r = run(cmd, capture_output=True, text=True)
    with open(os.path.join(rundir, "runner.txt"), "w") as f:
        f.write((r.stdout or "") + (r.stderr or ""))
    m = re.search(r"stopped at icount (\d+) .*final hash ([0-9a-f]+)", r.stdout or "")
    if not m or r.returncode:
        print(r.stdout[-2000:], r.stderr[-2000:])
        sys.exit("run failed")
    with open(os.path.join(rundir, "run.log")) as log:
        errors = check_route(route, log.read(), save)
    if errors:
        sys.exit("route failed: " + "; ".join(errors))
    interp = re.search(r"interpreted (\d+)", r.stdout)
    checkpoints = tuple(re.findall(r"^\[hash\] (\d+) ([0-9a-f]+) (.*)$", r.stdout, re.M))
    return int(m.group(1)), m.group(2), int(interp.group(1)) if interp else -1, checkpoints


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="the game's install directory (holds F117.COM)")
    ap.add_argument("--work", default=os.path.join(os.path.expanduser("~"), "f117-recomp-local"))
    ap.add_argument("--no-parity", action="store_true")
    ap.add_argument("--no-coverage", action="store_true")
    ap.add_argument("--parity-only", action="store_true", help="check the current build without translating or rebuilding")
    a = ap.parse_args()
    if a.parity_only and a.no_parity:
        ap.error("--parity-only and --no-parity cannot be combined")

    gen = os.path.join(a.work, "gen")
    covdir = os.path.join(a.work, "coverage")
    os.makedirs(covdir, exist_ok=True)
    routes = sorted(glob.glob(os.path.join(HERE, "routes", "*.args")))

    if not a.parity_only:
        print("1. translate")
        recompile(a.data, gen, sorted(glob.glob(os.path.join(covdir, "*.cov"))))
        print("2. build")
        build(gen)
    if not a.no_coverage and not a.parity_only:
        print("3. coverage")
        for r in routes:
            name = os.path.splitext(os.path.basename(r))[0]
            # Appended to, never replaced: a build records only what it had
            # to INTERPRET, so code an earlier capture got translated is
            # absent from this one - dropping the old file would lose it.
            cov = os.path.join(covdir, name + ".cov")
            icount, h, interp, _ = headless("recomp", a.data, a.work, name, route_args(r), ["--coverage", cov], r)
            print("  %-20s %d clocks, %d interpreted" % (name, icount, interp))
        print("4. translate again, build again")
        recompile(a.data, gen, sorted(glob.glob(os.path.join(covdir, "*.cov"))))
        build(gen)
    if not a.no_parity:
        print("5. parity")
        bad = 0
        for r in routes:
            name = os.path.splitext(os.path.basename(r))[0]
            ri = headless("interp", a.data, a.work, name, route_args(r), ["--hash-every", "50000000"], r)
            rr = headless("recomp", a.data, a.work, name, route_args(r), ["--hash-every", "50000000"], r)
            same = ri[:2] == rr[:2] and ri[3] == rr[3]
            bad += not same
            print("  %-20s interp %d/%s  recomp %d/%s (%d interpreted)  %s" % (
                name, ri[0], ri[1], rr[0], rr[1], rr[2], "IDENTICAL" if same else "DIFFERENT"))
            print("    %d checkpoints compared" % len(ri[3]))
        if bad:
            sys.exit("%d route(s) differ between the engines" % bad)
        print("6. every translated instruction against the interpreter")
        exe = os.path.join(ROOT, "build", "insn_lockstep.exe")
        r = run([exe, "--states", "64"], capture_output=True, text=True)
        last = (r.stdout or "").strip().splitlines()[-1:] or ["(no output)"]
        print("  " + last[0])
        if r.returncode != 0:
            print("\n".join(l for l in r.stdout.splitlines() if "MISMATCH" in l))
            sys.exit("translated instructions differ from the interpreter")
    print("done: %s" % os.path.join(ROOT, "build", "f117a.exe"))


if __name__ == "__main__":
    main()
