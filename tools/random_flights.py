#!/usr/bin/env python3
"""random_flights.py - seeded random flying, held to the interpreter.

    py tools/random_flights.py --data DIR [--seeds 1,2,3] [--span N] [--every N]

Scripted routes follow one path through the game. This plays the way into
flight from tools/routes/boot_to_flight.args, then, instead of quitting,
hands the aircraft to a random pilot: a seeded stream of flight keys -
stick, throttle, weapons, views, systems - each held for a random time, for
`span` instructions. The same inputs run under the interpreter and the
recompiled code, hashing the whole machine every `every` instructions, and
the first hash that differs is reported with its clock. A seed reproduces
its session exactly (the generated route is written next to the runs).

Alt combinations are never sent (Alt+Q ends the mission); everything else
the game reads in flight may be.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import os
import random
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WORK = os.path.join(os.path.expanduser("~"), "f117-recomp-local", "random")

# Keys a pilot uses in flight (escapes as tools/routes and src/host/keys.h
# read them). Weighted: the stick and throttle most, the rest now and then.
STICK = ["\\U", "\\D", "\\L", "\\R"]
THROTTLE = ["+", "-", "=", "_"]
OTHER = [" ", "\\r", "\\t", "\\b", "g", "b", "f", "a", "w", "e", "t", "r", "s", "m", "c", "h", "j",
         "k", "l", "n", "o", "p", "u", "v", "x", "z", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
         "[", "]", ",", ".", "/", ";", "'", "\\1", "\\2", "\\3", "\\4", "\\5", "\\6", "\\7", "\\8", "\\9"]


def base_route():
    """boot_to_flight's inputs up to flight, without its quit and step count."""
    out, lines = [], [l.strip() for l in open(os.path.join(HERE, "routes", "boot_to_flight.args"))
                      if l.strip() and not l.strip().startswith("#")]
    i = 0
    while i < len(lines):
        opt = lines[i]
        val = lines[i + 1] if i + 1 < len(lines) else ""
        i += 2
        if opt in ("--steps", "--time-us") or val.startswith("VGAME.EXE+"):
            continue
        out += [opt, val]
    return out


def random_route(seed, span):
    rng = random.Random(seed)
    args = base_route()
    args += ["--type", "VGAME.EXE+100000000:+"]          # throttle up, as the routes do
    t = 150_000_000
    while t < 150_000_000 + span:
        r = rng.random()
        key = rng.choice(STICK if r < 0.45 else THROTTLE if r < 0.6 else OTHER)
        hold = rng.choice([60, 120, 250, 500, 1000, 2000])
        args += ["--type", "VGAME.EXE+%d:~%d:%s" % (t, hold, key)]
        t += rng.randint(2_000_000, 40_000_000)
    return args


def run(engine, data, name, args, steps, every):
    rundir = os.path.join(WORK, "%s_%s" % (name, engine))
    os.makedirs(rundir, exist_ok=True)
    cmd = [os.path.join(ROOT, "build", "f117run.exe"), "--engine", engine, "--data", data,
           "--save", os.path.join(rundir, "save"), "--log", os.path.join(rundir, "run.log"),
           "--steps", str(steps), "--time-us", "700000000000000", "--hash-every", str(every)] + args
    r = subprocess.run(cmd, capture_output=True, text=True)
    hashes = re.findall(r"^\[hash\] (\d+) ([0-9a-f]+) (\S+)", r.stdout, re.M)
    final = re.search(r"stopped at icount (\d+) .*final hash ([0-9a-f]+)", r.stdout)
    progs = re.findall(r"^\[exec\] (\S+)", open(os.path.join(rundir, "run.log")).read(), re.M)
    return hashes, final.groups() if final else None, progs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--seeds", default="1,2,3,4")
    ap.add_argument("--span", type=int, default=1_500_000_000, help="instructions of random flying")
    ap.add_argument("--every", type=int, default=50_000_000)
    ap.add_argument("--jobs", type=int, default=2)
    a = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    steps = 2_400_000_000 + 150_000_000 + a.span
    bad = 0
    for seed in [int(s) for s in a.seeds.split(",")]:
        name = "seed%d" % seed
        args = random_route(seed, a.span)
        with open(os.path.join(WORK, name + ".args"), "w") as f:
            f.write("\n".join(args + ["--steps", str(steps), "--time-us", "700000000000000"]) + "\n")
        with cf.ThreadPoolExecutor(a.jobs) as ex:
            fi = ex.submit(run, "interp", a.data, name, args, steps, a.every)
            fr = ex.submit(run, "recomp", a.data, name, args, steps, a.every)
            hi, endi, progs = fi.result()
            hr, endr, _ = fr.result()
        first = next(((x, y) for x, y in zip(hi, hr) if x[:2] != y[:2]), None)
        n_keys = sum(1 for x in args if x.startswith("VGAME.EXE+"))
        flew = "VGAME.EXE" in progs
        if first or endi != endr:
            bad += 1
            at = first[0] if first else None
            print("%s: DIFFERENT%s (%d flight inputs; programs %s)" % (
                name, " first at icount %s in %s" % (at[0], at[2]) if at else " at the end",
                n_keys, " ".join(progs)))
        else:
            print("%s: IDENTICAL over %d hashes, final %s/%s (%d flight inputs%s; programs %s)" % (
                name, len(hi), endi[0] if endi else "?", endi[1] if endi else "?", n_keys,
                "" if flew else ", NEVER REACHED FLIGHT", " ".join(progs)))
        sys.stdout.flush()
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
