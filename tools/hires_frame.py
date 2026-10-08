#!/usr/bin/env python3
"""hires_frame.py LOG [N] [OUT_DIR|-] [--floor]: Stage 2's first build (docs/presentation.md).

Replays an observer log (the Stage 1 replay, drawlist_frame.py, exact at 320x200)
and keeps beside every page a picture N times finer in each direction. Every
write the replay makes to a page is mirrored as an N x N block (nearest
neighbour) except a model polygon's fill: its edges are re-walked on the finer
grid (the original's edge walk with the endpoints and clip rows scaled by N)
and the fill's style is applied to the fine rows, so a polygon's edges fall on
fine pixels. A polygon whose fine rows would not agree with the coarse ones
where no edge crosses (the clipped ones, docs/presentation.md) keeps the
nearest-neighbour fill.

The check from the stage list: at N = 1 the picture is Stage 1's, and at N > 1
every N x N block agrees with the N = 1 pixel wherever no edge crosses it,
here taken as the coarse pixels whose eight neighbours hold the same value.
The report counts those pixels at each phase's end and what share of the fine
picture differs from the plain scaled copy (the edges gained), and writes the
last display page as a binary PPM when OUT_DIR is given (the palette is a
grey ramp: the log carries none).
"""
from __future__ import annotations

import os
import sys
from collections import Counter

import drawlist_frame
from drawlist_spans import Spans, parse_edge, paint, rows_of, s16

WIDTH, HEIGHT = 320, 200


class HiPage(bytearray):
    """A 64 KB page that mirrors the first 320x200 bytes on an N-times finer grid."""
    n = 2

    def __init__(self, data):
        super().__init__(data)
        n = self.n
        self.suppress = False
        self.hi = bytearray(WIDTH * n * HEIGHT * n)
        for y in range(HEIGHT):
            row = bytes(b for b in self[y * WIDTH:(y + 1) * WIDTH] for _ in range(n)) if len(self) >= (y + 1) * WIDTH else bytes(WIDTH * n)
            for j in range(n):
                at = (y * n + j) * WIDTH * n
                self.hi[at:at + WIDTH * n] = row

    def _mirror(self, a, v):
        if 0 <= a < WIDTH * HEIGHT:
            n = self.n
            y, x = divmod(a, WIDTH)
            block = bytes([v]) * n
            for j in range(n):
                at = (y * n + j) * WIDTH * n + x * n
                self.hi[at:at + n] = block

    def copy_hi_rect(self, src, sx, sy, dx, dy, w, h):
        """A copy from another replayed page: the fine rows come across with the coarse ones."""
        n, W = self.n, WIDTH * self.n
        for y in range(h):
            if not (0 <= sy + y < HEIGHT and 0 <= dy + y < HEIGHT):
                continue
            x0, x1 = max(sx, 0), min(sx + w, WIDTH)
            for j in range(n):
                s_at = ((sy + y) * n + j) * W + x0 * n
                d_at = ((dy + y) * n + j) * W + (dx + x0 - sx) * n
                if 0 <= dx + x0 - sx and dx + x1 - sx <= WIDTH:
                    self.hi[d_at:d_at + (x1 - x0) * n] = src.hi[s_at:s_at + (x1 - x0) * n]

    def __setitem__(self, key, value):
        if isinstance(key, slice):
            idx = range(*key.indices(len(self)))
            values = list(value)
            super().__setitem__(key, bytes(values))
            if not self.suppress:
                for i, v in zip(idx, values):
                    self._mirror(i, v)
        else:
            super().__setitem__(key, value)
            if not self.suppress:
                self._mirror(key, value)


def scaled_edge(e, n):
    out = dict(e)
    for k in ("x0", "y0", "x1", "y1"):
        out[k] = (s16(e[k]) * n) & 0xFFFF
    for k in ("y0hi", "y1hi"):
        out[k] = s16(e[k]) * n
    return out


def fine_rows(edges, ymin, ymax, n):
    """The N-times finer span rows of a polygon, and the coarse ones, as {y: (l, r)}."""
    lo, hi = Spans(ymin, ymax), Spans(ymin * n, ymax * n + n - 1)
    for e in edges:
        lo.poly_edge(e)
        hi.poly_edge(scaled_edge(e, n))

    def table(sp):
        out = {}
        for k, (l, r) in enumerate(rows_of(sp)):
            if l != 0x7FFF and r != -0x7FFF and l <= r:
                out[sp.top + k] = (l, r)
        return out
    coarse, fine = table(lo), table(hi)
    if not fine:
        return coarse, fine
    # The scaled edges land on the top-left corner of a coarse pixel; the pixel's far side is N - 1 fine
    # pixels on, so every row's right end and the last row's lower side move out by that much.
    fine = {y: (l, r + n - 1) for y, (l, r) in fine.items()}
    last = max(fine)
    for j in range(1, n):
        fine[last + j] = fine[last]
    # Independent walks of neighbouring polygons leave cracks along clipped edges, so the coarse pixels
    # no edge can cross (all eight neighbours inside) are covered whole, and a fine row the walk does not
    # reach takes the plain scaled span; only the pixels next to an edge come from the walk.
    out = {}
    for y, (l, r) in coarse.items():
        rows = [coarse.get(y + d) for d in (-1, 0, 1)]
        inner = None
        if all(rows):
            ix0, ix1 = max(t[0] for t in rows) + 1, min(t[1] for t in rows) - 1
            if ix0 <= ix1:
                inner = (ix0 * n, ix1 * n + n - 1)
        for j in range(n):
            hy = y * n + j
            span = fine.get(hy)
            if span is None:
                span = (l * n, r * n + n - 1)
            elif inner:
                span = (min(span[0], inner[0]), max(span[1], inner[1]))
            out[hy] = span
    for hy, span in fine.items():
        out.setdefault(hy, span)
    return coarse, out


def agrees(coarse, fine, n):
    """hires_spans.py's rule: a coarse pixel with all eight neighbours inside is covered on all N x N fine pixels."""
    def inside(spans, x, y):
        row = spans.get(y)
        return row is not None and row[0] <= x <= row[1]
    for y, (l, r) in coarse.items():
        for x in range(l + 1, r):
            if all(inside(coarse, x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                if not all(inside(fine, x * n + i, y * n + j) for i in range(n) for j in range(n)):
                    return False
    return True


class Builder:
    def __init__(self, n, floor=False):
        self.n = n
        self.floor = floor          # a polygon's fine fill is laid over its nearest-neighbour fill, never taking coverage from it
        self.overlay = None
        self.pending = []
        self.poly = None            # {'edges', 'rows', 'ok'} between its fill entry and its painting
        self.join_open = None
        self.seen_fill = False
        self.fill_colour = None
        self.suppressed = None      # the page whose mirroring is paused while a fine fill's rows arrive
        self.stats = Counter()

    def hook(self, f, pages):
        k = f[0]
        if self.suppressed is not None and k not in ("b", "a"):
            self.suppressed.suppress = False
            self.suppressed = None
        if self.overlay is not None and k not in ("b", "a"):
            page, fine, colour = self.overlay
            self.overlay = None
            self.lay(page, fine, colour)
        if k == "E":
            # the near-clip join is the one edge the fill entry adds, when two crossings were collected
            if self.poly is not None and self.poly["nclip"] == 2 and not self.poly["join"]:
                self.poly["join"].append(parse_edge(f))
            else:
                self.pending.append(parse_edge(f))
        elif k == "F":
            v = [int(x) for x in f[2:]]
            if not self.seen_fill:
                self.seen_fill, self.pending = True, []
                self.poly = None
                return
            ymin, ymax, nclip = v[2], v[4], v[11]
            self.poly = {"edges": self.pending, "ymin": ymin, "ymax": ymax, "nclip": nclip, "join": [], "done": False}
            self.pending = []
        elif k == "R":
            v = [int(x) for x in f[2:]]
            self.fill_colour = v[3 + 2 * v[2]] & 0xFFFF
        elif k == "b" and self.poly is not None and not self.poly["done"]:
            self.poly["done"] = True
            es = int(f[5])
            page = pages.get(es)
            if not isinstance(page, HiPage):
                return
            edges = self.poly["edges"] + self.poly["join"]
            coarse, fine = fine_rows(edges, self.poly["ymin"], self.poly["ymax"], self.n)
            self.stats["polygons"] += 1
            if not coarse or not agrees(coarse, fine, self.n):
                self.stats["polygons left nearest"] += 1
                return
            if self.floor:
                self.overlay = (page, fine, self.fill_colour)
                self.stats["polygons refilled finer"] += 1
                return
            if not self.lay(page, fine, self.fill_colour):
                return
            self.stats["polygons refilled finer"] += 1
            page.suppress = True
            self.suppressed = page
        elif k == "b" and self.suppressed is not None:
            pass


    def lay(self, page, fine, colour):
        n, W = self.n, WIDTH * self.n
        for hy, (l, r) in fine.items():
            if not 0 <= hy < HEIGHT * n:
                continue
            l, r = max(l, 0), min(r, W - 1)
            if r < l:
                continue
            at = hy * W + l
            got = paint(colour, hy, l, list(page.hi[at:at + r - l + 1]))
            if got is None:
                self.stats["polygons in a style with no rule"] += 1
                return False
            page.hi[at:at + r - l + 1] = bytes(got)
        return True


def check(page, n, stats):
    """Count the coarse pixels whose 3x3 neighbourhood is one value and how many of them keep that value on
    all N x N fine pixels; and the share of fine pixels that differ from the plain scaled copy."""
    W = WIDTH * n
    for y in range(1, HEIGHT - 1):
        for x in range(1, WIDTH - 1):
            v = page[y * WIDTH + x]
            if all(page[(y + dy) * WIDTH + x + dx] == v for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                stats["flat coarse pixels"] += 1
                ok = all(page.hi[(y * n + j) * W + x * n + i] == v for j in range(n) for i in range(n))
                stats["flat pixels that agree"] += ok
    differing = 0
    for y in range(HEIGHT):
        for j in range(n):
            row = page.hi[(y * n + j) * W:(y * n + j + 1) * W]
            for x in range(WIDTH):
                v = page[y * WIDTH + x]
                differing += sum(1 for i in range(n) if row[x * n + i] != v)
    stats["fine pixels"] += W * HEIGHT * n
    stats["fine pixels unlike the scaled copy"] += differing


def write_ppm(page, n, path):
    W, H = WIDTH * n, HEIGHT * n
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        f.write(b"".join(bytes((v * 4 & 0xFF, v * 3 & 0xFF, v * 2 & 0xFF)) for v in page.hi))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    log = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    out = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != "-" else None
    floor = "--floor" in sys.argv
    HiPage.n = n
    builder = Builder(n, floor)
    stats = Counter()
    last = {}

    def on_phase(kind, dseg, page, z):
        if kind == "Y" and isinstance(page, HiPage):
            check(page, n, stats)
            stats["display phases"] += 1
            last["page"] = page

    drawlist_frame.main(log, page_factory=HiPage, hook=builder.hook, on_phase=on_phase)
    for k, v in sorted(builder.stats.items()):
        print("%-34s %d" % (k, v))
    for k in ("display phases", "flat coarse pixels", "flat pixels that agree", "fine pixels", "fine pixels unlike the scaled copy"):
        print("%-34s %d" % (k, stats[k]))
    if stats["flat coarse pixels"]:
        print("N = %d: %.3f%% of flat coarse pixels keep their value on all %d fine pixels; %.3f%% of the fine picture differs "
              "from the scaled copy" % (n, 100.0 * stats["flat pixels that agree"] / stats["flat coarse pixels"], n * n,
                                        100.0 * stats["fine pixels unlike the scaled copy"] / max(1, stats["fine pixels"])))
    if out and "page" in last:
        os.makedirs(out, exist_ok=True)
        write_ppm(last["page"], n, os.path.join(out, "display_%dx.ppm" % n))


if __name__ == "__main__":
    main()
