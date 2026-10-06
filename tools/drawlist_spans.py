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


def paint(colour, y, x0, before):
    """A fill row in the original's style (VGAME 0x14279 dispatches on the
    colour word's high byte): FF solid, FE AND, FD OR, FB discard (the rows
    are cleared and nothing is painted), FC stipple, else a dither of the
    two colour bytes."""
    lo, hi = colour & 0xFF, (colour >> 8) & 0xFF
    if hi == 0xFF:
        return [lo] * len(before)
    if hi == 0xFE:
        return [b & lo for b in before]
    if hi == 0xFD:
        return [b | lo for b in before]
    if hi == 0xFB:
        return list(before)
    return None                                       # stipple and dither: not replayed yet


def line_pixels(x0, y0, x1, y1):
    """Graphics entry 31, the library's line: the pixels it writes, as (x, y),
    by its documented rules - an unsigned sort on x, the single-pixel case
    decided by "were the two x's equal", a half-step error term, and the
    major-axis step through a linear 320-byte-row address."""
    ax, bx, cx, dx = x0, y0, x1, y1
    xeq = (ax & 0xFFFF) == (cx & 0xFFFF)
    if (ax & 0xFFFF) > (cx & 0xFFFF):
        ax, cx, bx, dx = cx, ax, dx, bx
    if xeq and bx == dx:
        return [(ax, bx)]
    si, bp = 1, 320
    cx, dx = s16(cx - ax), s16(dx - bx)
    if dx < 0:
        bp, dx = -bp, -dx
    if (cx & 0xFFFF) < (dx & 0xFFFF):
        si, bp, dx, cx = bp, si, cx, dx
    major, minor = cx, dx
    di = bx * 320 + ax
    err = s16(-(((major + 1) & 0xFFFF) >> 1))
    si -= 1
    out = []
    n = major
    while True:
        out.append((di % 320, di // 320))
        di += 1
        n -= 1
        if n < 0:
            break
        di += si
        err = s16(err + minor)
        if err < 0:
            continue
        err = s16(err - major)
        di += bp
    return out


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
    fill = None               # the painting in progress: its colour and the bytes before and after
    first_pix = []
    line_colour, pending_line, first_line = None, None, []
    pending_spans = None
    first_span = []
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
        elif f[0] == "X":
            stats["entry %d" % int(f[2])] += 1
        elif f[0] == "K":
            line_colour = int(f[2])
        elif f[0] == "N":
            v = [int(x) for x in f[2:]]
            pending_line = (v[0], v[1], v[2], v[3], line_colour)
        elif f[0] == "Q":
            v = [int(x) for x in f[2:]]
            if v[3]:                                 # a fill with no rows (first row negative) draws nothing
                pending_spans = (v[0], v[1], v[2], [(v[4 + 2 * k], v[5 + 2 * k]) for k in range(v[3])], line_colour)
                pending_line = None
        elif f[0] == "n" and pending_spans is not None:
            v = [int(x) for x in f[2:]]
            ya, yb, mode, rows, colour = pending_spans
            want = {}
            for k, (l, r) in enumerate(rows):
                y = ya + k
                if r < l or (r == l and r in (0, 0x13F)):
                    continue
                for x in range(l, r + 1):
                    want[(x, y)] = mode
            ok = True
            for k in range(v[2]):
                x, y, now, before = v[4 + 4 * k] & 0xFFFF, v[4 + 4 * k] >> 16, v[5 + 4 * k], v[6 + 4 * k]
                md = want.get((x, y))
                exp = None if md is None else (colour if md == 0 else before | colour if md == 1 else
                                               before & colour if md == 2 else ((before & 0x0E) >> 1) | 0x98)
                if exp != now:
                    ok = False
                    if len(first_span) < 6:
                        first_span.append((f[1], ya, yb, mode, colour, x, y, now, before, md, rows[max(0, y - ya - 1):y - ya + 2]))
                    break
            stats["library span fills"] += 1
            stats["library span fills exact"] += ok
            stats["span mode %d" % mode] += 1
            pending_spans = None
        elif f[0] == "n" and pending_line is not None:
            v = [int(x) for x in f[2:]]
            # page, 0, count, then per pixel: offset, x | y << 16, value, value before
            changed = [(v[4 + 4 * k] & 0xFFFF, v[4 + 4 * k] >> 16, v[5 + 4 * k]) for k in range(v[2])]
            x0, y0, x1, y1, colour = pending_line
            mine = set(line_pixels(x0, y0, x1, y1))
            ok = all((x, y) in mine and val == colour for x, y, val in changed)
            stats["lines"] += 1
            stats["lines exact"] += ok
            if not ok and len(first_line) < 4:
                bad = [(x, y, val) for x, y, val in changed if (x, y) not in mine or val != colour]
                first_line.append((f[1], (x0, y0, x1, y1), colour, len(changed), bad[:5]))
            pending_line = None
        elif f[0] == "b" and fill is not None:
            v = [int(x) for x in f[2:]]
            fill["before"].append(v)
        elif f[0] == "a" and fill is not None:
            v = [int(x) for x in f[2:]]
            k = len(fill["after"])
            fill["after"].append(v)
            if k < len(fill["before"]):
                y, x0, n = fill["before"][k][:3]               # then the segment and offset, then the bytes
                got = paint(fill["colour"], y, x0, fill["before"][k][5:5 + n])
                ok = got == v[5:5 + n]
                stats["pixel rows"] += 1
                stats["pixel rows exact"] += ok
                st = "%02X" % ((fill["colour"] >> 8) & 0xFF)
                stats["rows " + st] += 1
                stats["rows exact " + st] += ok
                if not ok and len(first_pix) < 4:
                    first_pix.append((f[1], st, y, x0, fill["before"][k][5:5 + min(n, 8)], v[5:5 + min(n, 8)], got[:8]))
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
            fill = {"colour": v[3 + 2 * nrows] & 0xFFFF, "before": [], "after": []}
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
    r = stats["pixel rows"]
    print("%d painted rows of pixels: exact for %d (%.1f%%)" % (r, stats["pixel rows exact"], 100.0 * stats["pixel rows exact"] / max(r, 1)))
    for k in sorted(x for x in stats if x.startswith("rows ") and not x.startswith("rows exact")):
        st = k.split()[1]
        print("   style %s: %d rows, %d exact" % (st, stats[k], stats["rows exact " + st]))
    print("%d library lines: every changed pixel on the replayed line and in its colour for %d (%.1f%%)" % (
        stats["lines"], stats["lines exact"], 100.0 * stats["lines exact"] / max(stats["lines"], 1)))
    print("%d library span fills: every changed pixel on a replayed span with its mode's value for %d (%.1f%%); modes %s" % (
        stats["library span fills"], stats["library span fills exact"],
        100.0 * stats["library span fills exact"] / max(stats["library span fills"], 1),
        {k: stats[k] for k in sorted(stats) if k.startswith("span mode")}))
    print("other graphics entries called:", ", ".join("%s x%d" % (k.split()[1], stats[k]) for k in sorted(
        (k for k in stats if k.startswith("entry ")), key=lambda k: -stats[k])))
    for b in first_span:
        print("  span fill at clock %s rows %d..%d mode %d colour %s: pixel (%d,%d) now %d before %d, replay mode %s; rows around it %s" % b)
    for b in first_line:
        print("  line at clock %s %s colour %s: %d pixels changed, off the replay: %s" % b)
    for b in first_pix:
        print("  pixels at clock %s, style %s, row %d from x %d: before %s after %s replay %s" % b)
    for b in first_bad:
        print("  at clock %s (%d join edges): top %s vs %s; rows %s vs %s; %d vs %d rows" % b)


if __name__ == "__main__":
    main()
