#!/usr/bin/env python3
"""remaster_wide_check.py - the widescreen rule: a wider frame shows more world, not a different one.

    py tools/remaster_wide_check.py NARROW.ppm WIDE.ppm [--tolerance 10]

Both files are `f117run --remaster-shot` outputs of the same composition: NARROW at the default 1280 columns, WIDE with
F117_RM_WIDTH set. Each file holds the render (left half) and the original frame (right half); only the render is
compared. The middle 1280 columns of WIDE must equal NARROW to within the tolerance (the sky's dither noise differs
with the width). Prints the count of pixels that differ and exits 1 when any does.
"""
import argparse
import sys

from PIL import Image


def render(path, columns):
    im = Image.open(path)
    return im.crop((0, 0, columns, im.height)), im.width // 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("narrow")
    ap.add_argument("wide")
    ap.add_argument("--tolerance", type=int, default=10)
    a = ap.parse_args()
    n_im = Image.open(a.narrow)
    w_im = Image.open(a.wide)
    n_cols, w_cols = n_im.width // 2, w_im.width // 2
    rows = min(n_im.height, w_im.height)
    off = (w_cols - n_cols) // 2
    narrow = n_im.crop((0, 0, n_cols, rows)).getdata()
    middle = w_im.crop((off, 0, off + n_cols, rows)).getdata()
    bad = sum(1 for p, q in zip(narrow, middle) if max(abs(p[k] - q[k]) for k in range(3)) > a.tolerance)
    print(f"{bad} of {n_cols * rows} pixels differ by more than {a.tolerance} levels")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
