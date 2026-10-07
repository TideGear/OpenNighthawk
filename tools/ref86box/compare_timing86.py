#!/usr/bin/env python3
"""compare_timing86.py - when the intro's scenes change, this machine against 86Box.

    py tools/ref86box/compare_timing86.py OURS_comparison.json 86BOX_frames.csv

OURS is the comparison.json of a video_compare.py run (a picture list with
times), the other the frames.csv of a traced 86Box run (trace_86box.ps1; every
displayed frame with its emulated microseconds and a hash). Both are reduced to
the pictures that stay on screen for a second or more (the logo and title
cards, the credits' pages) with their start times; 86Box's list starts at its
first mode-13h frame, ours at the first picture, so the first pair fixes the
offset. Pairs are matched in order by duration; judged: how many are paired,
the largest difference in a scene's duration, and the largest drift of a
start time against the first pair. Exit 0 when within the limits.
"""
import csv
import json
import sys

# Measured 7 Oct 2026 on the 386DX/33: 19 paired, 0.23 s, 1.80 s. The drift is made in the loading phases
# (+0.4 s and +0.9 s), which the disk interface (ISA port I/O at the 86Box's bus speed) paces, not the CPU:
# 40 MHz changes it by 0.1 s. The 6 MHz 286 drifted 8.35 s, a 25 MHz 286 2.45 s.
LIMITS = dict(min_paired=17, max_duration_diff=0.35, max_drift=2.2)


def ours_scenes(path):
    d = json.load(open(path))
    return [(s["time"], s["end"] - s["time"]) for s in d["ours"] if s["end"] - s["time"] >= 1.0]


def box_scenes(path):
    out, prev, start, width = [], None, 0.0, ""
    for frame, _tsc, us, w, _h, h, *_ in csv.reader(open(path)):
        t = int(us) / 1e6
        if h != prev:
            if prev is not None and t - start >= 1.0 and width == "640":
                out.append((start, t - start))
            prev, start, width = h, t, w
    return out


def pair(ours, box):
    """Greedy in-order pairs whose durations agree within a quarter second plus 15%."""
    pairs, j = [], 0
    off = None
    for s, d in box:
        for k in range(j, min(j + 3, len(ours))):
            if abs(ours[k][1] - d) <= 0.25 + 0.15 * d:
                if off is None:
                    off = s - ours[k][0]
                pairs.append((ours[k], (s, d)))
                j = k + 1
                break
    return pairs, off


def main():
    ours, box = ours_scenes(sys.argv[1]), box_scenes(sys.argv[2])
    pairs, off = pair(ours, box)
    if not pairs:
        print("timing 86box   FAIL  no scenes paired (ours %d, 86Box %d)" % (len(ours), len(box)))
        return 1
    dur = max(abs(o[1] - b[1]) for o, b in pairs)
    drift = max(abs((b[0] - o[0]) - off) for o, b in pairs)
    ok = len(pairs) >= LIMITS["min_paired"] and dur <= LIMITS["max_duration_diff"] and drift <= LIMITS["max_drift"]
    print("timing 86box   %s  %d of %d scenes (ours %d) paired (need %d), longest-scene duration difference %.2f s (max %.2f), "
          "start drift %.2f s (max %.1f)" % ("PASS" if ok else "FAIL", len(pairs), len(box), len(ours), LIMITS["min_paired"],
                                           dur, LIMITS["max_duration_diff"], drift, LIMITS["max_drift"]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
