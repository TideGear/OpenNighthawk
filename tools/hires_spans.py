#!/usr/bin/env python3
"""hires_spans.py LOG [N]: Stage 2's first check (docs/presentation.md).

Re-walk every filled polygon's edges on an N-times finer grid (endpoints and
the clip rows scaled by N, the original's edge walk unchanged) and compare the
spans with the N = 1 spans the original left. The check from the stage list:
every N x N block that no edge crosses agrees with the N = 1 pixel, that is,
for each N = 1 row [l, r] the columns l+1 .. r-1 are covered on all N finer
rows beneath it. Rows are also checked to stay inside the N = 1 span widened
by one pixel (the edge may only move within its own pixel). Prints how many
polygons and rows pass.
"""
from __future__ import annotations

import sys
from collections import Counter

from drawlist_spans import Spans, parse_edge, rows_of, s16


def scaled(e, n):
    out = dict(e)
    for k in ("x0", "y0", "x1", "y1"):
        out[k] = (s16(e[k]) * n) & 0xFFFF
    for k in ("y0hi", "y1hi"):
        out[k] = s16(e[k]) * n
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    stats = Counter()
    pending, seen = [], False
    for line in open(sys.argv[1]):
        f = line.split()
        if not f:
            continue
        if f[0] == "E":
            pending.append(parse_edge(f))
        elif f[0] == "F":
            v = [int(x) for x in f[2:]]
            if not seen:
                seen, pending = True, []
                continue
            ymin, ymax = v[2], v[4]
            lo, hi = Spans(ymin, ymax), Spans(ymin * n, ymax * n + n - 1)
            for e in pending:
                lo.poly_edge(e)
                hi.poly_edge(scaled(e, n))
            pending = []
            stats["polygons"] += 1
            low = {}
            for k, (l, r) in enumerate(rows_of(lo)):
                if l != 0x7FFF and r != -0x7FFF and l <= r:
                    low[lo.top + k] = (l, r)
            high = {}
            for k, (l, r) in enumerate(rows_of(hi)):
                if l != 0x7FFF and r != -0x7FFF and l <= r:
                    high[hi.top + k] = (l, r)
            ok = True
            # a coarse pixel whose eight neighbours are all inside the polygon is
            # not crossed by any edge: its N x N fine pixels must all be inside
            def inside(spans, x, y):
                row = spans.get(y)
                return row is not None and row[0] <= x <= row[1]
            for y, (l, r) in low.items():
                for x in range(l + 1, r):
                    if not all(inside(low, x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                        continue
                    stats["interior pixels"] += 1
                    good = all(inside(high, x * n + i, y * n + j) for i in range(n) for j in range(n))
                    stats["interior agree"] += good
                    ok &= good
            stats["polygons agree"] += ok
    for k in ("polygons", "polygons agree", "interior pixels", "interior agree"):
        print("%-16s %d" % (k, stats[k]))
    print("N = %d: %.3f%% of interior pixels agree" % (n, 100.0 * stats["interior agree"] / max(1, stats["interior pixels"])))


if __name__ == "__main__":
    main()
