#!/usr/bin/env python3
"""remaster_diff.py - what the Remastered 3D view lacks or changes against the Enhanced picture, region by region.

    py tools/remaster_diff.py ENHANCED.ppm REMASTERED.ppm [--rows N] [--threshold T] [--top K] [--out FILE.png]

Both pictures are the app's fine pictures (320n x 200n, the same size; F117_PRESENT_SHOTS writes them). Pixels of the 3D view
(the first --rows rows, by default the game's clip window of the takeoff view, 107 game rows) that differ by more than T
(sum of the three channel differences, default 60) are grouped into connected regions. Each region is reported in the game's own
320 x 200 coordinates with its area and the mean colour on each side, largest first, so a missing object (the sea strip, a
building) shows up as a large region with very different colours, and the smoothing of the sky bands (small differences
everywhere) does not.

The Remastered renderer is meant to differ in a few ways: smoother gradients, no whole-pixel flattening, sub-pixel edges. Those
make many tiny low-contrast regions. Anything with a large area or a large colour difference is a candidate defect.
Exit status 1 when a region larger than --fail-area (default 400 fine pixels) and with a colour difference above --fail-colour
(default 90) is found, so the check can sit in a script.
"""
import argparse
import sys

import numpy as np
from PIL import Image, ImageDraw


def read(path):
    return np.array(Image.open(path).convert("RGB")).astype(int)


def label(mask):
    """Connected components (4-neighbour) of a boolean mask; returns (labels, count). Uses scipy when present."""
    try:
        from scipy import ndimage
        return ndimage.label(mask)
    except ImportError:
        pass
    h, w = mask.shape
    labels = np.zeros((h, w), dtype=int)
    count = 0
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not labels[y, x]:
                count += 1
                stack = [(y, x)]
                labels[y, x] = count
                while stack:
                    cy, cx = stack.pop()
                    for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not labels[ny, nx]:
                            labels[ny, nx] = count
                            stack.append((ny, nx))
    return labels, count


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("enhanced")
    ap.add_argument("remastered")
    ap.add_argument("--rows", type=int, default=107, help="game rows of the 3D view to compare (default 107)")
    ap.add_argument("--threshold", type=int, default=60)
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--out", help="write an annotated picture (remastered, regions boxed)")
    ap.add_argument("--fail-area", type=int, default=400)
    ap.add_argument("--fail-colour", type=int, default=90)
    a = ap.parse_args()
    e, r = read(a.enhanced), read(a.remastered)
    if e.shape != r.shape:
        sys.exit("the pictures differ in size: %s against %s" % (e.shape, r.shape))
    n = e.shape[1] // 320
    rows = min(e.shape[0], a.rows * n)
    diff = np.abs(e[:rows] - r[:rows]).sum(axis=2)
    mask = diff > a.threshold
    lab, count = label(mask)
    found = []
    for k in range(1, count + 1):
        ys, xs = np.nonzero(lab == k)
        area = len(ys)
        me, mr = e[:rows][ys, xs].mean(axis=0), r[:rows][ys, xs].mean(axis=0)
        found.append((area, xs.min(), xs.max(), ys.min(), ys.max(), me, mr, float(np.abs(me - mr).sum())))
    found.sort(key=lambda f: -f[0])
    print("%d x %d fine pixels (n = %d); 3D rows 0..%d; %d regions above %d" % (e.shape[1], e.shape[0], n, rows - 1, count, a.threshold))
    print("%8s  %-27s  %-13s  %-13s  %s" % ("area", "game pixels x0..x1, y0..y1", "enhanced", "remastered", "colour diff"))
    bad = 0
    for area, x0, x1, y0, y1, me, mr, cd in found[:a.top]:
        flag = ""
        if area >= a.fail_area and cd >= a.fail_colour:
            flag = "  <-- candidate defect"
            bad += 1
        print("%8d  x %3d..%3d  y %3d..%3d       #%02X%02X%02X       #%02X%02X%02X       %5.0f%s" % (
            area, x0 // n, x1 // n, y0 // n, y1 // n, *[int(v) for v in me], *[int(v) for v in mr], cd, flag))
    if a.out:
        im = Image.fromarray(r.astype("uint8"))
        d = ImageDraw.Draw(im)
        for area, x0, x1, y0, y1, me, mr, cd in found[:a.top]:
            d.rectangle([x0, y0, x1, y1], outline=(255, 255, 0))
        im.save(a.out)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
