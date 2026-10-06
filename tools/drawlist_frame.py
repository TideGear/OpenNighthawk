"""drawlist_frame.py LOG: rebuild each picture phase from the draw list.

LOG is an observer log (f117run --observe) taken with F117R_OBSERVE_PAGES=1,
so every game_draw entry carries the whole page the library draws to ('Z').
Each phase is rebuilt from the dump at its start by replaying, in order,
every primitive the observer captured, and compared with the dump at its end:

  fill rows ('b', after the polygon's 'R')  painted in the fill's style from
                                            the replay's own bytes
  outline edges ('L')                       drawn by VGAME's line rules
  library lines ('N', colour 'K')           drawn by the line rules
  library span fills ('Q')                  filled by mode from the replay
  blits ('C')                               copied from the replay's page, or
                                            from the logged source bytes when
                                            the source is another page (art)

What has no replay rule yet is applied from what the original wrote and
counted apart, so the share still copied rather than replayed is visible:
fill rows in a dither or stipple style ('a'), and the graphics entries no hook
decodes ('x').
"""
import sys
from collections import Counter

from drawlist_spans import paint, line_pixels, s16


def outline_pixels(x0, y0, x1, y1):
    """An outline polygon's edge (VGAME 1377:046F): the pixels as (x, y).
    The major axis is x unless |dy| > |dx| (unsigned); the endpoints are put
    in order along it (a signed compare), the minor step's sign comes from the
    other coordinate, the count is the major length + 1, and the error term
    starts at -(count >> 1), adds the minor length after each pixel and, on
    reaching zero or more, takes a minor step and subtracts the major length."""
    ady, adx = abs(s16(y1 - y0)) & 0xFFFF, abs(s16(x1 - x0)) & 0xFFFF
    out = []
    if ady <= adx:
        if x1 < x0:
            x0, x1, y0, y1 = x1, x0, y1, y0
        step = -1 if y1 < y0 else 1
        n = (x1 - x0 + 1) & 0xFFFF
        err = -(n >> 1)
        x, y = x0, y0
        for _ in range(n):
            out.append((x, y))
            x += 1
            err += ady
            if err >= 0:
                y += step
                err -= adx
    else:
        if y1 < y0:
            x0, x1, y0, y1 = x1, x0, y1, y0
        step = -1 if x1 < x0 else 1
        n = (y1 - y0 + 1) & 0xFFFF
        err = -(n >> 1)
        x, y = x0, y0
        for _ in range(n):
            out.append((x, y))
            y += 1
            err += adx
            if err >= 0:
                x += step
                err -= ady
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    page = None              # the replay, a bytearray of the 64 KB page
    seg = origin = None
    phase = 0
    totals = Counter()
    colour = None            # the library's current colour (entries 32/33)
    fill_colour = None       # the polygon fill's colour word ('R')
    fill_rows = []           # the rows of the fill under way ('b'), to pair with their 'a'
    blit = None
    copied = set()           # offsets written from the original's own result this phase
    for line in open(sys.argv[1]):
        f = line.split()
        if not f:
            continue
        k = f[0]
        if k == "Z":
            v = f[2:]
            z = bytes(int(x) for x in v[2:])
            if page is not None:
                diff = [a for a in range(65536) if page[a] != z[a]]
                changed = sum(1 for a in range(65536) if start[a] != z[a])
                print("phase %d (origin at start %04X): %d bytes changed; replay differs at %d%s; %d bytes copied, not replayed" % (
                    phase, start_origin, changed, len(diff),
                    "" if not diff else " (first %s)" % ", ".join("%04X:%d/%d" % (a, page[a], z[a]) for a in diff[:4]),
                    len(copied)))
                totals["phases"] += 1
                totals["exact"] += not diff
                totals["changed"] += changed
                totals["copied"] += len(copied)
            seg, origin = int(v[0]), int(v[1])                # the origin at the phase's start
            start_origin = origin
            page = bytearray(z)
            start = z
            phase += 1
            copied = set()
            continue
        if page is None:
            continue
        if k == "X" and int(f[2]) in (24, 26):       # the library's origin: 24 resets it, 26 sets it
            origin = 0 if int(f[2]) == 24 else int(f[3])
        elif k == "K":
            colour = int(f[2])
        elif k == "R":
            # the colour word, the top row, the row count, the rows, then the
            # colour the fill really uses ([8606], after its fade)
            v = [int(x) for x in f[2:]]
            fill_colour = v[3 + 2 * v[2]] & 0xFFFF
            fill_rows = []
        elif k == "b":
            # y, x0, len, segment, offset, then the bytes before (unused: the replay has its own)
            y, x0, n, es, at = (int(x) for x in f[2:7])
            if es != seg:
                continue
            got = paint(fill_colour, y, x0, [page[(at + i) & 0xFFFF] for i in range(n)])
            if got is None:
                fill_rows.append((at, n))
                continue
            for i, b in enumerate(got):
                page[(at + i) & 0xFFFF] = b
        elif k == "a" and fill_rows:
            y, x0, n, es, at = (int(x) for x in f[2:7])
            if (at, n) in fill_rows:                         # a style with no rule yet: the original's bytes
                for i in range(n):
                    page[(at + i) & 0xFFFF] = int(f[7 + i])
                    copied.add((at + i) & 0xFFFF)
        elif k == "L":
            v = [int(x) for x in f[2:]]
            if len(v) < 10:
                continue
            _, x0, y0, x1, y1, col, es, row0, row1, handler = v
            if handler != 0x046F or row1 - row0 != 320:
                totals["outline edges with no rule"] += 1
                continue
            if es != seg:
                continue
            for x, y in outline_pixels(x0, y0, x1, y1):
                page[(row0 + 320 * y + x) & 0xFFFF] = col & 0xFF
        elif k == "N":
            x0, y0, x1, y1 = (int(x) for x in f[2:6])
            for x, y in line_pixels(x0, y0, x1, y1):
                if 0 <= x < 320 and 0 <= y < 200:
                    page[(320 * y + origin + x) & 0xFFFF] = colour
        elif k == "Q":
            v = [int(x) for x in f[2:]]
            ya, mode, nrows = v[0], v[2], v[3]
            for r in range(nrows):
                l, rt = v[4 + 2 * r], v[5 + 2 * r]
                y = ya + r
                if rt < l or (rt == l and rt in (0, 0x13F)) or not 0 <= y < 200:
                    continue
                for x in range(l, rt + 1):
                    a = (320 * y + origin + x) & 0xFFFF
                    b = page[a]
                    page[a] = (colour if mode == 0 else b | colour if mode == 1 else
                               b & colour if mode == 2 else ((b & 0x0E) >> 1) | 0x98)
        elif k == "C":
            v = [int(x) for x in f[2:]]
            sp, sx, sy, dp, dx, dy, w, h, sseg, dseg = v[:10]
            src = v[10:]
            if dseg != seg:
                continue
            if sseg == seg:                                  # within the page: from the replay itself
                rows = [bytes(page[(320 * (sy + y) + sx) & 0xFFFF:][:w]) for y in range(h)]
            else:
                if len(src) != w * h:
                    totals["blits without source bytes"] += 1
                    continue
                rows = [bytes(src[y * w:(y + 1) * w]) for y in range(h)]
            for y in range(h):
                for x in range(w):
                    page[(320 * (dy + y) + dx + x) & 0xFFFF] = rows[y][x]
        elif k == "x":
            v = [int(x) for x in f[2:]]
            entry, xseg, n = v[0], v[1], v[2]
            if entry == 42 or xseg != seg:
                continue
            for j in range(n):
                a, now = v[3 + 3 * j], v[4 + 3 * j]
                page[a] = now
                copied.add(a)
    t = totals
    for k in sorted(t):
        if "no rule" in k or "without" in k:
            print("%s: %d" % (k, t[k]))
    print("%d phases rebuilt from the draw list, %d exact; %d bytes changed in all, %d of them copied from the original rather than replayed" % (
        t["phases"], t["exact"], t["changed"], t["copied"]))


if __name__ == "__main__":
    main()
