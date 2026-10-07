#!/usr/bin/env python3
"""instr86.py - instructions executed between the intro's scenes, this machine against 86Box.

    py tools/ref86box/instr86.py OURS_comparison.json 86BOX_frames.csv

Pairs the intro's long scenes as compare_timing86.py does, then for each pair of consecutive
paired scenes compares the guest instructions executed in between: this machine's clock count
(icount, from the shot's file name; one clock an instruction plus I/O delays) against 86Box's
instruction counter (the trace patch's last frames.csv column) and the emulated time each took.
The ratio says how many instructions a real-chip model (86Box's 386DX/33) spends where this
model spends one, and what MIPS rate the game's own code runs at on it. Only the span from the
first paired scene on is used: before it this machine runs no DOS or BIOS code at all.
"""
import csv
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_timing86 as ct  # noqa: E402

IPS = 9_000_000


def ours_scenes(path):
    d = json.load(open(path))
    out = []
    for s in d["ours"]:
        if s["end"] - s["time"] >= 1.0:
            m = re.search(r"shot_(\d+)\.ppm", s["source"])
            out.append((s["time"], s["end"] - s["time"], int(m[1]) if m else None))
    return out


def box_scenes(path):
    out, prev, start, width, start_ins = [], None, 0.0, "", 0
    for row in csv.reader(open(path)):
        frame, _tsc, us, w, _h, h, ins = row[:7]
        t = int(us) / 1e6
        if h != prev:
            if prev is not None and t - start >= 1.0 and width == "640":
                out.append((start, t - start, start_ins))
            prev, start, width, start_ins = h, t, w, int(ins)
    return out


def main():
    ours, box = ours_scenes(sys.argv[1]), box_scenes(sys.argv[2])
    pairs, _ = ct.pair([(a, b) for a, b, _ in ours], [(a, b) for a, b, _ in box])
    # re-attach the counters to the paired scenes
    by_o = {(a, b): c for a, b, c in ours}
    by_b = {(a, b): c for a, b, c in box}
    rows = [(by_o[o], by_b[b], o[0], b[0]) for o, b in pairs]
    print("%-3s %14s %14s %9s %9s %8s" % ("#", "ours clocks", "86Box instr", "ours s", "86Box s", "ratio"))
    tot_o = tot_b = tot_to = tot_tb = 0
    for i in range(1, len(rows)):
        do, db = rows[i][0] - rows[i - 1][0], rows[i][1] - rows[i - 1][1]
        to, tb = rows[i][2] - rows[i - 1][2], rows[i][3] - rows[i - 1][3]
        tot_o += do; tot_b += db; tot_to += to; tot_tb += tb
        print("%-3d %14d %14d %9.2f %9.2f %8.3f" % (i, do, db, to, tb, db / do if do else 0))
    print("total: ours %.1f M clocks in %.1f s (%.2f M/s), 86Box %.1f M instructions in %.1f s (%.2f MIPS); "
          "86Box executes %.3f instructions per clock of this model" % (
              tot_o / 1e6, tot_to, tot_o / tot_to / 1e6, tot_b / 1e6, tot_tb, tot_b / tot_tb / 1e6, tot_b / tot_o))


if __name__ == "__main__":
    main()
