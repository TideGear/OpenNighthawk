#!/usr/bin/env python3
"""interp_frame.py LOG [options]: Stage 3 as a 320x200 study (docs/presentation.md).

Interpolates between consecutive logic frames of an observer log (f117run --observe, with
F117R_OBSERVE_PAGES=1), and draws the in-between frames with the Stage 1 replay
(drawlist_frame.main: the original's own rasteriser rules, at 320x200).

A *logic frame* is the draw list of one flight-model step: the phases (game_draw calls, 'P'
records) the log holds between two steps, each with the page dumps at its start. The picture a
logic frame leaves is the display page ('Y') after its last phase. Frame n is paired with frame
n+1 and an in-between frame at t (0 < t < 1) is built from a *skeleton*, the draw list of the
nearer frame (n for t < 0.5, n + 1 for t >= 0.5), in which every primitive that pairs with one
in the other frame is moved the fraction w of the way to it (w = t from n, 1 - t from n + 1),
and everything that does not pair, or is not smooth, stays as the skeleton has it (it switches
at t = 0.5). At t = 0 and t = 1 nothing moves, so the output is the replay of that frame itself.

What pairs:
  * model polygons: the batches (the vertices one model chunk projects, 'V' records from
    C6B4) are aligned between the frames by their vertex counts, then painted polygons are
    paired inside an aligned batch by their edge slots; their edges (E records) move, the span
    rows are rebuilt by the original's edge walk (the same Spans rules drawlist_spans.py proves
    exact) and painted in the fill's style;
  * span fills (Q), outline edges (L) and library lines (N): see the functions below.
Everything else (text, sprites, blits, ticks, page copies) is held.
"""
from __future__ import annotations

import sys
from collections import Counter

import drawlist_frame
from drawlist_spans import Spans, NOLEFT, NORIGHT, rows_of, s16


# ---------------------------------------------------------------------------------------------
# the log


class Rec:
    __slots__ = ("kind", "line", "_v")

    def __init__(self, kind, line):
        self.kind, self.line, self._v = kind, line, None

    @property
    def v(self):
        if self._v is None:
            f = self.line.split()
            if self.kind == "V":
                self._v = [int(x) for x in f[2:8]] + [int(f[8], 16), int(f[9], 16)]
            else:
                self._v = [int(x) for x in f[2:]]
        return self._v

    @property
    def icount(self):
        return int(self.line.split(None, 2)[1])


class Phase:
    """One game_draw call: its start dumps (the raw 'Z', 'Y' and 'J' lines) and the records after it."""

    def __init__(self, icount):
        self.icount = icount
        self.zline = self.yline = self.aline = None
        self.recs: list[Rec] = []

    @property
    def zseg_origin(self):
        f = self.zline.split(None, 4)
        return int(f[2]), int(f[3])


def read_log(path):
    phases: list[Phase] = []
    for line in open(path):
        k = line[:1]
        if k == "P":
            phases.append(Phase(int(line.split()[1])))
        elif not phases:
            continue
        elif k == "Z":
            phases[-1].zline = line
        elif k == "Y":
            phases[-1].yline = line
        elif k == "J":
            phases[-1].aline = line
        elif k.strip():
            phases[-1].recs.append(Rec(k, line))
    return [p for p in phases if p.zline and p.yline]


# ---------------------------------------------------------------------------------------------
# the draw list of a phase: batches and polygons


def parse_edge_v(v):
    """An 'E' record's values (slot, x0, y0, x1, y1, status, the crossing's x, y) as the
    edge dict drawlist_spans.Spans.poly_edge reads."""
    return {"slot": v[0], "x0": v[1] & 0xFFFF, "y0": v[2] & 0xFFFF, "x1": v[3] & 0xFFFF, "y1": v[4] & 0xFFFF,
            "st": v[5] & 0xFFFF, "y0hi": s16(v[2] >> 16), "y1hi": s16(v[4] >> 16)}


class Poly:
    """A painted model polygon: its edges as they reached the rasteriser, the fill's records."""

    def __init__(self):
        self.batch = None        # the batch it belongs to (index in the frame)
        self.edges = []          # E records (dicts) before the fill
        self.join = []           # the near-clip join edge(s), after the fill entry
        self.f = None            # 'F' values
        self.r = None            # 'R' values
        self.ri = None           # index of the 'R' record
        self.rows = []           # the 'b' records (y, x0, n, es, at) of this polygon
        self.bi = []             # their indexes in the frame's records
        self.ai = []             # indexes of the 'a' records
        self.colour = None       # the colour word the fill uses ([8606])


class Batch:
    def __init__(self, idx):
        self.idx = idx
        self.verts = []          # V values
        self.edges = {}          # slot -> (vertex k of the first end, of the second)
        self.polys: list[Poly] = []
        self.lines = []          # indexes of the outline edges ('L') drawn from this batch

    @property
    def sig(self):
        return len(self.verts)


def parse_phase(ph):
    """The batches, polygons and outline edges of a phase or frame (anything with .recs), with
    their positions in ph.recs. Mirrors drawlist_spans.main's polygon bookkeeping."""
    batches: list[Batch] = []
    pending = []
    poly = None
    fill_poly = None
    cur = None
    seen_fill = False
    for i, rec in enumerate(ph.recs):
        k = rec.kind
        if k == "V":
            v = rec.v
            if v[-2] == 0xC6B4 or cur is None:
                cur = Batch(len(batches))
                batches.append(cur)
            cur.verts.append(v)
        elif k == "G":
            v = rec.v
            if cur is not None:
                cur.edges[v[0]] = ((v[1] - 0xD6B4) // 8, (v[2] - 0xD6B4) // 8)
        elif k == "L":
            if cur is not None:
                cur.lines.append(i)
        elif k == "E":
            e = parse_edge_v(rec.v)
            if poly is not None and poly["nclip"] == 2 and not poly["p"].join:
                poly["p"].join.append(e)
            else:
                pending.append(e)
        elif k == "F":
            v = rec.v
            if not seen_fill:
                seen_fill = True
                pending = []
                poly = None
                continue
            p = Poly()
            p.batch = cur.idx if cur else None
            p.edges, p.f = pending, v
            pending = []
            poly = {"p": p, "nclip": v[11]}
        elif k == "R" and poly is not None:
            p = poly["p"]
            p.r, p.ri = rec.v, i
            p.colour = p.r[3 + 2 * p.r[2]] & 0xFFFF
            fill_poly = p
            if cur is not None:
                cur.polys.append(p)
            poly = None
        elif k == "b" and fill_poly is not None:
            fill_poly.rows.append(tuple(rec.v[:5]))
            fill_poly.bi.append(i)
        elif k == "a" and fill_poly is not None:
            fill_poly.ai.append(i)
    return batches


def poly_rows(p, edges=None, join=None):
    """The span rows of a polygon from its edges by the original's rules (drawlist_spans.main's
    'R' step): (top, [(left, right)]). edges / join default to the polygon's own."""
    edges = p.edges if edges is None else edges
    join = p.join if join is None else join
    xmin, ymin, xmax, ymax = p.f[1:5]
    sp = Spans(ymin, ymax)
    for e in edges:
        sp.poly_edge(e)
    for e in join:
        sp.poly_edge(e)
    fl = sp.flags
    if sp.drew:
        if fl & 5:
            sp.run(xmax + 1, sp.rmax, sp.rmin)
        if fl & 10:
            sp.run(xmin - 1, sp.lmax, sp.lmin)
    elif (fl & 5) and (fl & 10):
        sp.run(xmax + 1, sp.rmax, sp.rmin)
        sp.run(xmin - 1, sp.lmax, sp.lmin)
    return sp.top, rows_of(sp)


def original_rows(p):
    nrows = p.r[2]
    return p.r[1], [(p.r[3 + 2 * k], p.r[4 + 2 * k]) for k in range(nrows)]


# ---------------------------------------------------------------------------------------------
# drawing a draw list with the Stage 1 replay


class _Lines:
    """What drawlist_frame.main opens in place of a file: a fixed list of lines."""

    def __init__(self, lines):
        self.lines = lines

    def __iter__(self):
        return iter(self.lines)


def replay(start_z, start_y, lines):
    """Run the Stage 1 replay (drawlist_frame.main itself) on the page dumps START_Z / START_Y
    (the raw 'Z' and 'Y' lines) plus LINES, and return the work page and the display page it
    ends with. A sentinel dump pair at the end makes main hand the pages to the hook."""
    import contextlib
    import io
    got = {}

    def on_phase(kind, seg, page, dump):
        got["Z" if kind == "Z" else "Y"] = bytes(page)

    zf = start_z.split(None, 4)
    sent = ["Z 0 %s %s" % (zf[2], zf[3]) + " 0" * 8 + "\n", "Y 0 40960 0 0 0\n"]
    flat = []
    for line in lines:
        flat.extend(line.splitlines(True))
    drawlist_frame.open = lambda path: _Lines([start_z, start_y] + flat + sent)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            drawlist_frame.main("-", on_phase=on_phase)
    finally:
        del drawlist_frame.open
    return got["Z"], got["Y"]


def dump_bytes(line):
    return bytes(int(x) for x in line.split()[4:])


# ---------------------------------------------------------------------------------------------
# pairing


def project(xf):
    """The original's model_project (VGAME 0x129A2) for a camera-space vertex of three 32-bit
    coordinates: the pixel pair, or None behind the eye or when the divide would fault."""
    x, y, z = xf

    def idiv(n, d):
        if d == 0:
            return None
        q = abs(n) // abs(d)
        q = q if (n < 0) == (d < 0) else -q
        return q if -32768 <= q <= 32767 else None

    zhi = s16(z >> 16)
    if zhi >= 0x100:
        a, b = idiv(x >> 8, zhi), idiv(y >> 8, zhi)
    elif zhi >= 1:
        d = ((z >> 8) & 0xFFFF) >> 1
        a, b = idiv(x >> 1, d), idiv(y >> 1, d)
    else:
        return None
    return None if a is None or b is None else (a, b)


def vertex_distance(va, vb):
    """The screen distance between two projected vertex records (V values), or None when either
    is behind the eye."""
    if va[5] == 2 or vb[5] == 2:
        return None
    return max(abs(va[3] - vb[3]), abs(va[4] - vb[4]))


def batch_distance(a, b):
    """Mean screen motion of the vertices (the first min(n) of two batches; None: nothing to compare)."""
    ds = [vertex_distance(x, y) for x, y in zip(a.verts, b.verts)]
    ds = [d for d in ds if d is not None]
    return sum(ds) / len(ds) if ds else None


MAX_MOTION = 60        # a batch whose vertices moved further than this (pixels, mean) is not paired
MAX_HUD_MOTION = 30    # a HUD line that moved further than this between steps is held
MAX_EDGE_MOTION = 80   # a paired polygon with an edge end that moved further than this is held
MAX_COUNT_DIFF = 0.1   # batches of 3 or more vertices may differ in count by this share (at least 1)


def compatible(a, b):
    """May batch a be the same model chunk as batch b? Equal vertex counts, or - for chunks of
    three or more vertices - counts that differ by a few (a chunk whose far vertices were not
    needed this step) when the shared first vertices are near each other in camera space."""
    if a.sig == b.sig:
        return True
    lo = min(a.sig, b.sig)
    if lo < 3 or abs(a.sig - b.sig) > max(1, int(MAX_COUNT_DIFF * max(a.sig, b.sig))):
        return False
    near = 0
    for va, vb in zip(a.verts[:lo], b.verts[:lo]):
        z = max(abs(va[2]), abs(vb[2]), 1 << 16)
        near += max(abs(va[k] - vb[k]) for k in range(3)) <= z // 4
    return near >= 0.7 * lo      # a vertex dropped from the middle shifts the rest: most must still agree


def align_batches(A, B):
    """Pair batches of two frames in order by the longest common subsequence of compatible
    batches (equal counts count most), look-alikes decided by the smaller screen motion.
    Returns [(i, j)]."""
    na, nb = len(A), len(B)

    def score(i, j):
        if not compatible(A[i], B[j]):
            return None
        d = batch_distance(A[i], B[j])
        if d is not None and d > MAX_MOTION:
            return None
        return 1000.0 - 100.0 * abs(A[i].sig - B[j].sig) - (d if d is not None else 0.0)
    best = [[0.0] * (nb + 1) for _ in range(na + 1)]
    for i in range(na - 1, -1, -1):
        for j in range(nb - 1, -1, -1):
            s = score(i, j)
            v = best[i + 1][j + 1] + s if s is not None else -1
            best[i][j] = max(best[i + 1][j], best[i][j + 1], v)
    pairs = []
    i = j = 0
    while i < na and j < nb:
        s = score(i, j)
        if s is not None and abs(best[i][j] - (best[i + 1][j + 1] + s)) < 1e-9:
            pairs.append((i, j))
            i += 1
            j += 1
        elif best[i + 1][j] >= best[i][j + 1]:
            i += 1
        else:
            j += 1
    return pairs


def align_polys(PA, PB):
    """Pair the painted polygons of two paired batches by their edge slots (LCS, in order)."""
    ka = [tuple(e["slot"] for e in p.edges) for p in PA]
    kb = [tuple(e["slot"] for e in p.edges) for p in PB]
    na, nb = len(ka), len(kb)
    best = [[0] * (nb + 1) for _ in range(na + 1)]
    for i in range(na - 1, -1, -1):
        for j in range(nb - 1, -1, -1):
            best[i][j] = max(best[i + 1][j], best[i][j + 1], best[i + 1][j + 1] + 1 if ka[i] == kb[j] else -1)
    pairs = []
    i = j = 0
    while i < na and j < nb:
        if ka[i] == kb[j] and best[i][j] == best[i + 1][j + 1] + 1:
            pairs.append((i, j))
            i += 1
            j += 1
        elif best[i + 1][j] >= best[i][j + 1]:
            i += 1
        else:
            j += 1
    return pairs


# ---------------------------------------------------------------------------------------------
# logic frames


class Frame:
    """One logic frame: the phases of one flight-model step, joined. recs is every record of
    the phases in order (a later phase opens with an origin record the replay reads, so the
    pages carry on without a new dump); zline / yline are the dumps at the first phase's start."""

    def __init__(self, phases):
        self.phases = phases
        self.icount = phases[0].icount
        self.last = phases[-1]
        self.zline, self.yline = phases[0].zline, phases[0].yline
        self.recs = []
        for k, ph in enumerate(phases):
            if k:
                self.recs.append(Rec("X", "X %d 26 %d\n" % (ph.icount, ph.zseg_origin[1])))
            self.recs.extend(ph.recs)
        self.batches = parse_phase(self)
        self.polys = [p for b in self.batches for p in b.polys]
        self.spans = self._spans()
        self.hud = self._hud_lines()

    def _spans(self):
        """The library span fills ('Q') keyed by the colour set just before them ('K'), their mode
        and their order among those: {key: record index}."""
        out, colour, seen = {}, None, Counter()
        for i, r in enumerate(self.recs):
            if r.kind == "K":
                colour = r.v[0]
            elif r.kind == "Q" and r.v[3] > 0:
                k = (colour, r.v[2])
                out[k + (seen[k],)] = i
                seen[k] += 1
        return out

    def _hud_lines(self):
        """The library's lines ('N': the HUD's ladder, tapes and markers) grouped by the colour set
        just before them and the page they draw to: {(colour, page, origin): [record indexes]}."""
        out, colour = {}, None
        for i, r in enumerate(self.recs):
            if r.kind == "K":
                colour = r.v[0]
            elif r.kind == "N":
                v = r.v
                out.setdefault((colour,) + tuple(v[5:7]), []).append(i)
        return out

    def lines(self):
        return [r.line for r in self.recs]


def has_present(ph):
    """The phase presents the work page to the display (graphics entry 44)."""
    return any(r.kind == "D" and r.v[0] == 44 for r in ph.recs)


def frames_from(phases, per_frame=0):
    """Group consecutive phases into logic frames. per_frame = 0 splits after every phase that
    presents the picture (the step's second phase; the first group is dropped, it may have
    begun before the log); a number takes that many phases at a time."""
    if per_frame:
        return [Frame(phases[i:i + per_frame]) for i in range(0, len(phases) - per_frame + 1, per_frame)]
    groups, cur = [], []
    for ph in phases:
        cur.append(ph)
        if has_present(ph):
            groups.append(cur)
            cur = []
    return [Frame(g) for g in groups[1:]]


# ---------------------------------------------------------------------------------------------
# interpolating a model polygon


def lerp(a, b, w):
    return a + (b - a) * w


def rnd(x):
    return int(x + 0.5) if x >= 0 else -int(-x + 0.5)


def vertex_of(batch, slot, end):
    """The V values of an edge's first (end 0) or second (end 1) vertex in its batch."""
    ends = batch.edges.get(slot)
    if ends is None or ends[end] >= len(batch.verts):
        return None
    return batch.verts[ends[end]]


def interp_edge(ea, eb, w, bat_a, bat_b, viewport, space, stats):
    """The edge ea moved the fraction w towards eb (the same slot, the same clip status). In
    "camera" space an unclipped edge whose ends project from front-range vertices has its
    camera-space vertices interpolated and projected by the original's divide; every other edge,
    and "screen" space, interpolates the integer screen endpoints."""
    new = dict(ea)
    xmin, ymin, xmax, ymax = viewport
    lo = ea["st"] & 0xFF
    if space == "camera" and lo == 0:
        va0, va1 = vertex_of(bat_a, ea["slot"], 0), vertex_of(bat_a, ea["slot"], 1)
        vb0, vb1 = vertex_of(bat_b, eb["slot"], 0), vertex_of(bat_b, eb["slot"], 1)
        if va0 and va1 and vb0 and vb1 and 2 not in (va0[5], va1[5], vb0[5], vb1[5]) and                 bat_a.edges[ea["slot"]] == bat_b.edges[eb["slot"]]:
            ox, oy = s16(ea["x0"]) - s16(va0[3]), s16(ea["y0"]) - s16(va0[4])
            ox2, oy2 = s16(eb["x0"]) - s16(vb0[3]), s16(eb["y0"]) - s16(vb0[4])
            if (ox, oy) == (ox2, oy2) and s16(ea["x1"]) - s16(va1[3]) == ox and s16(ea["y1"]) - s16(va1[4]) == oy and \
                    s16(eb["x1"]) - s16(vb1[3]) == ox and s16(eb["y1"]) - s16(vb1[4]) == oy:
                got = []
                for va, vb in ((va0, vb0), (va1, vb1)):
                    xf = [rnd(lerp(va[k], vb[k], w)) for k in range(3)]
                    got.append(project(xf))
                if None not in got:
                    xs = [got[0][0] + ox, got[1][0] + ox]
                    ys = [got[0][1] + oy, got[1][1] + oy]
                    if all(xmin <= x <= xmax for x in xs) and all(ymin <= y <= ymax for y in ys):
                        new["x0"], new["y0"], new["x1"], new["y1"] = xs[0] & 0xFFFF, ys[0] & 0xFFFF, xs[1] & 0xFFFF, ys[1] & 0xFFFF
                        stats["edges camera-space"] += 1
                        return new
        stats["edges camera mode fell back"] += 1
    for k in ("x0", "y0", "x1", "y1"):
        new[k] = rnd(lerp(s16(ea[k]), s16(eb[k]), w)) & 0xFFFF
    for k in ("y0hi", "y1hi"):
        new[k] = rnd(lerp(ea[k], eb[k], w))
    stats["edges screen-space"] += 1
    return new


def interp_poly(pa, pb, bat_a, bat_b, w, space, stats):
    """Rebuild polygon pa (the skeleton's) a fraction w of the way to its pair pb: the new span
    rows as (top, rows), or None with the cause counted in stats."""
    if pa.colour & 0xFF00 not in (0xFF00, 0xFE00, 0xFD00, 0xFB00):
        stats["held: fill style with no rule"] += 1
        return None
    if [e["st"] for e in pa.edges] != [e["st"] for e in pb.edges] or \
            [(e["slot"], e["st"]) for e in pa.join] != [(e["slot"], e["st"]) for e in pb.join]:
        stats["held: clip status differs"] += 1
        return None
    vp = pa.f[1:5]
    for ea, eb in zip(pa.edges, pb.edges):
        if not ea["st"] & 0x80 and max(abs(s16(ea[k]) - s16(eb[k])) for k in ("x0", "y0", "x1", "y1")) > MAX_EDGE_MOTION:
            stats["held: an edge end moved over %d px (not the same vertex?)" % MAX_EDGE_MOTION] += 1
            return None
    edges = [interp_edge(ea, eb, w, bat_a, bat_b, vp, space, stats) for ea, eb in zip(pa.edges, pb.edges)]
    join = [interp_edge(ea, eb, w, bat_a, bat_b, vp, "screen", stats) for ea, eb in zip(pa.join, pb.join)]
    return poly_rows(pa, edges, join)


def poly_lines(p, top, rows, rowbase, icount):
    """The 'R' record and the 'b' records for polygon p painted with rows: what the observer
    would have logged, in the original's order (the 'a' records are not needed by the replay for
    the solid, AND, OR and discard styles)."""
    r = p.r
    n = r[2]
    out = ["R %d %d %d %d %s %d %d\n" % (icount, r[0], top, len(rows), " ".join("%d %d" % lr for lr in rows),
                                         r[3 + 2 * n], r[4 + 2 * n])]
    xmin, xmax = p.f[1], p.f[3]
    es = r[4 + 2 * n]
    for k, (l, rt) in enumerate(rows):
        x0, x1 = max(l, xmin), min(rt, xmax)
        if x0 > x1 or l == NOLEFT:
            continue
        y = top + k
        out.append("b %d %d %d %d %d %d\n" % (icount, y, x0, x1 - x0 + 1, es, rowbase + 320 * y + x0))
    return out


def interp_spans(va, vb, w):
    """A span fill ('Q' values: first row, last row, mode, row count, the rows' left and right,
    then the page segment and origin) a fraction w of the way to another of the same colour and
    mode: the row range moves, and each row takes the bounds of the nearest row of either fill."""
    na, nb = va[3], vb[3]
    ya, yb = rnd(lerp(va[0], vb[0], w)), rnd(lerp(va[1], vb[1], w))
    if yb < ya:
        return None
    rows = []
    for y in range(ya, yb + 1):
        ra = min(max(y - va[0], 0), na - 1)
        rb = min(max(y - vb[0], 0), nb - 1)
        rows.append((rnd(lerp(va[4 + 2 * ra], vb[4 + 2 * rb], w)), rnd(lerp(va[5 + 2 * ra], vb[5 + 2 * rb], w))))
    return [ya, yb, va[2], len(rows)] + [x for lr in rows for x in lr] + va[4 + 2 * na:]


def interp_line(la, lb, w):
    """An outline edge ('L' values) moved towards another of the same slot and colour."""
    out = list(la)
    for k in range(1, 5):
        out[k] = rnd(lerp(la[k], lb[k], w))
    return out


# ---------------------------------------------------------------------------------------------
# a pair of frames


class Pairing:
    """The pairing of frame A with the next frame B: aligned batches, paired polygons."""

    def __init__(self, A, B):
        self.A, self.B = A, B
        self.batch_pairs = align_batches(A.batches, B.batches)
        self.poly_pairs = []          # (pa, pb, bat_a, bat_b)
        self.paired_a, self.paired_b = set(), set()
        for i, j in self.batch_pairs:
            ba, bb = A.batches[i], B.batches[j]
            for x, y in align_polys(ba.polys, bb.polys):
                self.poly_pairs.append((ba.polys[x], bb.polys[y], ba, bb))
                self.paired_a.add(id(ba.polys[x]))
                self.paired_b.add(id(bb.polys[y]))
        self.batches_a = {i for i, _ in self.batch_pairs}
        self.batches_b = {j for _, j in self.batch_pairs}

    def causes(self):
        """Why polygons / batches did not pair, counted for both frames."""
        c = Counter()
        for fr, paired, bpaired in ((self.A, self.paired_a, self.batches_a), (self.B, self.paired_b, self.batches_b)):
            for b in fr.batches:
                c["batches"] += 1
                c["batches paired" if b.idx in bpaired else "batches unpaired"] += 1
                for p in b.polys:
                    c["polygons"] += 1
                    if id(p) in paired:
                        c["polygons paired"] += 1
                    elif b.idx not in bpaired:
                        c["polygons unpaired: " + self.why(fr, b)] += 1
                    else:
                        c["polygons unpaired: no polygon with its edges in the paired batch"] += 1
        return c

    def why(self, fr, b):
        """Why batch b of frame fr has no pair in the other frame."""
        other = self.B if fr is self.A else self.A
        same = [o for o in other.batches if (compatible(b, o) if fr is self.A else compatible(o, b))]
        if not same:
            return "no batch of its size (%s)" % ("1 vertex" if b.sig == 1 else "model split or merged, or count changed too much")
        near = []
        for o in same:
            d = batch_distance(b, o) if fr is self.A else batch_distance(o, b)
            if d is None or d <= MAX_MOTION:
                near.append(o)
        if not near:
            return "every batch with its vertex count moved over %d px" % MAX_MOTION
        return "equal batch exists but in another order or taken by a nearer one"

    def motion(self):
        """Per-vertex screen motion between the frames over the paired batches (max of |dx|, |dy|)."""
        out = []
        for i, j in self.batch_pairs:
            for va, vb in zip(self.A.batches[i].verts, self.B.batches[j].verts):
                d = vertex_distance(va, vb)
                if d is not None:
                    out.append(d)
        return out


def rowbase_of(frame):
    c = Counter()
    for r in frame.recs:
        if r.kind == "b":
            v = r.v
            c[v[4] - v[1] - 320 * v[0]] += 1
    return c.most_common(1)[0][0] if c else 0


def inbetween(A, B, pairing, t, space="camera", stats=None, skeleton=None, hud=True):
    """The record lines of the in-between frame at t (0 <= t <= 1): frame A at t = 0, frame B at
    t = 1, and for 0 < t < 1 the nearer frame's list (or the one named by skeleton, "A" or "B") with
    its paired primitives moved."""
    stats = Counter() if stats is None else stats
    if t <= 0:
        return A.lines(), stats
    if t >= 1:
        return B.lines(), stats
    if skeleton == "A" or (skeleton is None and t < 0.5):
        skel, w = A, t
        pairs = {id(pa): (pa, pb, ba, bb) for pa, pb, ba, bb in pairing.poly_pairs}
    else:
        skel, w = B, 1 - t
        pairs = {id(pb): (pb, pa, bb, ba) for pa, pb, ba, bb in pairing.poly_pairs}
    base = rowbase_of(skel)
    replace = {}
    for p in skel.polys:
        got = pairs.get(id(p))
        if got is None:
            stats["held: unpaired polygon"] += 1
            continue
        pa, pb, ba, bb = got
        res = interp_poly(pa, pb, ba, bb, w, space, stats)
        if res is None:
            continue
        top, rows = res
        if not rows or p.r[2] == 0:
            stats["held: nothing painted"] += 1
            continue
        mine = p.rows[0][4] - p.rows[0][1] - 320 * p.rows[0][0] if p.rows else base
        new_lines = poly_lines(p, top, rows, mine, int(skel.recs[p.ri].line.split()[1]))
        assert pb in (B if skel is A else A).polys and pa in skel.polys and             [e["slot"] for e in pa.edges] == [e["slot"] for e in pb.edges], "an unpaired polygon was moved"
        stats["regenerated lines"] += len(new_lines)
        replace[p.ri] = "".join(new_lines)
        for i in p.bi + p.ai:
            replace[i] = ""
        stats["polygons moved"] += 1
    other = B if skel is A else A
    for key, i in skel.spans.items():
        j = other.spans.get(key)
        stats["span fills"] += 1
        if j is None:
            stats["held: span fill with no pair"] += 1
            continue
        v = interp_spans(skel.recs[i].v, other.recs[j].v, w)
        if v is None:
            stats["held: span fill"] += 1
            continue
        replace[i] = "Q %s %s\n" % (skel.recs[i].line.split()[1], " ".join(str(x) for x in v))
        stats["span fills moved"] += 1
        stats["regenerated lines"] += 1
    if hud:
        for key, idx in skel.hud.items():
            oth = other.hud.get(key, [])
            stats["HUD lines"] += len(idx)
            if len(oth) != len(idx):
                stats["held: HUD lines whose group changed in size"] += len(idx)
                continue
            for i, j in zip(idx, oth):
                a, b = skel.recs[i].v, other.recs[j].v
                if max(abs(a[k] - b[k]) for k in range(4)) > MAX_HUD_MOTION:
                    stats["held: HUD line that moved over %d px" % MAX_HUD_MOTION] += 1
                    continue
                nv = [rnd(lerp(a[k], b[k], w)) for k in range(4)] + a[4:]
                replace[i] = "N %s %s\n" % (skel.recs[i].line.split()[1], " ".join(str(x) for x in nv))
                stats["HUD lines moved"] += 1
                stats["regenerated lines"] += 1
    bmap = {i: j for i, j in pairing.batch_pairs} if skel is A else {j: i for i, j in pairing.batch_pairs}
    for b in skel.batches:
        ob = other.batches[bmap[b.idx]] if b.idx in bmap else None
        seen = Counter()
        for i in b.lines:
            v = skel.recs[i].v
            stats["outline edges"] += 1
            if ob is None:
                stats["held: outline edge in an unpaired batch"] += 1
                continue
            key = (v[0], v[5])
            mine = [x for x in ob.lines if other.recs[x].v[0] == v[0] and other.recs[x].v[5] == v[5]]
            n = seen[key]
            seen[key] += 1
            if n >= len(mine):
                stats["held: outline edge with no pair"] += 1
                continue
            nv = interp_line(v, other.recs[mine[n]].v, w)
            replace[i] = "L %s %s\n" % (skel.recs[i].line.split()[1], " ".join(str(x) for x in nv))
            stats["outline edges moved"] += 1
            stats["regenerated lines"] += 1
    for i, r in enumerate(skel.recs):
        if r.kind == "D" and r.v[0] == 44 and len(r.v) > 5 and i not in replace:
            # the present: the replay's own work page is the source, not the bytes the original
            # logged (those are the original's picture and would hide every change)
            replace[i] = "D %s %s\n" % (r.line.split()[1], " ".join(str(x) for x in r.v[:5]))
    return [replace.get(i, r.line) for i, r in enumerate(skel.recs)], stats


# ---------------------------------------------------------------------------------------------
# pictures


def palette_of(phase):
    """256 (r, g, b) from the phase's 'J' record (6-bit DAC values), or a grey ramp."""
    if phase.aline:
        v = [int(x) for x in phase.aline.split()[2:]]
        return [tuple(min(255, (c << 2) | (c >> 4)) for c in v[3 * i:3 * i + 3]) for i in range(256)]
    return [(i, i, i) for i in range(256)]


def picture(display, pal, width=320, height=200):
    """RGB bytes of the display page (64,000 palette indexes)."""
    out = bytearray()
    for b in display[:width * height]:
        out += bytes(pal[b])
    return bytes(out)


def write_png(path, rgb, width, height):
    import struct
    import zlib

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def montage(pictures, cols, scale=1, gap=2):
    """Tile 320x200 RGB pictures into one image: (rgb, width, height)."""
    w, h = 320 * scale, 200 * scale
    rows = (len(pictures) + cols - 1) // cols
    W, H = cols * w + (cols - 1) * gap, rows * h + (rows - 1) * gap
    img = bytearray(b"\x20" * (W * H * 3))
    for n, pic in enumerate(pictures):
        cx, cy = (n % cols) * (w + gap), (n // cols) * (h + gap)
        for y in range(h):
            row = pic[(y // scale) * 320 * 3:(y // scale + 1) * 320 * 3]
            if scale > 1:
                row = b"".join(row[i:i + 3] * scale for i in range(0, len(row), 3))
            at = ((cy + y) * W + cx) * 3
            img[at:at + w * 3] = row
    return bytes(img), W, H


# ---------------------------------------------------------------------------------------------
# measuring


def percentile(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else 0


CUT_PAIRED = 0.5       # fewer than this share of the polygons paired: a cut, hold
CUT_MOTION = 40        # the median vertex moved further than this (pixels): a cut, hold


def is_cut(pairing):
    c = pairing.causes()
    polys = c["polygons"]
    if polys and c["polygons paired"] / polys < CUT_PAIRED:
        return "polygons paired %.0f%%" % (100.0 * c["polygons paired"] / polys)
    m = pairing.motion()
    if m and percentile(m, 0.5) > CUT_MOTION:
        return "median motion %d px" % percentile(m, 0.5)
    if not pairing.batch_pairs and (pairing.A.batches or pairing.B.batches):
        return "no batch pairs"
    return None


def phases_per_frame(phases):
    """How many phases make a logic step: the flight model's state (the camera-space vertices
    of the first batch) changes once per step, so count how often it changes between phases."""
    firsts = []
    for ph in phases:
        vs = [r.v for r in ph.recs if r.kind == "V"][:200]
        firsts.append(tuple(tuple(v[:3]) for v in vs))
    changes = [i for i in range(1, len(firsts)) if firsts[i] != firsts[i - 1]]
    return changes, firsts


def end_state(frames, k):
    """The work page and display page the log shows after frame k: the dumps that open frame
    k + 1."""
    return dump_bytes(frames[k + 1].zline), dump_bytes(frames[k + 1].yline)


def render(frame, lines):
    return replay(frame.zline, frame.yline, lines)


def diff_count(a, b, n=64000):
    return sum(1 for x, y in zip(a[:n], b[:n]) if x != y)


def check_exact(frames, space, eps=1e-6):
    """Frame by frame: the Stage 1 replay of the frame's own list against the next dump; then
    the interpolation machinery at t = eps (frame A) and 1 - eps (frame B), which regenerates every
    paired polygon, span fill and outline edge from the pairing, against the same dumps."""
    res = Counter()
    for n in range(len(frames) - 2):
        A, B = frames[n], frames[n + 1]
        P = Pairing(A, B)
        za, ya = end_state(frames, n)
        zb, yb = end_state(frames, n + 1)
        res["frames"] += 1
        z, y = render(A, A.lines())
        res["stage 1 replay of frame n: display exact"] += y[:64000] == ya[:64000]
        res["stage 1 replay of frame n: work page exact"] += z == za[:len(z)]
        stats = Counter()
        lines, stats = inbetween(A, B, P, eps, space, stats)
        z, y = render(A, lines)
        ok = y[:64000] == ya[:64000]
        res["t=0+: display exact"] += ok
        res["t=0+: work page exact"] += z == za[:len(z)]
        if not ok:
            res["t=0+: display bytes off"] += diff_count(y, ya)
        lines2, stats2 = inbetween(A, B, P, 1 - eps, space, Counter())
        z, y = render(B, lines2)
        ok = y[:64000] == yb[:64000]
        res["t=1-: display exact"] += ok
        res["t=1-: work page exact"] += z == zb[:len(z)]
        if not ok:
            res["t=1-: display bytes off"] += diff_count(y, yb)
        for k in ("polygons moved", "span fills moved", "outline edges moved"):
            res["regenerated " + k] += stats[k] + stats2[k]
    return res


def colour_error(pic_a, pic_b, pal, rows=None):
    """Over the first `rows` rows of two display pages: the fraction of pixels whose index differs,
    the mean per-channel difference (0-255), and the fraction that differ by more than 32 per
    channel on average (a shade change is not a moved edge)."""
    n = 320 * (rows or 200)
    d = diff = big = 0
    for x, y in zip(pic_a[:n], pic_b[:n]):
        if x != y:
            diff += 1
            e = sum(abs(p - q) for p, q in zip(pal[x], pal[y]))
            d += e
            big += e > 96
    return diff / n, d / (3.0 * n), big / n


def predict(frames, space, window_rows=107):
    """Interpolate frame n and frame n + 2 at the midpoint and compare with the frame n + 1 the
    log really holds (no information from n + 1 is used: the skeleton is frame n, whose page
    dumps precede it). The baselines are frame n and frame n + 2 held. Reports the share of
    pixels of the 3D window (the first window_rows rows) and of the whole display that differ."""
    out = []
    for n in range(len(frames) - 3):
        A, B, C = frames[n], frames[n + 1], frames[n + 2]
        pal = palette_of(B.last)
        truth = end_state(frames, n + 1)[1]
        P = Pairing(A, C)
        stats = Counter()
        lines, stats = inbetween(A, C, P, 0.5, space, stats, skeleton="A")
        _, y = render(A, lines)
        _, ya = render(A, A.lines())
        yc = end_state(frames, n + 2)[1]
        row = {"n": n}
        for name, pic in (("interp", y), ("hold n", ya), ("hold n+2", yc)):
            row[name + " 3D"] = colour_error(pic, truth, pal, window_rows)
            row[name + " all"] = colour_error(pic, truth, pal)
        out.append(row)
    return out


def report_predict(rows):
    if not rows:
        print("no frames to predict")
        return
    for scope in ("3D", "all"):
        for name in ("interp", "hold n", "hold n+2"):
            fr = [r[name + " " + scope] for r in rows]
            print("  %-9s %-3s: %.2f%% of pixels differ from the real middle frame, %.2f%% by more than a shade "
                  "(mean colour error %.2f/255); worst frame %.2f%% / %.2f%%" % (
                      name, scope, 100.0 * sum(f[0] for f in fr) / len(fr), 100.0 * sum(f[2] for f in fr) / len(fr),
                      sum(f[1] for f in fr) / len(fr), 100.0 * max(f[0] for f in fr), 100.0 * max(f[2] for f in fr)))


def geometry_error(frames, space):
    """The edge endpoints of polygons present in frames n, n + 1 and n + 2 with the same clip
    status: how far (pixels, the larger of |dx| and |dy| per endpoint) frame n held, and the
    midpoint of n and n + 2 interpolated, lie from where frame n + 1 really has them."""
    hold, interp = [], []
    for n in range(len(frames) - 2):
        A, B, C = frames[n], frames[n + 1], frames[n + 2]
        pab, pac = Pairing(A, B), Pairing(A, C)
        ab = {id(pa): pb for pa, pb, _, _ in pab.poly_pairs}
        for pa, pc, ba, bc in pac.poly_pairs:
            pb = ab.get(id(pa))
            if pb is None:
                continue
            sts = [e["st"] for e in pa.edges]
            if sts != [e["st"] for e in pb.edges] or sts != [e["st"] for e in pc.edges]:
                continue
            vp = pa.f[1:5]
            for ea, eb, ec in zip(pa.edges, pb.edges, pc.edges):
                if ea["st"] & 0x80:
                    continue
                mid = interp_edge(ea, ec, 0.5, ba, bc, vp, space, Counter())
                for (xa, ya), (xb, yb), (xm, ym) in (((s16(ea["x0"]), s16(ea["y0"])), (s16(eb["x0"]), s16(eb["y0"])), (s16(mid["x0"]), s16(mid["y0"]))),
                                                     ((s16(ea["x1"]), s16(ea["y1"])), (s16(eb["x1"]), s16(eb["y1"])), (s16(mid["x1"]), s16(mid["y1"])))):
                    hold.append(max(abs(xa - xb), abs(ya - yb)))
                    interp.append(max(abs(xm - xb), abs(ym - yb)))
    return hold, interp


def report_geometry(hold, interp):
    if not hold:
        print("no polygons in three frames")
        return
    for name, e in (("hold n", hold), ("interpolated", interp)):
        print("  %-12s edge endpoints off from the real middle frame: mean %.2f px, median %d, p95 %d, p99 %d; exact %.1f%%, within 1 px %.1f%% (%d endpoints)" % (
            name, sum(e) / len(e), percentile(e, .5), percentile(e, .95), percentile(e, .99),
            100.0 * sum(1 for x in e if x == 0) / len(e), 100.0 * sum(1 for x in e if x <= 1) / len(e), len(e)))


def crop_scale(rgb, box, scale):
    """Crop a 320 x 200 RGB picture to box = (x0, y0, x1, y1) and enlarge it by whole pixels."""
    x0, y0, x1, y1 = box
    rows = []
    for y in range(y0, y1):
        row = rgb[(y * 320 + x0) * 3:(y * 320 + x1) * 3]
        if scale > 1:
            row = b"".join(row[k:k + 3] * scale for k in range(0, len(row), 3))
        rows.extend([row] * scale)
    return b"".join(rows), (x1 - x0) * scale, (y1 - y0) * scale


def tile(pictures, w, h, cols, gap=2):
    """Tile equal-sized RGB pictures (w x h) into one image."""
    nrows = (len(pictures) + cols - 1) // cols
    W, H = cols * w + (cols - 1) * gap, nrows * h + (nrows - 1) * gap
    img = bytearray(b" " * (W * H * 3))
    for n, pic in enumerate(pictures):
        cx, cy = (n % cols) * (w + gap), (n // cols) * (h + gap)
        for y in range(h):
            at = ((cy + y) * W + cx) * 3
            img[at:at + w * 3] = pic[y * w * 3:(y + 1) * w * 3]
    return bytes(img), W, H


def strips(frames, outdir, space, steps, first=0, count=2, box=(0, 0, 320, 200), scale=1, cols=2, name="strip"):
    """PNG strips: for logic pairs first .. first + count - 1, the in-between frames at t = j / steps,
    j = 0 .. steps (the display page, cropped to box and enlarged), as a grid, written to
    outdir/NAME_NN.png."""
    import os
    os.makedirs(outdir, exist_ok=True)
    for n in range(first, min(first + count, len(frames) - 2)):
        A, B = frames[n], frames[n + 1]
        pal = palette_of(A.last)
        P = Pairing(A, B)
        pics = []
        for j in range(steps + 1):
            t = j / steps
            lines, st = inbetween(A, B, P, t, space)
            _, y = render(A if (t < 0.5 or t <= 0) else B, lines)
            pic, w, h = crop_scale(picture(y, pal), box, scale)
            pics.append(pic)
        rgb, W, H = tile(pics, w, h, cols)
        path = os.path.join(outdir, "%s_%02d.png" % (name, n))
        write_png(path, rgb, W, H)
        print("wrote", path)


def check_primitives(frames, space, ts=(0.25, 0.5, 0.75), render_colours=True):
    """The in-between frames at t in ts for every pair: how much of each frame's draw list was
    moved and how much held (and why), and the provenance rule: every line of the in-between
    list is a line of frame n or n + 1 or a regeneration of a primitive paired in both, so
    none is absent from both. With render_colours the in-between display's colour indexes are
    also compared with those of the two neighbours."""
    res = Counter()
    for n in range(len(frames) - 2):
        A, B = frames[n], frames[n + 1]
        P = Pairing(A, B)
        cut = is_cut(P)
        res["frame pairs"] += 1
        res["frame pairs that are cuts (hold)"] += bool(cut)
        known = set(A.lines()) | set(B.lines())
        da, db = dump_bytes(B.yline)[:64000], dump_bytes(frames[n + 2].yline)[:64000]   # the display after A, after B
        ya, yb = set(da), set(db)
        for t in ts:
            st = Counter()
            lines, st = inbetween(A, B, P, t, space, st)
            res["in-between frames"] += 1
            flat = []
            for line in lines:
                flat.extend(line.splitlines(True))
            novel = [l for l in flat if l not in known]
            kinds = {l[0] for l in novel}
            res["records in the list"] += len(flat)
            res["records neither frame has"] += len(novel)
            res["provenance violations"] += (len(novel) > st["regenerated lines"] + st["D"]) or not kinds <= set("RbQLDN")
            for k, v in st.items():
                res[k] += v
            if render_colours:
                _, y = render(A if t < 0.5 else B, lines)
                extra = set(y[:64000]) - ya - yb
                res["pixels compared"] += 64000
                res["pixels that differ from the skeleton frame's own picture"] += diff_count(y, da if t < 0.5 else db)
                res["in-between displays with a colour index in neither neighbour"] += bool(extra)
    return res


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--per-frame", type=int, default=0, help="phases per logic frame (default: split after each present)")
    ap.add_argument("--space", choices=("screen", "camera"), default="camera")
    ap.add_argument("--pairing", action="store_true", help="pairing rates and causes")
    ap.add_argument("--check", action="store_true", help="t = 0 and t = 1 against the Stage 1 replay")
    ap.add_argument("--predict", action="store_true", help="interpolate n and n+2 at 1/2 and compare with n+1")
    ap.add_argument("--primitives", action="store_true", help="what moves and what holds, and the provenance rule")
    ap.add_argument("--geometry", action="store_true", help="edge endpoints: interpolated n,n+2 midpoint against real n+1")
    ap.add_argument("--strip", metavar="DIR", help="write PNG strips of in-between frames")
    ap.add_argument("--from-frame", type=int, default=0, help="first logic frame to use")
    ap.add_argument("--frames", type=int, default=0, help="how many frames to use (default all)")
    ap.add_argument("--count", type=int, default=2, help="strips: how many logic steps to draw")
    ap.add_argument("--crop", default="0,0,320,200", help="strips: x0,y0,x1,y1 of the display page to show")
    ap.add_argument("--scale", type=int, default=1, help="strips: enlargement")
    ap.add_argument("--cols", type=int, default=2, help="strips: tiles per row")
    ap.add_argument("--name", default="strip")
    ap.add_argument("--steps", type=int, default=7, help="in-between frames per logic step (strips)")
    a = ap.parse_args(argv)
    phases = read_log(a.log)
    frames = frames_from(phases, a.per_frame)
    if a.frames:
        frames = frames[a.from_frame:a.from_frame + a.frames]
    print("%d phases, %d logic frames" % (len(phases), len(frames)))
    if a.pairing:
        report_pairing(frames)
    if a.check:
        res = check_exact(frames, a.space)
        for k in sorted(res):
            print("  %s: %d" % (k, res[k]))
    if a.primitives:
        res = check_primitives(frames, a.space)
        for k in sorted(res):
            print("  %s: %d" % (k, res[k]))
    if a.predict:
        report_predict(predict(frames, a.space))
    if a.geometry:
        report_geometry(*geometry_error(frames, a.space))
    if a.strip:
        strips(frames, a.strip, a.space, a.steps, 0, a.count, tuple(int(x) for x in a.crop.split(",")), a.scale, a.cols, a.name)
    return phases, frames, a


def report_pairing(frames):
    tot, cuts, motion = Counter(), [], []
    for n in range(len(frames) - 1):
        P = Pairing(frames[n], frames[n + 1])
        c = P.causes()
        tot.update(c)
        m = P.motion()
        motion += m
        cut = is_cut(P)
        if cut:
            cuts.append((n, cut))
    pairs = len(frames) - 1
    print("%d frame pairs; %d fall back to hold (cuts): %s" % (pairs, len(cuts), cuts[:6]))
    print("polygons: %d, paired %d (%.1f%%)" % (tot["polygons"], tot["polygons paired"], 100.0 * tot["polygons paired"] / max(tot["polygons"], 1)))
    for k in sorted(tot):
        if k.startswith("polygons unpaired") or k.startswith("batches"):
            print("  %s: %d" % (k, tot[k]))
    if motion:
        print("vertex motion between steps (px, max of |dx|,|dy|): mean %.1f, median %d, p95 %d, p99 %d, max %d, over %d vertices" % (
            sum(motion) / len(motion), percentile(motion, .5), percentile(motion, .95), percentile(motion, .99), max(motion), len(motion)))


if __name__ == "__main__":
    main(sys.argv[1:])
