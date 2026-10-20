#!/usr/bin/env python3
"""Replay gate routes with presentation and compare their saved scan verdicts.

Run build_recomp.py first to supply fresh baseline runs, then:
  py tools/presentation_check.py --data DIR --baseline-runs DIR/runs --out DIR
Every replay and interpolation endpoint must be exact, no frame may be
dropped, and every guest checkpoint/final hash must equal its scan baseline.
Raw results stay outside the repository. Concurrent timings are diagnostics,
not a rendering-performance benchmark.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

import route_damage
from run_route import check_route, route_args
from build_recomp import session_groups

ROOT = Path(__file__).resolve().parents[1]


def verdict(text):
    final = re.findall(r"stopped at icount (\d+) .*final hash ([0-9a-f]+)", text)
    if len(final) != 1:
        raise ValueError("missing or ambiguous final guest state")
    checkpoints = re.findall(r"^\[hash\] (\d+) ([0-9a-f]+) (.*)$", text, re.M)
    if int(final[0][0]) <= 0 or (int(final[0][0]) >= 50000000 and not checkpoints):
        raise ValueError("no guest checkpoints")
    return final[0], checkpoints


def presentation(text, flight):
    replay = re.search(r"\[present\] replay: (\d+) logic frames, (\d+) equal to the display at their close, "
                       r"(\d+) not, (\d+) before a seed; (\d+) dropped", text)
    pairs = re.search(r"\[present\] interp: (\d+) frame pairs, (\d+) exact at both ends .*?, (\d+) not;", text)
    if not replay or not pairs:
        raise ValueError("missing presentation summaries")
    frames, exact, wrong, unseeded, dropped = map(int, replay.groups())
    paired, endpoints, bad_endpoints = map(int, pairs.groups())
    if wrong or unseeded or dropped or frames != exact or bad_endpoints or paired != endpoints:
        raise ValueError("presentation lost or changed a frame or interpolation endpoint")
    if flight and (frames < 2 or paired < 1):
        raise ValueError("flight never produced a checked presentation pair")
    return dict(frames=frames, pairs=paired, dropped=dropped)


def rerun_presentation(text, paired):
    isolation = re.search(r"\[present\] isolation: (\d+) copies checked, (\d+) changed the real machine, (\d+) DOS calls blocked", text)
    rerun = re.search(r"\[present\] rerun: (\d+) in-between pictures drawn by the original on a copy, (\d+) fell back to "
                      r"interpolation; at t = 0 the copy drew its frame exactly (\d+) times, not (\d+)", text)
    if not isolation or not rerun:
        raise ValueError("missing rerun checks")
    copies, changed, blocked = map(int, isolation.groups())
    drawn, fallback, exact, wrong = map(int, rerun.groups())
    # A copy that reaches a DOS call (a display change loads its picture file in flight) is blocked, discarded and
    # replaced by interpolation (rerun.c: the copy's result is used only when nothing was blocked), so a blocked copy
    # must be one of the fallbacks; more blocked copies than fallbacks would mean a discarded copy was drawn.
    if changed or blocked > fallback or wrong or copies < drawn + exact + wrong or (paired and (not drawn or not exact)):
        raise ValueError("rerun isolation, unchanged-copy equality or drawing failed")
    return dict(copies=copies, copy_exact=exact, rerun_drawn=drawn, rerun_fell_back=fallback)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", required=True)
    ap.add_argument("--baseline-runs", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--scales", type=int, nargs="+", default=[4, 9])
    ap.add_argument("--routes", nargs="+", help="route stems; default all gate routes")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--mode", choices=("interp", "rerun"), default="interp")
    args = ap.parse_args()
    route_damage.set_master(args.data)
    if any(n < 1 or n > 12 for n in args.scales) or args.jobs < 1:
        ap.error("scales must be 1..12 and jobs positive")
    exe = Path(os.environ.get("F117R_RUN_EXE", ROOT / "build/f117run.exe")).resolve()
    runner_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    routes = sorted((ROOT / "tools/routes").glob("*.args"))
    canonical = {Path(r).stem: Path(group[0]).stem
                 for group in session_groups([str(r) for r in routes]) for r in group}
    if args.routes:
        names = set(args.routes)
        routes = [r for r in routes if r.stem in names]
        if {r.stem for r in routes} != names:
            ap.error("unknown route name")
    # Validate all baseline verdicts and milestones before starting work.
    baselines = {}
    for route in routes:
        base = args.baseline_runs / (canonical[route.stem] + "_interp")
        text = (base / "runner.txt").read_text()
        log = (base / "run.log").read_text()
        saves = sorted(base.glob("save-*"), key=lambda p: p.stat().st_mtime)
        errors = check_route(str(route), log, str(saves[-1]) if saves else str(base / "save"))
        if errors:
            raise ValueError(f"baseline {route.stem}: {errors}")
        baselines[route.stem] = verdict(text)
    args.out.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()

    def run(job):
        route, scale = job
        # run_route uses the output folder's save state. A repeated check
        # must start fresh, including when the caller reuses --out.
        out = Path(tempfile.mkdtemp(prefix=f"{route.stem}-n{scale}-", dir=args.out))
        began = time.monotonic()
        with (out / "runner.txt").open("w") as stream:
            result = subprocess.run([sys.executable, str(ROOT / "tools/run_route.py"), str(route),
                "--data", args.data, "--out", str(out), "--", "--present", args.mode, "--present-scale", str(scale),
                "--hash-every", "50000000"], env=dict(os.environ, F117R_RUN_EXE=str(exe)),
                stdout=stream, stderr=subprocess.STDOUT)
        row = dict(route=route.stem, scale=scale, out=str(out), seconds=round(time.monotonic()-began, 2), errors=[])
        try:
            if result.returncode:
                raise ValueError(f"runner exited {result.returncode}")
            text = (out / "runner.txt").read_text()
            if verdict(text) != baselines[route.stem]:
                raise ValueError("guest checkpoints/final state differ from scan baseline")
            log = (out / "run.log").read_text()
            row.update(presentation(text, "[exec] VGAME.EXE " in log))
            if args.mode == "rerun":
                row.update(rerun_presentation(text, row["pairs"]))
        except (OSError, ValueError) as error:
            row["errors"].append(str(error))
        print(f"{route.stem} N={scale}: {'FAIL '+str(row['errors']) if row['errors'] else 'exact'}", flush=True)
        return row

    jobs = [(r, n) for r in routes for n in args.scales]
    def cost(job):
        options = route_args(str(job[0]))
        return int(options[options.index("--steps")+1]) if "--steps" in options else 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(run, sorted(jobs, key=cost, reverse=True)))
    changed = hashlib.sha256(exe.read_bytes()).hexdigest() != runner_hash
    report = dict(seconds=round(time.monotonic()-started, 2),
                  runner_sha256=runner_hash, runner_changed=changed, results=results)
    (args.out / "verdict.json").write_text(json.dumps(report, indent=2))
    return int(changed or any(r["errors"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
