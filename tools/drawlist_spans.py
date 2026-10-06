#!/usr/bin/env python3
"""drawlist_spans.py LOG: replay each filled polygon's edges into span rows and
compare them with the rows the original left for its fill.

LOG is `f117run --observe FILE:FROM:TO` output. For every filled polygon (the
'E' edge records since the last 'F'), the edges are scan-converted by the rules
below - the original's model edge walk (VGAME 1377:072B) and polygon
accumulator (130D:004A), written from their documented behaviour - into an
empty span table, and the rows from the top row down are compared with the
'F' record's rows, which were read from the machine at the fill's entry. The
accumulator (status OR, left/right y ranges, near-clip count) is compared too.

This is Stage 1's first exactness check (docs/presentation.md): the draw list
is complete for a polygon's edges when the replay matches every row.
"""
from __future__ import annotations

import sys
from collections import Counter

NOLEFT, NORIGHT = 0x7FFF, -0x7FFF  # 7FFFh and 8001h as signed words


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Spans:
    def __init__(self, ymin, ymax):
        self.left, self.right = {}, {}
        self.top = NOLEFT
        self.ymin, self.ymax = ymin, ymax
        # the accumulator (130D)
        self.flags = 0
        self.lmin, self.lmax, self.rmin, self.rmax = NOLEFT, -0x8000, NOLEFT, -0x8000
        self.drew = 0
        self.nclip = 0

    def widen(self, y, x):
        if x < self.left.get(y, NOLEFT):
            self.left[y] = x
        if x > self.right.get(y, NORIGHT):
            self.right[y] = x

    def widen_left(self, y, x):
        if x < self.left.get(y, NOLEFT):
            self.left[y] = x

    def widen_right(self, y, x):
        if x > self.right.get(y, NORIGHT):
            self.right[y] = x

    def span_edge(self, x0, y0, x1, y1):
        if x0 > x1:
            x0, x1, y0, y1 = x1, x0, y1, y0
        y = y0
        dx = s16(x1 - x0)
        step, ylo, yhi = (1, y0, y1) if y1 >= y0 else (-1, y1, y0)
        # the whole edge is dropped, not clipped
        if ylo < self.ymin or ylo > self.ymax or yhi < self.ymin or yhi > self.ymax:
            return
        if ylo < self.top:
            self.top = ylo
        dy = s16(yhi - ylo)
        x = x0
        if dy > dx:                                  # steep: one row a step
            cx = dy + 1
            err = s16(-(cx >> 1))
            while True:
                self.widen_left(y, x)
                self.widen_right(y, x)
                err = s16(err + dx)
                if err >= 0:
                    cx -= 1
                    if cx == 0:
                        return
                    err = s16(err - dy)
                    x += 1
                    y += step
                else:
                    y += step
                    cx -= 1
                    if cx == 0:
                        return
        cx = dx + 1                                  # shallow: several x a row
        err = s16(-(cx >> 1))
        while True:
            self.widen_left(y, x)
            while True:
                err = s16(err + dy)
                if err >= 0:
                    break
                x += 1
                cx -= 1
                if cx == 0:
                    self.widen_right(y, x - 1)
                    return
            self.widen_right(y, x)
            err = s16(err - dx)
            cx -= 1
            if cx == 0:
                return
            x += 1
            y += step

    def side(self, left, a, b):
        if b < a:
            a, b = b, a
        if left:
            self.lmax = max(self.lmax, b)
            self.lmin = min(self.lmin, a)
        else:
            self.rmax = max(self.rmax, b)
            self.rmin = min(self.rmin, a)

    def run(self, x, a, b):
        """1377:07E8: one vertical run at column x over rows a..b (borders)."""
        if b < a:
            a, b = b, a
        if b <= self.ymin or a >= self.ymax:
            return
        a, b = max(a, self.ymin), min(b, self.ymax)
        n = b - a
        if n == 0:
            return
        if a < self.top:
            self.top = a
        for y in range(a, a + n + 1):
            self.widen_left(y, x)
            self.widen_right(y, x)

    def poly_edge(self, e):
        x0, y0, x1, y1, st = e["x0"], e["y0"], e["x1"], e["y1"], e["st"]
        self.flags |= st
        if st & 0x40:
            self.nclip += 1
        lo = st & 0xFF
        if lo & 0x80:                                # rejected: borders only
            if lo & 3:
                self.side(lo & 2, e["y0hi"], e["y1hi"])
            return
        self.drew |= 4
        self.span_edge(s16(x0), s16(y0), s16(x1), s16(y1))
        if lo & 3:
            self.side(lo & 2, s16(y0), e["y0hi"])
        if lo & 0x0C:
            self.side(lo & 8, s16(y1), e["y1hi"])


def rows_of(sp):
    mine = []
    if sp.top != NOLEFT:
        y = sp.top
        while sp.left.get(y, NOLEFT) != NOLEFT or sp.right.get(y, NORIGHT) != NORIGHT:
            mine.append((sp.left.get(y, NOLEFT), sp.right.get(y, NORIGHT)))
            y += 1
    return mine


def parse_edge(f):
    v = [int(x) for x in f[2:]]
    return {"x0": v[1] & 0xFFFF, "y0": v[2] & 0xFFFF, "x1": v[3] & 0xFFFF, "y1": v[4] & 0xFFFF,
            "st": v[5] & 0xFFFF, "y0hi": s16(v[2] >> 16), "y1hi": s16(v[4] >> 16)}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pending = []              # edges since the last fill
    poly = None               # the polygon between its fill entry ('F') and its painting ('R')
    stats = Counter()
    first_bad = []
    seen_fill = False         # the first polygon may have edges from before the log's window
    for line in open(sys.argv[1]):
        f = line.split()
        if not f:
            continue
        if f[0] == "E":
            # the near-clip join is the one edge the fill entry adds, and only
            # when two crossings were collected; any other edge is the next polygon's
            if poly is not None and poly["nclip"] == 2 and not poly["join"]:
                poly["join"].append(parse_edge(f))
            else:
                pending.append(parse_edge(f))
        elif f[0] == "F":
            v = [int(x) for x in f[2:]]
            if not seen_fill:
                seen_fill = True
                pending = []
                continue
            xmin, ymin, xmax, ymax = v[1:5]
            flags, lmin, lmax, rmin, rmax, drew, nclip = v[5:12]
            top, nrows = v[12], v[13]
            rows = [(v[14 + 2 * k], v[15 + 2 * k]) for k in range(nrows)]
            sp = Spans(ymin, ymax)
            for e in pending:
                sp.poly_edge(e)
            ok_rows = rows_of(sp) == rows and (sp.top if rows else NOLEFT) == (top if rows else NOLEFT)
            ok_acc = (sp.flags & 0xFFFF, sp.lmin, sp.lmax, sp.rmin, sp.rmax, sp.drew, sp.nclip) ==                      (flags & 0xFFFF, lmin, lmax, rmin, rmax, drew, nclip)
            stats["polygons"] += 1
            stats["rows exact"] += ok_rows
            stats["accumulator exact"] += ok_acc
            stats["rows"] += len(rows)
            poly = {"sp": sp, "xmin": xmin, "xmax": xmax, "join": [], "clock": f[1], "nclip": nclip}
            pending = []
        elif f[0] == "R" and poly is not None:
            v = [int(x) for x in f[2:]]
            top, nrows = v[1], v[2]
            want = [(v[3 + 2 * k], v[4 + 2 * k]) for k in range(nrows)]
            sp = poly["sp"]
            for e in poly["join"]:                   # 130D:0116 with two crossings: the join edge
                sp.poly_edge(e)
            fl = sp.flags
            if sp.drew:
                if fl & 5:
                    sp.run(poly["xmax"] + 1, sp.rmax, sp.rmin)
                if fl & 10:
                    sp.run(poly["xmin"] - 1, sp.lmax, sp.lmin)
            elif (fl & 5) and (fl & 10):
                sp.run(poly["xmax"] + 1, sp.rmax, sp.rmin)
                sp.run(poly["xmin"] - 1, sp.lmax, sp.lmin)
            mine = rows_of(sp)
            ok = mine == want and (sp.top if want else NOLEFT) == (top if want else NOLEFT)
            stats["painted"] += 1
            stats["painted exact"] += ok
            stats["joins"] += bool(poly["join"])
            stats["joins exact"] += bool(poly["join"]) and ok
            if not ok and len(first_bad) < 5:
                first_bad.append((poly["clock"], len(poly["join"]), sp.top, top, mine[:3], want[:3], len(mine), len(want)))
            poly = None
    p = stats["polygons"]
    print("%d filled polygons, %d span rows from their edges: exact for %d (%.1f%%), accumulator exact for %d (%.1f%%)" % (
        p, stats["rows"], stats["rows exact"], 100.0 * stats["rows exact"] / max(p, 1),
        stats["accumulator exact"], 100.0 * stats["accumulator exact"] / max(p, 1)))
    q = stats["painted"]
    print("%d painted, with the join and the borders: exact for %d (%.1f%%); %d had a near-clip join, %d of them exact" % (
        q, stats["painted exact"], 100.0 * stats["painted exact"] / max(q, 1), stats["joins"], stats["joins exact"]))
    for b in first_bad:
        print("  at clock %s (%d join edges): top %s vs %s; rows %s vs %s; %d vs %d rows" % b)


if __name__ == "__main__":
    main()
