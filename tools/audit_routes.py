#!/usr/bin/env python3
"""Which scripted routes does the default fix set change? (the wide fix audit; docs/bugs.md "Fix audit")

The gate runs every route with fixes off, so no route has ever shown what the default set does to it. This
runs each route twice through tools/run_route.py, once with the fixes off and once with the normal-play set
(--fix all), and compares the machine's final state hash. A route that differs is one where some fix changes
the flight; for those, `--bisect` repeats the run with each fix left out in turn. The result is a table of
which fixes change which missions, to be held against the original (a fix that changes nothing in any route
is recorded as untested, not as proved).

    py tools/audit_routes.py --data D:/GOG/F-117A --out OUT_DIR [--jobs 4] [--routes a,b] [--bisect]
"""
import argparse
import concurrent.futures as cf
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROUTES = HERE / "routes"


def run(args, route, tag, extra):
    out = args.out / route / tag
    out.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, str(HERE / "run_route.py"), str(ROUTES / (route + ".args")), "--data", str(args.data),
           "--engine", "recomp", "--out", str(out), "--"] + extra
    try:
        done = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return None
    text = done.stdout + done.stderr                      # the runner's own report ends with "final hash ..."
    (out / "runner.txt").write_text(text, encoding="utf-8")
    m = re.search(r"final hash ([0-9a-f]+)", text)
    return m.group(1) if m else None


def fix_ids(args):
    app = args.app
    res = subprocess.run([str(app), "--fix", "none", "--list-fixes"], capture_output=True, text=True)
    skip = {"QUIZ", "D1REAL", "D1PROX"}          # the quiz and the two timing options: not repairs (D1REAL moves the clock of every route)
    return [line.split()[0] for line in res.stdout.splitlines() if line.strip() and line.split()[0] not in skip]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--app", type=Path, default=HERE.parent / "build" / "f117run.exe")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=3000)
    ap.add_argument("--routes", default="")
    ap.add_argument("--bisect", action="store_true")
    args = ap.parse_args()
    names = [p.stem for p in sorted(ROUTES.glob("*.args")) if not p.stem.startswith("err_")]
    if args.routes:
        names = [n for n in names if n in args.routes.split(",")]
    ids = fix_ids(args)
    every = ["--fix", "none"] + [x for fid in ids for x in ("--fix", fid)]       # the repairs, no timing options
    with cf.ThreadPoolExecutor(args.jobs) as pool:
        futs = {}
        for n in names:
            futs[(n, "none")] = pool.submit(run, args, n, "none", ["--fix", "none"])
            futs[(n, "all")] = pool.submit(run, args, n, "all", every)
        res = {k: f.result() for k, f in futs.items()}
        differ = []
        for n in names:
            a, b = res[(n, "none")], res[(n, "all")]
            verdict = "NO RUN" if a is None or b is None else "same" if a == b else "DIFFERS"
            print("%-34s %s" % (n, verdict), flush=True)
            if verdict == "DIFFERS":
                differ.append(n)
        if args.bisect and differ:
            print("bisect: %d routes x %d fixes" % (len(differ), len(ids)), flush=True)
            sub = {}
            for n in differ:
                for fid in ids:                       # the normal set with one fix left out
                    keep = ["--fix", "none"] + [x for fid2 in ids if fid2 != fid for x in ("--fix", fid2)]
                    sub[(n, fid)] = pool.submit(run, args, n, "no-" + fid, keep)
            for n in differ:
                changed = [fid for fid in ids if sub[(n, fid)].result() not in (None, res[(n, "all")])]
                print("%-34s changed by: %s" % (n, " ".join(changed) or "(none alone)"), flush=True)


if __name__ == "__main__":
    main()
