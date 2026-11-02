#!/usr/bin/env python3
"""Which of a session's recorded fixes change that flight?

For each fix in the log's `# f117r-fixes` header, replay the session with only that fix removed and
compare the machine's final state hash with the full-set replay. Same hash: that fix did nothing in
this flight (it may still matter on a route the flight never took). Different hash: it changed the
game, so the difference is the thing to judge against the original (GOG's DOSBox, tools/dosbox_flight.py).

    py tools/fix_audit.py --app build/f117a.exe --data D:/GOG/F-117A --session SESSION_DIR --out OUT_DIR
                          [--exit-after 5100000000] [--jobs 4] [--only D5,D12]
"""
import argparse
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
from pathlib import Path


def header_fixes(log):
    for line in log.read_text(errors="replace").splitlines()[:8]:
        if line.startswith("# f117r-fixes"):
            return line.split()[2:]
    return []


def run(args, name, log_text):
    work = args.out / name
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    log = work / "input.log"
    log.write_text(log_text, newline="\n")
    save = work / "save"
    shutil.copytree(args.session / "save-start", save)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    env.update(getattr(args, "extra_env", {}).get(name, {}))        # per-run extras (verify_flights: a RAM dump)
    cmd = [str(args.app), "--data", str(args.data), "--save", str(save), "--replay", str(log), "--presentation", "original",
           "--sound", "adlib", "--exit-after", str(args.exit_after), "--log", str(work / "run.log")]
    subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=args.timeout)
    text = (work / "run.log").read_text(errors="replace") if (work / "run.log").exists() else ""
    m = re.search(r"stopped at icount (\d+);.*final hash ([0-9a-f]+)", text)
    return (m.group(2), m.group(1)) if m else (None, None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", type=Path, required=True)
    ap.add_argument("--data", type=Path, required=True)
    ap.add_argument("--session", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--exit-after", type=int, default=5100000000)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=3000)
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    text = (args.session / "input.log").read_text(errors="replace")
    ids = header_fixes(args.session / "input.log")
    if args.only:
        ids = [i for i in ids if i in args.only.split(",")]
    jobs = {"all": text}
    for fid in ids:
        jobs["no-" + fid] = re.sub(r"^(# f117r-fixes.*?)(?<=\s)%s(\s|$)" % re.escape(fid), r"\1\2", text, count=1, flags=re.M)
    with cf.ThreadPoolExecutor(args.jobs) as pool:
        futs = {k: pool.submit(run, args, k, v) for k, v in jobs.items()}
        res = {k: f.result() for k, f in futs.items()}
    base = res["all"]
    print("full set: hash %s at icount %s" % base)
    for fid in ids:
        h, ic = res["no-" + fid]
        verdict = "NO RUN" if h is None else "same: no effect in this flight" if h == base[0] else "CHANGES the flight"
        print("%-8s %s" % (fid, verdict))


if __name__ == "__main__":
    main()
