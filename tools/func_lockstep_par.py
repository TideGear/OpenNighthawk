#!/usr/bin/env python3
"""func_lockstep_par.py - func_lockstep in parallel shards, merged into one report.

    py tools/func_lockstep_par.py [--exe build/func_lockstep.exe] [--jobs N] [func_lockstep options]

Each routine draws its random states from a stream of its own (the run's seed,
its module and address), so routine k tests the same states whichever process
runs it: the shards (`--shard K/N`, every Nth routine of the table) together
test exactly what one run does. Their output is merged in table order, each
routine's detail lines with it, and the tallies are summed into func_lockstep's
own last line, so callers read one report as before. Exit 1 if any shard
reports a mismatch, 2 if a shard fails to run.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TALLY = re.compile(r"matched routines: (\d+), (\d+) states compared \((\d+) finished by the original code\), "
                   r"(\d+) skipped, (\d+) mismatching")


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "func_lockstep.exe"))
    ap.add_argument("--jobs", type=int, default=max(1, min(16, (os.cpu_count() or 2) // 2)))
    a, rest = ap.parse_known_args()
    a.exe = os.path.abspath(a.exe)
    n = max(1, a.jobs)
    procs = []
    for k in range(n):
        out = tempfile.TemporaryFile(mode="w+")
        procs.append((subprocess.Popen([a.exe] + rest + ["--shard", "%d/%d" % (k, n)], stdout=out,
                                       stderr=subprocess.STDOUT, text=True), out))
    blocks, totals, routines, failed = [], [0, 0, 0, 0], None, False
    for proc, out in procs:
        proc.wait()
        out.seek(0)
        pending = []
        tally = None
        for line in out.read().splitlines():
            m = re.match(r"@(\d+) (.*)", line)
            t = TALLY.match(line)
            if m:
                blocks.append((int(m.group(1)), pending + [m.group(2)]))
                pending = []
            elif t:
                tally = t
            else:
                pending.append(line)
        if proc.returncode not in (0, 1) or tally is None:
            failed = True
            print("\n".join(pending))
            print("shard %d failed (exit %s)" % (procs.index((proc, out)), proc.returncode))
            continue
        if pending:                       # lines after the last routine (none expected)
            blocks.append((1 << 30, pending))
        routines = int(tally.group(1))
        for i in range(4):
            totals[i] += int(tally.group(2 + i))
    for _, lines in sorted(blocks, key=lambda b: b[0]):
        print("\n".join(lines))
    if failed or routines is None:
        sys.exit(2)
    print("matched routines: %d, %d states compared (%d finished by the original code), %d skipped, %d mismatching"
          % (routines, totals[0], totals[1], totals[2], totals[3]))
    sys.exit(1 if totals[3] else 0)


if __name__ == "__main__":
    main()
