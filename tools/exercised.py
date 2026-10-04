#!/usr/bin/env python3
"""exercised.py - how much of the game the parity routes actually run.

    py tools/exercised.py --data DIR [--run] [--route FILE ...]

Parity is measured by playing routes under both engines; it says nothing
about code no route reaches. This runs each route under the interpreter
with --coverage (which then records every instruction executed inside a
known module), and reports, per module, the share of its code area
(tools/census.py) the routes executed, together and route by route.

Without --run it reads the files of an earlier --run.
"""
from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "recompiler"))

from census import code_area, code_spans              # noqa: E402
from discover import Discovery                        # noqa: E402
from modules import load_all                          # noqa: E402

WORK = os.path.join(os.path.expanduser("~"), "f117-recomp-local", "exec")


def run_routes(routes, data):
    os.makedirs(WORK, exist_ok=True)
    for r in routes:
        name = os.path.splitext(os.path.basename(r))[0]
        cov = os.path.join(WORK, name + ".cov")
        if os.path.exists(cov):
            os.remove(cov)
        print("running %s" % name, flush=True)
        subprocess.run([sys.executable, os.path.join(HERE, "run_route.py"), r, "--data", data,
                        "--engine", "interp", "--out", tempfile.mkdtemp(prefix=name + "_", dir=WORK),
                        "--", "--coverage", cov], check=True, stdout=subprocess.DEVNULL)


def executed(path, mods):
    """Image bytes covered by an executed instruction, per module."""
    by_key = {(m.name, m.file_hash): m for m in mods}
    out = {m.name: bytearray(len(m.image)) for m in mods}
    for line in open(path):
        p = line.split()
        if len(p) < 4:
            continue
        m = by_key.get((p[0].upper(), int(p[1], 16)))
        if not m:
            continue
        off = m.off(int(p[2], 16), int(p[3], 16))
        if 0 <= off < len(m.image):
            out[m.name][off] = 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--run", action="store_true", help="run the routes first")
    ap.add_argument("--route", action="append", default=None)
    a = ap.parse_args()
    routes = a.route or sorted(glob.glob(os.path.join(HERE, "routes", "*.args")))
    if a.run:
        run_routes(routes, a.data)
    mods = load_all(a.data)
    names = [os.path.splitext(os.path.basename(r))[0] for r in routes]
    per = {}
    for n in names:
        p = os.path.join(WORK, n + ".cov")
        if os.path.exists(p):
            per[n] = executed(p, mods)
    tot_area = tot_run = 0
    print("%-14s %8s %8s %6s   %s" % ("module", "area", "run", "share", "  ".join("%14s" % n for n in per)))
    for m in mods:
        d = Discovery(m, log=lambda *x: None)
        regions = d.run()
        covered = bytearray(len(m.image))
        lens = {}
        for r in regions:
            for ip, ins in r.insns.items():
                off = m.off(r.seg, ip)
                lens[off] = max(lens.get(off, 0), ins.length)
                covered[off:off + ins.length] = b"\1" * ins.length
        spans = code_spans(m, covered) if (m.kind == "exe" and m.dgroup) else [code_area(m)]
        area = sum(hi - lo for lo, hi in spans)

        def run_bytes(starts):
            b = bytearray(len(m.image))
            for off, s in enumerate(starts):
                if s:
                    n = lens.get(off, 1)
                    b[off:off + n] = b"\1" * n
            return sum(sum(b[lo:hi]) for lo, hi in spans)

        union = bytearray(len(m.image))
        cols = []
        for n in per:
            s = per[n][m.name]
            for i, v in enumerate(s):
                if v:
                    union[i] = 1
            cols.append(run_bytes(s))
        total = run_bytes(union)
        tot_area += area
        tot_run += total
        print("%-14s %8d %8d %5.1f%%   %s" % (m.name, area, total, 100.0 * total / area if area else 0,
                                          "  ".join("%14d" % c for c in cols)))
    print("all modules: %d of %d code-area bytes executed by the routes (%.1f%%)" % (
        tot_run, tot_area, 100.0 * tot_run / tot_area if tot_area else 0))


if __name__ == "__main__":
    main()
