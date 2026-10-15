#!/usr/bin/env python3
"""hires_subpixel.py LOG [N] [OUT_DIR|-] [--grow G] [--crops DIR] [--no-guard]: Stage 2's sub-pixel re-projection
(docs/presentation.md).

Replays an observer log with the Stage 1 replay (drawlist_frame.py, exact at 320x200) beside a picture N times
finer in each direction, as hires_frame.py does, but fills each model polygon from its true geometry instead of
re-walking the original's integer edges:

* the polygon's edges come from the 'E' records, each tied by its slot to the 'G' record that prepared it and
  that to the two 'V' records of its vertices (camera-space x, y, z and the pixel the original made of each);
  extended G captures preserve exact camera endpoints and origin for all polygons;
* a perspective vertex uses the full fixed-point camera ratio (256 x / z, 256 y / z), before the original
  shifts the numerator and depth or truncates the signed divide. Keeping only the final remainder still
  jumped at depth-shift boundaries; using positive modulo also jumped at negative projected integers.
  The origin ([8602], [8604]) comes from G snapshots, or unclipped edges in legacy logs;
* N = 1 paints exactly the original footprint; at N > 1 ordinary polygons use verified
  geometry clipped to the viewport, with half a fine pixel of inclusive coverage. Clipping
  every fine row to the old coarse staircase produced teeth and is no longer used;
* orthographic coordinates round to the nearest integer, which names a cell centre;
* near-plane polygons retain their separately verified footprint and seam rule.

The original footprint must still agree with the geometry within three coarse pixels;
otherwise the polygon stays scaled. A missing snapshot, an unrelated vertex behind the
camera, or a near crossing that disagrees with its captured coordinates also stays scaled.
The guard excludes verified fine geometry. Fine rectangles survive a blit through an
unseeded page only while its logged coarse bytes still match the saved rectangle.
All fill styles have rules; dither and stipple retain coarse-sized pattern cells
inside the refined boundary. This Python tool handles model polygons; the C runtime also
refines HUD strokes and U-record span polygons. Bitmap lettering and artwork remain original.

The checks from the stage list, per phase at the display page: N = 1 is the replay's own picture; the flat-pixel
agreement (every coarse pixel whose eight neighbours hold its value keeps it on all N x N fine pixels), counted
as the rule leaves it and then after a guard that restores the value where it did not hold (with the number it
restored); and the share of the fine picture that differs from the plain scaled copy. --crops DIR writes
side-by-side PNG crops (scaled copy, hires_frame.py's floor mode, this rule) of the busiest regions of some phases.
"""
from __future__ import annotations

import math
import os
import sys
from collections import Counter

import numpy as np

import drawlist_frame
from drawlist_spans import paint, parse_edge, s16
from hires_frame import Builder, HiPage, WIDTH, HEIGHT


def vertex_fraction(v, ortho=0):
    """The position of a vertex in coarse pixels from a 'V' record (icount, x, y, z, px, py, range, xf_at, px_at):
    the full perspective ratio, or the verified orthographic projection."""
    _, x, y, z, px, py, rng = v[:7]
    if ortho:
        projected = ortho_project((x, y, z), ortho)
        if projected is None or projected[0] != (px, py):
            return None
        return tuple(c + 0.5 for c in projected[1])
    if rng not in (0, 1):
        return None  # orthographic needs G mode metadata
    # Retain the full fixed-point numerator and depth. Keeping only the
    # final divide remainder still jumps when the earlier depth shift changes.
    # This also preserves the sign of negative projected coordinates.
    if z <= 0:
        return None
    return x * 256 / z, y * 256 / z


def ortho_project(camera, shift):
    """120A:08E1: rounded signed high-word shift, with full fine precision."""
    if not 1 <= shift <= 31:
        return None
    if shift < 16:
        coarse = tuple(s16(s16(value >> 16) << (16 - shift)) for value in camera[:2])
        return coarse, coarse
    bits = shift - 16
    coarse = []
    for value in camera[:2]:
        hi = s16(value >> 16)
        coarse.append((hi >> bits) + (bool(hi & (1 << (bits - 1))) if bits else 0))
    return tuple(coarse), tuple(value / (1 << shift) for value in camera[:2])


def near_crossing(front, behind):
    """Original 120A:098C midpoint search, retaining its discarded low byte.

    Addition wraps before the signed shift. The branch tests DEC DX's sign,
    including its wrap at 8000h; it does not simply test the depth's sign.
    """
    for _ in range(64):
        mid = tuple((((a + b + 1 + (1 << 31)) % (1 << 32)) - (1 << 31)) // 2
                    for a, b in zip(front, behind))
        high = (mid[2] >> 16) & 0xFFFF
        if high in (0, 1):
            return tuple((c >> 8) * 2 for c in mid[:2]), tuple(c / 128 for c in mid[:2])
        if (high - 1) & 0x8000:
            behind = mid
        else:
            front = mid
    return None


def snapshot_vertex(g, at):
    zhi = s16(g[at + 2] >> 16)
    rng = 0 if zhi >= 0x100 else 1 if zhi >= 1 else 2
    return (0,) + g[at:at + 5] + (rng,)


class Extents:
    """A convex polygon's left and right extent at a scan line, from its edges. An edge's end points are put in a
    fixed order (by y, then x) before the crossing is computed, so two polygons that share an edge get the
    identical crossing and tile."""

    def __init__(self, edges):
        self.edges = []
        for ax, ay, bx, by in edges:
            if ay == by:
                continue
            if (ay, ax) > (by, bx):
                ax, ay, bx, by = bx, by, ax, ay
            self.edges.append((ax, ay, bx, by, (bx - ax) / (by - ay)))

    def at(self, yc):
        """The extent on the line y = yc, an edge counting from its top end up to but not including its bottom."""
        lo, hi = math.inf, -math.inf
        for ax, ay, bx, by, k in self.edges:
            if ay <= yc < by:
                x = ax + (yc - ay) * k
                if x < lo:
                    lo = x
                if x > hi:
                    hi = x
        return lo, hi

    def over(self, y0, y1):
        """The extent of the polygon over the band y0 <= y <= y1 (every edge clipped to the band)."""
        lo, hi = math.inf, -math.inf
        for ax, ay, bx, by, k in self.edges:
            t, u = max(ay, y0), min(by, y1)
            if t > u:
                continue
            for y in (t, u):
                x = ax + (y - ay) * k
                if x < lo:
                    lo = x
                if x > hi:
                    hi = x
        return lo, hi


# The overshoot, in pixels of the grid being drawn, the original's inclusive spans are allowed: a fine pixel is
# painted when its centre lies within this distance of the true polygon (0 is the pure top-left rule).
GROWS = (0, 0.5, 1, 1.5, 2, 3)


def footprint_rows(poly):
    """{coarse row: (left, right)} of what the original painted, clamped to its viewport."""
    xmin, ymin, xmax, ymax = poly["vp"]
    rows = {}
    for k, (l, r) in enumerate(poly["rows"]):
        y = poly["top"] + k
        if ymin <= y <= ymax:
            l, r = max(l, xmin), min(r, xmax)
            if l <= r:
                rows[y] = (l, r)
    return rows


def fine_cover(rows, edges, n, grow):
    """{fine row: [(left, right)]}: the fine pixels a polygon paints on an N-times grid. `edges` are its true edges in
    fine coordinates, `rows` the original's footprint in coarse pixels. A fine pixel is painted when it lies in the
    footprint and its centre is within `grow` fine pixels of the true polygon (grow 0: left and top edges inclusive,
    right and bottom exclusive), or when its coarse pixel has all eight neighbours in the footprint."""
    ext = Extents(edges)
    out = {}
    for y, (l, r) in rows.items():
        above, below = rows.get(y - 1), rows.get(y + 1)
        inner = None
        if above and below:
            ix0, ix1 = max(l, above[0], below[0]) + 1, min(r, above[1], below[1]) - 1
            if ix0 <= ix1:
                inner = (ix0 * n, ix1 * n + n - 1)
        for j in range(n):
            yc = y * n + j + 0.5
            spans = []
            if grow:
                lo, hi = ext.over(yc - grow, yc + grow)
                a, b = (math.ceil(lo - grow - 0.5), math.floor(hi + grow - 0.5)) if lo != math.inf else (1, 0)
            else:
                lo, hi = ext.at(yc)
                a, b = (math.ceil(lo - 0.5), math.ceil(hi - 0.5) - 1) if lo != math.inf else (1, 0)
            a, b = max(a, l * n), min(b, r * n + n - 1)
            if a <= b:
                spans.append((a, b))
            if inner:
                if spans and spans[0][1] >= inner[0] - 1 and spans[0][0] <= inner[1] + 1:
                    spans = [(min(spans[0][0], inner[0]), max(spans[0][1], inner[1]))]
                else:
                    spans = sorted(spans + [inner])
            if spans:
                out[y * n + j] = spans
    return out


def geometry_cover(edges, n, viewport, grow=0.5):
    """Verified fine geometry, clipped to the viewport rather than coarse stair steps."""
    ext = Extents(edges)
    xmin, ymin, xmax, ymax = viewport
    cover = {}
    if not ext.edges:
        return cover
    top = math.ceil(min(e[1] for e in ext.edges)-grow-0.5)
    bottom = math.floor(max(e[3] for e in ext.edges)+grow-0.5)+1
    for y in range(max(0, ymin*n, top), min(HEIGHT*n, (ymax+1)*n, bottom)):
        lo, hi = ext.over(y+0.5-grow, y+0.5+grow)
        if lo == math.inf:
            continue
        l = max(0, xmin*n, math.ceil(lo-grow-0.5))
        r = min(WIDTH*n-1, (xmax+1)*n-1, math.floor(hi+grow-0.5))
        if l <= r:
            cover[y] = [(l, r)]
    return cover


def protect_geometry(page, rows, cover, n):
    for y, (l, r) in rows.items():
        if 0 <= y < HEIGHT:
            l, r = max(0,l), min(WIDTH-1,r)
            if l <= r:
                page.detail[y*WIDTH+l:y*WIDTH+r+1] = bytes([1])*(r-l+1)
    for y, spans in cover.items():
        for l, r in spans:
            a, b = (y//n)*WIDTH+l//n, (y//n)*WIDTH+r//n
            page.detail[a:b+1] = bytes([1])*(b-a+1)


def choose_grow(rows, edges):
    """The least overshoot (GROWS) with which the polygon's own true edges cover the whole footprint on the
    original's grid, so that N = 1 is the original's picture; None if none in GROWS does."""
    want = sum(r - l + 1 for l, r in rows.values())
    for g in GROWS:
        cover = fine_cover(rows, edges, 1, g)
        if sum(r - l + 1 for spans in cover.values() for l, r in spans) == want:
            return g
    return None


class SubBuilder(Builder):
    def __init__(self, n, grow=None):
        super().__init__(n)
        self.grow = grow            # a fixed overshoot, or None: each polygon's own, from the original's grid
        self.vertices = {}          # px_at -> the latest 'V' record
        self.slots = {}             # edge slot -> the two vertex records its 'G' record named
        self.snapshots = {}         # exact camera records, when the capture supplies them
        self.palette = None
        self.origin = [159, 52]
        self.blits = {}           # unseeded-page fine rectangle cache (at most eight)
        self.coverage = {}          # page identity -> page, geometry, paint, coarse footprint

    def finish_models(self, pages, clear=False):
        """Repair only model geometry left uncovered inside original footprints.
        Keep coverage on a read, and close it before the next drawing layer.
        """
        for key, (page, geometry, painted, foot) in list(self.coverage.items()):
            if not any(page is held for held in pages.values()):
                del self.coverage[key]
                continue
            coarse = np.frombuffer(page, np.uint8, count=WIDTH*HEIGHT).reshape(HEIGHT, WIDTH)
            fine = np.frombuffer(page.hi, np.uint8).reshape(HEIGHT*self.n, WIDTH*self.n)
            scaled = np.repeat(np.repeat(coarse, self.n, axis=0), self.n, axis=1)
            allowed = np.repeat(np.repeat(foot, self.n, axis=0), self.n, axis=1)
            repair = geometry & ~painted & allowed & (fine != scaled)
            self.stats['model seam fine pixels restored'] += int(repair.sum())
            fine[repair] = scaled[repair]
            if clear:
                del self.coverage[key]

    def track_model(self, page, rows, edges, cover, viewport):
        if self.n == 1 or self.fill_colour >> 8 == 0xFB:
            return
        shape = HEIGHT*self.n, WIDTH*self.n
        key = id(page)
        if key not in self.coverage:
            self.coverage[key] = (page, np.zeros(shape, bool), np.zeros(shape, bool),
                                  np.zeros((HEIGHT, WIDTH), bool))
        _, geometry, painted, foot = self.coverage[key]
        for y, (l, r) in rows.items():
            if 0 <= y < HEIGHT:
                foot[y, max(0,l):min(WIDTH,r+1)] = True
        for y, spans in cover.items():
            if 0 <= y < shape[0]:
                for l, r in spans:
                    painted[y, max(0,l):min(shape[1],r+1)] = True
        ext = Extents(edges)
        if not ext.edges:
            return
        xmin, ymin, xmax, ymax = viewport
        lo_y = max(0, ymin*self.n, math.ceil(min(e[1] for e in ext.edges)-0.5))
        hi_y = min(shape[0], (ymax+1)*self.n, math.ceil(max(e[3] for e in ext.edges)-0.5))
        for y in range(lo_y, hi_y):
            lo, hi = ext.at(y+0.5)
            if lo == math.inf:
                continue
            l = max(0, xmin*self.n, math.ceil(lo-0.5))
            r = min(shape[1], (xmax+1)*self.n, math.ceil(hi-0.5))
            if l < r:
                geometry[y, l:r] = True

    def near_edges(self, poly):
        if poly["nclip"] != 2 or len(poly["edges"]) >= 512:
            return None
        origin, cuts, out = None, [], []
        for edge in poly["edges"]:
            g = edge.get("snapshot")
            if g is None:
                return None
            if len(g) >= 18 and g[17]:
                return None
            if origin is None:
                origin = g[3:5]
            elif origin != g[3:5]:
                return None
            ba, bb = bool(g[10]), bool(g[16])
            if ba and bb:
                if not edge["st"] & 0x80:
                    return None
                continue
            if ba or bb:
                if not edge["st"] & 0x40 or "crossing" not in edge or len(cuts) == 2:
                    return None
                front, behind = (11, 5) if ba else (5, 11)
                crossing = near_crossing(g[front:front + 3], g[behind:behind + 3])
                visible = vertex_fraction(snapshot_vertex(g, front))
                if crossing is None or visible is None:
                    return None
                coarse, fine = crossing
                if tuple(c + o for c, o in zip(coarse, origin)) != edge["crossing"]:
                    return None
                cuts.append(tuple(c + o for c, o in zip(fine, origin)))
                a, b = (fine, visible) if ba else (visible, fine)
            else:
                if edge["st"] & 0x40:
                    return None
                a, b = vertex_fraction(snapshot_vertex(g, 5)), vertex_fraction(snapshot_vertex(g, 11))
                if a is None or b is None:
                    return None
            out.append(tuple(c + o for point in (a, b) for c, o in zip(point, origin)))
        if len(cuts) != 2 or len(out) < 2:
            return None
        return out + [cuts[0] + cuts[1]]

    def polygon_edges(self, poly):
        """The true edges in coarse-pixel coordinates (the origin added), or None (the reason counted) when the
        polygon cannot be rebuilt from its vertices."""
        if poly["nclip"]:
            edges = self.near_edges(poly)
            if edges is not None:
                return edges
            self.stats["skipped: near-clipped"] += 1
            return None
        edges = poly["edges"]
        if len(edges) < 3:
            self.stats["skipped: fewer than three edges"] += 1
            return None
        # Exact preparation snapshots carry the origin even when every edge
        # is clipped, and do not depend on the latest V for reused storage.
        snapshots = [e.get("snapshot") for e in edges]
        mode = lambda g: g[17] if len(g) >= 18 else 0
        if all(g is not None and not g[10] and not g[16] and g[3:5] == snapshots[0][3:5]
               and mode(g) == mode(snapshots[0])
               for g in snapshots):
            out = []
            for g in snapshots:
                a, b = vertex_fraction(snapshot_vertex(g, 5), mode(g)), vertex_fraction(snapshot_vertex(g, 11), mode(g))
                if a is None or b is None:
                    self.stats["skipped: vertex without a divisor"] += 1
                    return None
                out.append(tuple(c + o for point in (a, b) for c, o in zip(point, g[3:5])))
            return out
        pairs = []
        for e in edges:
            g = e["g"]
            if not g or not g[0] or not g[1]:
                self.stats["skipped: edge without vertices"] += 1
                return None
            if g[0][6] == 2 or g[1][6] == 2:
                self.stats["skipped: vertex behind the eye"] += 1
                return None
            pairs.append(g)
        votes = Counter()
        for e, (a, b) in zip(edges, pairs):
            if e["st"] & 0xFF == 0:
                votes[(s16(e["x0"]) - a[4], s16(e["y0"]) - a[5])] += 1
        if votes:
            self.origin = list(votes.most_common(1)[0][0])
        ox, oy = self.origin
        out = []
        for a, b in pairs:
            fa, fb = vertex_fraction(a), vertex_fraction(b)
            if fa is None or fb is None:
                self.stats["skipped: vertex without a divisor"] += 1
                return None
            out.append((fa[0] + ox, fa[1] + oy, fb[0] + ox, fb[1] + oy))
        return out

    def lay_spans(self, page, cover, colour):
        n, W = self.n, WIDTH * self.n
        if paint(colour, 0, 0, [0]) is None:
            self.stats["skipped: fill style with no rule"] += 1
            return False
        for hy, spans in cover.items():
            if not 0 <= hy < HEIGHT * n:
                continue
            for l, r in spans:
                l, r = max(l, 0), min(r, W - 1)
                if r < l:
                    continue
                at = hy * W + l
                page.hi[at:at + r - l + 1] = bytes(paint(colour, hy, l, list(page.hi[at:at + r - l + 1]), n))
        return True

    def hook(self, f, pages):
        k = f[0]
        for page in pages.values():
            if isinstance(page, HiPage):
                page.preserve_equal = False
        if k == 'x' and int(f[2]) in (42,46):
            page = pages.get(int(f[3]))
            if isinstance(page, HiPage):
                page.preserve_equal = True
        if k in 'LNQCTDHSMW':
            self.finish_models(pages, clear=True)
        elif k in ('Z', 'Y'):
            self.finish_models(pages)
        if self.suppressed is not None and k not in ("b", "a"):
            self.suppressed.suppress = False
            self.suppressed = None
        if k == "C":
            v = list(map(int, f[2:]))
            _, sx, sy, _, dx, dy, w, ht, sseg, dseg = v[:10]
            src, dst = pages.get(sseg), pages.get(dseg)
            if self.n > 1 and 0 < w <= WIDTH and 0 < ht <= HEIGHT and 0 <= sx <= WIDTH-w and 0 <= sy <= HEIGHT-ht and 0 <= dx <= WIDTH-w and 0 <= dy <= HEIGHT-ht:
                if isinstance(src, HiPage) and dst is None:
                    key = (dseg, dx, dy, w, ht)
                    coarse = b''.join(bytes(src[(sy+y)*WIDTH+sx:(sy+y)*WIDTH+sx+w]) for y in range(ht))
                    fine = b''.join(src.hi[((sy*self.n+y)*WIDTH+sx)*self.n:((sy*self.n+y)*WIDTH+sx+w)*self.n] for y in range(ht*self.n))
                    detail = b''.join(src.detail[(sy+y)*WIDTH+sx:(sy+y)*WIDTH+sx+w] for y in range(ht))
                    if key not in self.blits and len(self.blits) == 8:
                        del self.blits[next(iter(self.blits))]
                    self.blits[key] = coarse, fine, detail
                elif src is None and isinstance(dst, HiPage):
                    saved = self.blits.get((sseg,sx,sy,w,ht))
                    if saved is not None and saved[0] == bytes(v[10:]):
                        _, fine, detail = saved
                        for y in range(ht*self.n):
                            at = ((dy*self.n+y)*WIDTH+dx)*self.n
                            dst.hi[at:at+w*self.n] = fine[y*w*self.n:(y+1)*w*self.n]
                        for y in range(ht):
                            dst.detail[(dy+y)*WIDTH+dx:(dy+y)*WIDTH+dx+w] = detail[y*w:(y+1)*w]
                        dst.suppress = True
                        self.suppressed = dst
        elif k == "V":
            self.vertices[int(f[9], 16)] = (int(f[1]),) + tuple(int(t) for t in f[2:8]) + (int(f[8], 16),)
        elif k == "G":
            slot, a, b = (int(t) for t in f[2:5])
            self.slots[slot] = (self.vertices.get(a), self.vertices.get(b))
            self.snapshots[slot] = tuple(map(int, f[2:20])) if len(f) >= 19 else None
        elif k == "J":
            self.palette = [int(t) for t in f[2:770]]
        elif k == "E":
            e = parse_edge(f)
            e["g"] = self.slots.get(int(f[2]))
            e["snapshot"] = self.snapshots.get(int(f[2]))
            if len(f) >= 10:
                e["crossing"] = tuple(map(int, f[8:10]))
            if self.poly is not None and self.poly["nclip"] == 2 and not self.poly["join"]:
                self.poly["join"].append(e)
            else:
                self.pending.append(e)
        elif k == "F":
            v = [int(x) for x in f[2:]]
            if not self.seen_fill:
                self.seen_fill, self.pending = True, []
                self.poly = None
                return
            self.poly = {"edges": self.pending, "vp": tuple(v[1:5]), "nclip": v[11], "join": [], "done": False}
            self.pending = []
        elif k == "R":
            v = [int(x) for x in f[2:]]
            self.fill_colour = v[3 + 2 * v[2]] & 0xFFFF
            if self.poly is not None:
                self.poly["top"] = v[1]
                self.poly["rows"] = [(v[3 + 2 * i], v[4 + 2 * i]) for i in range(v[2])]
        elif k == "b" and self.poly is not None and not self.poly["done"]:
            self.poly["done"] = True
            page = pages.get(int(f[5]))
            if not isinstance(page, HiPage) or "rows" not in self.poly:
                return
            self.stats["polygons"] += 1
            base = self.polygon_edges(self.poly)
            if base is None:
                self.stats["polygons left nearest"] += 1
                self.finish_models(pages, clear=True)
                return
            rows = footprint_rows(self.poly)
            grow = self.grow if self.grow is not None else choose_grow(rows, base)
            if grow is None:
                self.stats["skipped: footprint beyond %g px of its geometry" % GROWS[-1]] += 1
                self.stats["polygons left nearest"] += 1
                self.finish_models(pages, clear=True)
                return
            self.stats["overshoot %g" % grow] += 1
            n = self.n
            fine_edges = [tuple(c*n for c in e) for e in base]
            geometric = n > 1 and not self.poly['nclip']
            cover = (geometry_cover(fine_edges, n, self.poly['vp']) if geometric
                     else fine_cover(rows, fine_edges, n, grow))
            if not self.lay_spans(page, cover, self.fill_colour):
                self.stats["polygons left nearest"] += 1
                self.finish_models(pages, clear=True)
                return
            if geometric:
                protect_geometry(page, rows, cover, n)
            else:
                self.track_model(page, rows, fine_edges, cover, self.poly['vp'])
            self.stats["footprint pixels"] += sum(r - l + 1 for l, r in rows.values())
            self.stats["footprint fine pixels painted"] += sum(b - a + 1 for spans in cover.values() for a, b in spans)
            self.stats["polygons refilled from vertices"] += 1
            page.suppress = True
            self.suppressed = page


def phase_check(page, n, stats, guard):
    """The stage's checks on one display page, counted into stats; with the guard, a flat pixel whose fine block
    disagrees is restored to its value (and counted), after the rule's own agreement is counted."""
    coarse = np.frombuffer(bytes(page[:WIDTH * HEIGHT]), np.uint8).reshape(HEIGHT, WIDTH)
    fine = np.frombuffer(bytes(page.hi), np.uint8).reshape(HEIGHT * n, WIDTH * n).copy()
    flat = np.zeros((HEIGHT, WIDTH), bool)
    c = coarse[1:-1, 1:-1]
    ok = np.ones(c.shape, bool)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            ok &= coarse[1 + dy:HEIGHT - 1 + dy, 1 + dx:WIDTH - 1 + dx] == c
    flat[1:-1, 1:-1] = ok
    flat &= ~np.frombuffer(page.detail, np.uint8).reshape(HEIGHT, WIDTH).astype(bool)
    scaled = np.repeat(np.repeat(coarse, n, axis=0), n, axis=1)
    differs = fine != scaled
    blocks = differs.reshape(HEIGHT, n, WIDTH, n).sum(axis=(1, 3))        # fine pixels unlike the coarse one, per coarse pixel
    stats["flat coarse pixels"] += int(flat.sum())
    stats["flat pixels that agree"] += int((flat & (blocks == 0)).sum())
    bad = flat & (blocks > 0)
    stats["flat pixels disagreeing"] += int(bad.sum())
    stats["fine pixels in disagreeing flat pixels"] += int(blocks[bad].sum())
    if os.environ.get("HIRES_DEBUG") and bad.any():
        for y, x in list(zip(*np.nonzero(bad)))[:3]:
            print("DEBUG phase %d flat pixel x %d y %d value %d, fine block %s" % (
                stats["display phases"] + 1, x, y, coarse[y, x], fine[y * n:(y + 1) * n, x * n:(x + 1) * n].tolist()))
    if guard and bad.any():
        mask = np.repeat(np.repeat(bad, n, axis=0), n, axis=1)
        fine[mask] = scaled[mask]
        differs = fine != scaled
    stats["fine pixels"] += fine.size
    stats["fine pixels unlike the scaled copy"] += int(differs.sum())
    stats["display phases"] += 1
    if n == 1:
        stats["N = 1 pages equal to the replay's"] += int(not differs.any())
    return fine, scaled


def palette_rgb(pal):
    """The DAC's six-bit components as 8-bit RGB triples (a grey ramp when the log carries no palette)."""
    if not pal:
        return [(v * 4 & 0xFF, v * 3 & 0xFF, v * 2 & 0xFF) for v in range(256)]
    return [tuple((c << 2) | (c >> 4) for c in pal[3 * i:3 * i + 3]) for i in range(256)]


def replay(log, n, builder, keep=()):
    """Replay LOG with a builder; returns the checks' counts and {display phase: (coarse, fine, palette)} for the
    phases named in `keep` (1 is the first display page the log completes)."""
    HiPage.n = n
    stats = Counter()
    kept = {}
    phase = [0]

    def on_phase(kind, dseg, page, z):
        if kind == "Y" and isinstance(page, HiPage):
            phase[0] += 1
            fine, scaled = phase_check(page, n, stats, getattr(builder, "guard", True))
            if phase[0] in keep:
                kept[phase[0]] = (scaled[::n, ::n].copy(), fine, palette_rgb(getattr(builder, "palette", None)))

    drawlist_frame.main(log, page_factory=HiPage, hook=builder.hook, on_phase=on_phase)
    return stats, kept


def render(index, rgb, scale):
    """An index picture as an RGB image scaled up by whole pixels (nearest neighbour)."""
    from PIL import Image
    img = Image.fromarray(np.array(rgb, np.uint8)[index], "RGB")
    return img if scale == 1 else img.resize((img.width * scale, img.height * scale), Image.NEAREST)


def busiest(fine, scaled, n, count, box=(60, 38)):
    """The `count` non-overlapping windows (in coarse pixels, left/top/width/height) where the fine picture
    differs most from the scaled copy."""
    diff = (fine != scaled).reshape(HEIGHT, n, WIDTH, n).sum(axis=(1, 3)).astype(np.int64)
    bw, bh = box
    cum = diff.cumsum(0).cumsum(1)
    cum = np.pad(cum, ((1, 0), (1, 0)))
    score = cum[bh:, bw:] - cum[:-bh, bw:] - cum[bh:, :-bw] + cum[:-bh, :-bw]
    picks = []
    for _ in range(count):
        y, x = np.unravel_index(int(score.argmax()), score.shape)
        if score[y, x] <= 0:
            break
        picks.append((int(x), int(y), bw, bh, int(score[y, x])))
        score[max(0, y - bh + 1):y + bh, max(0, x - bw + 1):x + bw] = -1
    return picks


def write_crops(log, n, outdir, name, phases=None, grow=None, count=2, shots=3):
    """Side-by-side crops of the busiest regions of some display phases: the scaled copy, hires_frame.py's
    floor mode, this rule, and where the rule differs from the scaled copy. Also each phase's whole picture."""
    from PIL import Image, ImageDraw
    os.makedirs(outdir, exist_ok=True)
    sub = SubBuilder(n, grow)
    allphases = range(1, 1000)
    _, kept_sub = replay(log, n, sub, keep=allphases)
    if phases is None:      # the phases where the rule changes most of the picture
        def changed(ph):
            c, f, _ = kept_sub[ph]
            return int((f != np.repeat(np.repeat(c, n, axis=0), n, axis=1)).sum())
        ranked = sorted(kept_sub, key=changed, reverse=True)[:shots]
        phases = sorted(ranked)
    floor = Builder(n, floor=True)
    _, kept_floor = replay(log, n, floor, keep=phases)
    written = []
    for ph in phases:
        if ph not in kept_sub:
            continue
        coarse, fine, rgb = kept_sub[ph]
        _, ffine, _ = kept_floor[ph]
        scaled = np.repeat(np.repeat(coarse, n, axis=0), n, axis=1)
        full = Image.new("RGB", (WIDTH * n, HEIGHT * n))
        full.paste(render(fine, rgb, 1))
        path = os.path.join(outdir, "%s_n%d_phase%02d_full_subpixel.png" % (name, n, ph))
        full.save(path)
        written.append(path)
        for k, (x, y, w, h, score) in enumerate(busiest(fine, scaled, n, count)):
            sc = max(1, 12 // n)
            def crop(img):
                return img[y * n:(y + h) * n, x * n:(x + w) * n]
            panels = [render(crop(scaled), rgb, sc), render(crop(ffine), rgb, sc), render(crop(fine), rgb, sc)]
            mark = np.where(crop(fine) != crop(scaled), 1, 0)
            dm = np.zeros(mark.shape + (3,), np.uint8)
            dm[...] = (30, 30, 30)
            dm[mark == 1] = (255, 60, 60)
            panels.append(Image.fromarray(dm, "RGB").resize((mark.shape[1] * sc, mark.shape[0] * sc), Image.NEAREST))
            pw, ph_ = panels[0].size
            sheet = Image.new("RGB", (4 * pw + 3 * 8, ph_ + 22), (0, 0, 0))
            draw = ImageDraw.Draw(sheet)
            for i, (img, label) in enumerate(zip(panels, ("original scaled (nearest)", "floor mode", "sub-pixel rule", "sub-pixel minus scaled"))):
                sheet.paste(img, (i * (pw + 8), 22))
                draw.text((i * (pw + 8) + 4, 5), "%s  [N=%d phase %d, x %d y %d]" % (label, n, ph, x, y), fill=(255, 255, 255))
            path = os.path.join(outdir, "%s_n%d_phase%02d_crop%d.png" % (name, n, ph, k + 1))
            sheet.save(path)
            written.append(path)
    return written


def main():
    args = list(sys.argv[1:])
    if not args:
        sys.exit(__doc__)
    grow, guard, crops = None, True, None
    for opt in ("--grow", "--crops"):
        if opt in args:
            i = args.index(opt)
            val = args[i + 1]
            del args[i:i + 2]
            if opt == "--grow":
                grow = float(val)
            else:
                crops = val
    if "--no-guard" in args:
        args.remove("--no-guard")
        guard = False
    log = args[0]
    n = int(args[1]) if len(args) > 1 else 2
    if crops:
        name = os.path.splitext(os.path.basename(log))[0]
        for path in write_crops(log, n, crops, name, None, grow):
            print(path)
        return
    builder = SubBuilder(n, grow)
    builder.guard = guard
    stats, kept = replay(log, n, builder)
    for k, v in sorted(builder.stats.items()):
        print("%-46s %d" % (k, v))
    for k in ("display phases", "flat coarse pixels", "flat pixels that agree", "flat pixels disagreeing",
              "fine pixels in disagreeing flat pixels", "fine pixels", "fine pixels unlike the scaled copy"):
        print("%-46s %d" % (k, stats[k]))
    if n == 1:
        print("%-46s %d" % ("N = 1 pages equal to the replay's", stats["N = 1 pages equal to the replay's"]))
    flat = stats["flat coarse pixels"]
    if flat:
        print("N = %d, overshoot %s: rule alone %.3f%% of flat coarse pixels keep their value on all %d fine pixels "
              "(the guard restores %d); %.3f%% of the fine picture differs from the scaled copy" % (
                  n, "per polygon" if grow is None else "%g" % grow,
                  100.0 * stats["flat pixels that agree"] / flat, n * n, stats["flat pixels disagreeing"],
                  100.0 * stats["fine pixels unlike the scaled copy"] / max(1, stats["fine pixels"])))


if __name__ == "__main__":
    main()
