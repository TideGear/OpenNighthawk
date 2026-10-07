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
import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

from run_route import check_route, route_args, prepare_roster, roster_seed

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


_T0 = time.time()
_LAST = [time.time()]


def lap(label):
    """Print the wall time since the previous lap, so a run shows where it goes."""
    now = time.time()
    print("  [%s took %.0f s; %.0f s since start]" % (label, now - _LAST[0], now - _T0))
    _LAST[0] = now


def run(cmd, **kw):
    print("  $ " + " ".join(('"%s"' % c) if " " in c else c for c in cmd))
    return subprocess.run(cmd, **kw)


def sync_tree(new, gen):
    """Copy the files of `new` into `gen`, touching only those whose bytes
    differ (so the build tool, which goes by modification time, recompiles
    only what changed), and remove files `new` no longer has. True when
    anything changed."""
    changed = False
    os.makedirs(gen, exist_ok=True)
    names = set(os.listdir(new))
    for name in names:
        src, dst = os.path.join(new, name), os.path.join(gen, name)
        if os.path.isfile(dst):
            with open(src, "rb") as a, open(dst, "rb") as b:
                if a.read() == b.read():
                    continue
        shutil.copyfile(src, dst)
        changed = True
    for name in os.listdir(gen):
        if name not in names:
            os.remove(os.path.join(gen, name))
            changed = True
    return changed


def recompile(data, gen, coverage_files):
    """Translate into a scratch directory and bring `gen` up to date from it.
    True when any generated file changed."""
    scratch = gen + ".new"
    shutil.rmtree(scratch, ignore_errors=True)
    cmd = [sys.executable, os.path.join(ROOT, "recompiler", "recomp.py"), "--data", data, "--out", scratch]
    for c in coverage_files:
        cmd += ["--coverage", c]
    if run(cmd).returncode != 0:
        sys.exit("recompile failed")
    changed = sync_tree(scratch, gen)
    shutil.rmtree(scratch, ignore_errors=True)
    return changed


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
    return int(m.group(1)), m.group(2), int(interp.group(1)) if interp else -1, checkpoints, save, os.path.join(rundir, "run.log")


def session_groups(routes):
    """Group routes that play the same session: the same options (so the same
    inputs, budget, clock and replay file) and the same seed roster. Only
    their `# expect-` milestones differ, and those are read from the log and
    the saved files, so one run per engine serves them all; each route's
    milestones are checked against it. The first route (by name) leads."""
    groups = {}
    for r in routes:
        groups.setdefault((tuple(route_args(r)), roster_seed(r)), []).append(r)
    return list(groups.values())


def expected_cost(route, engine=None):
    """A rough relative run time, for starting the longest runs first: the
    clock budget, times 3 for the interpreter. Order never changes a result
    (each run is a child process on its own virtual clock)."""
    args = route_args(route)
    steps = int(args[args.index("--steps") + 1]) if "--steps" in args else 0
    return steps * (3 if engine == "interp" else 1)


def parallel(jobs, items, work, cost=None):
    """Run work(item) for every item on `jobs` threads (each is a child process,
    on its own virtual clock, so the results do not depend on the load) and
    return the results in item order; the first failure is raised after the
    others finish. With `cost`, the most expensive items are started first."""
    def guarded(item):
        try:
            return work(item), None
        except SystemExit as e:
            return None, e
    order = sorted(range(len(items)), key=lambda i: -cost(items[i])) if cost else range(len(items))
    results = [None] * len(items)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = {i: pool.submit(guarded, items[i]) for i in order}
        for i, f in futures.items():
            results[i] = f.result()
    for _, error in results:
        if error is not None:
            raise error
    return [value for value, _ in results]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="the game's install directory (holds F117.COM)")
    ap.add_argument("--work", default=os.path.join(os.path.expanduser("~"), "f117-recomp-local"))
    ap.add_argument("--jobs", type=int, default=max(1, min(24, (os.cpu_count() or 2) * 3 // 4)),
                    help="routes to run at once in the coverage and parity steps (1 = one at a time)")
    ap.add_argument("--seed-coverage", metavar="DIR",
                    help="start from the *.cov files of an earlier run's coverage directory, so the first "
                         "translation is already complete and the second build can be skipped")
    ap.add_argument("--no-parity", action="store_true")
    ap.add_argument("--no-coverage", action="store_true")
    ap.add_argument("--parity-only", action="store_true", help="check the current build without translating or rebuilding")
    a = ap.parse_args()
    if a.parity_only and a.no_parity:
        ap.error("--parity-only and --no-parity cannot be combined")

    gen = os.path.join(a.work, "gen")
    covdir = os.path.join(a.work, "coverage")
    os.makedirs(covdir, exist_ok=True)
    if a.seed_coverage:
        for cov in glob.glob(os.path.join(a.seed_coverage, "*.cov")):
            dst = os.path.join(covdir, os.path.basename(cov))
            if not os.path.exists(dst):
                shutil.copyfile(cov, dst)
    routes = sorted(glob.glob(os.path.join(HERE, "routes", "*.args")))
    groups = session_groups(routes)
    lead = {r: g[0] for g in groups for r in g}
    shared = [g for g in groups if len(g) > 1]
    for g in shared:
        print("  one session serves %s" % ", ".join(os.path.splitext(os.path.basename(r))[0] for r in g))

    if not a.parity_only:
        print("1. translate")
        recompile(a.data, gen, sorted(glob.glob(os.path.join(covdir, "*.cov"))))
        print("2. build")
        build(gen)
        lap("translate and build")
    if not a.no_coverage and not a.parity_only:
        print("3. coverage")
        def cover(r):
            name = os.path.splitext(os.path.basename(r))[0]
            # Appended to, never replaced: a build records only what it had
            # to INTERPRET, so code an earlier capture got translated is
            # absent from this one - dropping the old file would lose it.
            cov = os.path.join(covdir, name + ".cov")
            icount, h, interp, *_ = headless("recomp", a.data, a.work, name, route_args(r), ["--coverage", cov], r)
            return name, icount, interp
        for name, icount, interp in parallel(a.jobs, [g[0] for g in groups], cover, cost=expected_cost):
            print("  %-20s %d clocks, %d interpreted" % (name, icount, interp))
        lap("coverage")
        print("4. translate again, build again")
        if recompile(a.data, gen, sorted(glob.glob(os.path.join(covdir, "*.cov")))):
            build(gen)
        else:
            print("  the coverage added no translated code: the generated files are byte-identical, "
                  "so the first build stands")
        lap("second translate and build")
    if not a.no_parity:
        # Neither lockstep depends on the routes, only on the build, so they
        # run beside the route replays instead of after them.
        lockstep = {}
        for key, exe_name, states in (("insn", "insn_lockstep.exe", "64"), ("func", "func_lockstep.exe", "4000")):
            exe = os.path.join(ROOT, "build", exe_name)
            print("  $ " + '"%s" --states %s (in the background)' % (exe, states))
            lockstep[key] = subprocess.Popen([exe, "--states", states], stdout=subprocess.PIPE,
                                             stderr=subprocess.STDOUT, text=True)
        print("5. parity")
        bad = 0

        def replay(job):
            r, engine = job
            name = os.path.splitext(os.path.basename(r))[0]
            return headless(engine, a.data, a.work, name, route_args(r), ["--hash-every", "50000000"], r)
        leads = [g[0] for g in groups]
        jobs = [(r, e) for r in leads for e in ("interp", "recomp")]
        done = parallel(a.jobs, jobs, replay, cost=lambda j: expected_cost(j[0], j[1]))
        by_lead = {r: (done[2 * k], done[2 * k + 1]) for k, r in enumerate(leads)}
        for g in groups:
            ri, rr = by_lead[g[0]]
            same = ri[:2] == rr[:2] and ri[3] == rr[3]
            for r in g:
                name = os.path.splitext(os.path.basename(r))[0]
                # Every route in the group is held to its own milestones on
                # the shared run (the leader's were checked as it ran).
                if r != g[0]:
                    for engine, run_result in zip(("interp", "recomp"), (ri, rr)):
                        with open(run_result[5]) as log:
                            errors = check_route(r, log.read(), run_result[4])
                        if errors:
                            sys.exit("route %s failed on the shared %s session: %s" % (name, engine, "; ".join(errors)))
                bad += not same
                print("  %-20s interp %d/%s  recomp %d/%s (%d interpreted)  %s" % (
                    name, ri[0], ri[1], rr[0], rr[1], rr[2], "IDENTICAL" if same else "DIFFERENT"))
                print("    %d checkpoints compared" % len(ri[3]))
        lap("parity")
        if bad:
            for proc in lockstep.values():
                proc.kill()
            sys.exit("%d route(s) differ between the engines" % bad)
        for number, key, what, message in (
                (6, "insn", "every translated instruction against the interpreter",
                 "translated instructions differ from the interpreter"),
                (7, "func", "every matched routine against the original",
                 "matched routines differ from the original")):
            print("%d. %s" % (number, what))
            out, _ = lockstep[key].communicate()
            last = (out or "").strip().splitlines()[-1:] or ["(no output)"]
            print("  " + last[0])
            if lockstep[key].returncode != 0:
                print("\n".join(l for l in out.splitlines() if "MISMATCH" in l))
                sys.exit(message)
            lap(what)
    print("done: %s" % os.path.join(ROOT, "build", "f117a.exe"))


if __name__ == "__main__":
    main()
