#!/usr/bin/env python3
"""compare_intro.py - 86Box's intro pictures against this machine's.

    py tools/ref86box/compare_intro.py CAP_DIR OURS_SHOTS_DIR [--ips 9000000]

CAP_DIR is capture_intro.py's output (frames.json and PNGs). OURS_SHOTS_DIR
holds this project's picture snapshots, shot_<clock>.ppm (video_compare.py
writes them under its run directory). Ours are reduced to their distinct
pictures first. Each 86Box picture is then matched to the nearest of ours,
searching forward from the previous match, and the report gives:

  exact       86Box's picture has the same 6-bit DAC values as one of ours at
              every pixel. The two expand them to 8 bits differently: 86Box
              floor(v*255/63), ours v<<2|v>>4, and v>>2 recovers v from both
  close       mean absolute difference under 0.75 DAC levels (3 of 8 bits) per channel (not a
              different picture)
  unmatched   anything else
  order       whether the matches never go backwards in our timeline

Nothing here depends on how fast either machine is: it asks whether the same
pictures appear in the same order. Timing is reported separately (the VM is
a 6 MHz 286, so its times are not comparable yet).
"""
import argparse
import hashlib
import json
import os
import sys

import numpy as np
from PIL import Image


def load_ours(shots, ips):
    names = sorted(f for f in os.listdir(shots) if f.endswith(".ppm"))
    out, last = [], None
    for n in names:
        path = os.path.join(shots, n)
        with open(path, "rb") as f:
            raw = f.read()
        digest = hashlib.sha256(raw).hexdigest()
        if digest == last:
            continue
        last = digest
        clock = int(n.rsplit("_", 1)[1].split(".")[0])
        out.append((clock / ips, path))
    return out


def arr(path):
    """The picture as 6-bit DAC values."""
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16) >> 2


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cap")
    ap.add_argument("ours")
    ap.add_argument("--ips", type=int, default=9_000_000)
    ap.add_argument("--window", type=int, default=400, help="how far ahead in our pictures to look")
    a = ap.parse_args()
    ref = json.load(open(os.path.join(a.cap, "frames.json")))["frames"]
    ours = load_ours(a.ours, a.ips)
    print("86Box: %d pictures; ours: %d distinct pictures" % (len(ref), len(ours)))
    cache = {}

    def ours_arr(i):
        if i not in cache:
            cache[i] = arr(ours[i][1])
        return cache[i]
    pos, exact, close, unmatched, back = 0, 0, 0, 0, 0
    rows = []
    for f in ref:
        if f["size"] != [320, 200]:
            rows.append((f["file"], "text", None)); continue
        img = arr(os.path.join(a.cap, f["file"]))
        best, best_i = None, None
        for i in range(max(0, pos - 20), min(len(ours), pos + a.window)):
            o = ours_arr(i)
            if o.shape != img.shape:
                continue
            d = float(np.abs(o - img).mean())
            if best is None or d < best:
                best, best_i = d, i
                if d == 0:
                    break
        if best is None:
            unmatched += 1; rows.append((f["file"], "no candidate", None)); continue
        if best_i < pos - 20:
            back += 1
        if best == 0:
            exact += 1; kind = "exact"
        elif best < 0.75:
            close += 1; kind = "close"
        else:
            unmatched += 1; kind = "unmatched"
        if kind != "unmatched":
            pos = max(pos, best_i)
        rows.append((f["file"], kind, (best, best_i, ours[best_i][0])))
    graphics = len([f for f in ref if f["size"] == [320, 200]])
    print("graphics pictures %d: exact %d, close %d, unmatched %d; backwards matches %d" %
          (graphics, exact, close, unmatched, back))
    for name, kind, info in rows:
        if kind in ("close", "unmatched", "no candidate"):
            print("  %s %s %s" % (name, kind, "" if info is None else "best diff %.3f" % info[0]))
    print("order preserved: %s" % ("yes" if back == 0 else "NO"))
    return 0 if unmatched == 0 and back == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
