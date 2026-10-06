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
  text ('T')                                painted from the logged font by
                                            the library's text rules
  tick scales ('H')                         drawn by the library's rules
  page copies ('D')                         the present (work page to display),
                                            and whole-page copies
  sprites ('S')                             copied from the logged source,
                                            colour 0 transparent, clipped by
                                            the library's rules

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


def text_pixels(entry, blk, chars, font):
    """Graphics entries 1, 3, 4 and 5, the library's text (driver 0x3E8 sets
    up, 0x31D cuts the string at the width clip for entry 3, 0x37F clips the
    rows for entry 1, 0x61A paints): the pixels as (x, y, colour), and the
    foreground left at the end (the library's colour from then on)."""
    mode, fg, bg, x, y = blk[1] & 0xFF, blk[2] & 0xFF, blk[3] & 0xFF, blk[4], blk[5]
    first, last, shift, fixed, height, spacing, extra, widths, glyphs = font
    s = list(chars) + [0, 0]
    out = []
    if not chars:
        return out, None
    rows = height + (extra if blk[1] & 1 else 0)
    fixed_w = (fixed + spacing) & 0xFF if fixed else 0
    sh = (shift - 1) & 0xFF
    stride = (last - first + 1) << sh
    base = 0
    cut = skip = 0                       # [1808] the last character's overshoot, [1807] a left skip

    def width(c):
        return fixed_w if fixed_w else (widths[c - first] + spacing) & 0xFF

    if entry in (3, 6):
        limit, dx = blk[10] & 0xFFFF, x & 0xFFFF
        if dx >= limit:
            return out, None
        limit += 1
        for i, c in enumerate(s):
            if c == 0:
                break
            if c & 0x80 or c > last or c < first:
                continue
            dx += width(c)
            if dx < limit:
                continue
            cut = (dx - limit) & 0xFF
            s[i + 1] = 0
            break
    if entry in (2, 6):                  # driver 0x2B9: skip what lies left of the clip at +12h
        left, right = blk[9] & 0xFFFF, blk[10] & 0xFFFF
        if s16(right - left) < 0:
            return out, None
        if (x & 0xFFFF) < left:
            cx, x = x & 0xFFFF, left
            for i, c in enumerate(s):
                if c == 0:
                    return out, None         # the string ends inside the clipped part: nothing drawn
                if c & 0x80 or c > last or c < first:
                    continue
                cx = (cx + width(c)) & 0xFFFF
                if s16(cx) <= s16(left):
                    continue
                skip = (width(c) - (cx - left)) & 0xFF
                s = s[i:]               # the painter starts at this character; earlier colour codes are lost
                break
    if entry in (1, 6):
        top, bottom = blk[7] & 0xFF, blk[8] & 0xFF
        dl = y & 0xFF
        dh = (rows - 1 + dl) & 0xFF
        if dl > bottom or dh < top:
            return out, None
        if dl < top:
            rows = (dh - top + 1) & 0xFF
            y = (y & 0xFF00) | top
            base += (top - dl) * stride
        if dh > bottom:
            rows = (bottom - dl + 1) & 0xFF
    colour_end = fg
    for i, c in enumerate(s):
        if c == 0:
            break
        nxt = s[i + 1]
        if c & 0x80:
            fg = colour_end = c & 0x7F
            continue
        if c > last or c < first:
            continue
        w = width(c)
        if nxt == 0:
            w = (w - cut) & 0xFF
        w = (w - skip) & 0xFF
        g = base + ((c - first) << sh)
        draw = w
        if mode != 1:
            if nxt != 0 and draw:
                draw = (draw - spacing) & 0xFF
        if draw:
            for r in range(rows):
                at = g + r * stride
                bits = ((glyphs[at] << 8 | glyphs[at + 1]) << skip) & 0xFFFF
                for k in range(draw):
                    on = bits & 0x8000
                    bits = (bits << 1) & 0xFFFF
                    if on:
                        out.append((x + k, y + r, fg))
                    elif mode == 1:
                        out.append((x + k, y + r, bg))
        skip = 0
        x += w
    return out, colour_end


def scaled_sprite(v):
    """Graphics entry 22 (driver 0x0B70-0x0E2D): the scaled, flippable RLE
    sprite, as (offset, colour) writes to the page. v is the 'W' record: the
    entry, the page segment, the block's eleven words, x, y, width, height,
    the source segment and offset, its width and height, the length of its
    rows and their bytes. Clip (0x0BC0) to the block's y range (+0Eh, +10h)
    and x range (+12h, +14h), step the source rows and columns by the
    Bresenham-style error terms the driver keeps in cs:[085E] and cs:[0860],
    colour 0 transparent, a negative width or height flipping that axis."""
    blk = v[2:13]
    x, y, wd, hd = (s16(t) for t in v[13:17])
    sseg, sw, sh, nbytes = v[17], v[19], v[20], v[21]
    data = v[22:22 + nbytes]
    out = []
    if not sseg or not sw or not sh or not data:
        return out
    ymin, ymax, xmin, xmax = (s16(t) for t in blk[7:11])
    w, h = abs(wd), abs(hd)
    flip_x, flip_y = wd < 0, hd < 0
    top = bottom = left = right = 0
    di, dx = y, x
    if di < 0:
        top = -di
        if top >= h:
            return out
    bx = ymax - ymin
    if bx < 0 or di > bx:
        return out
    if di + h - 1 - bx > 0:
        bottom = di + h - 1 - bx
    if dx < 0:
        left = -dx
        if left >= w:
            return out
    bx = xmax - xmin
    if bx < 0 or dx > bx:
        return out
    if dx + w - 1 - bx > 0:
        right = dx + w - 1 - bx
    cols, rows = w - left - right, h - top - bottom
    if cols <= 0 or rows <= 0:
        return out
    dx += xmin
    di += ymin + top
    vstep = 320
    cx = top
    if flip_y:
        vstep, cx = -320, bottom
        di += rows - 1
    # the source row the first visible destination row starts on
    row_at = [0]
    pos = 0
    for _ in range(sh):
        row_at.append(pos + (data[pos] | data[pos + 1] << 8) + 4)
        pos = row_at[-1]
        if pos >= len(data):
            break
    r = 0
    ax = -h
    while True:
        cx -= 1
        if cx < 0:
            break
        ax += sh
        if ax < 0:
            continue
        while True:
            r += 1
            ax -= h
            if ax < 0:
                break
    verr = ax
    dx += left
    cx = left
    step = 1
    if flip_x:
        cx, step = right, -1
        dx += cols - 1
    ax, scol = -w, 0
    while True:
        cx -= 1
        if cx < 0:
            break
        ax += sw
        if ax < 0:
            continue
        while True:
            scol += 1
            ax -= w
            if ax < 0:
                break
    herr = ax
    base = 320 * di + dx
    for _ in range(rows):
        a = row_at[r] if r < sh and r < len(row_at) else None
        if a is not None and a + 4 <= len(data):
            alen = data[a] | data[a + 1] << 8
            lead = data[a + 2] | data[a + 3] << 8
            if alen and alen + lead > scol:
                buf = [0] * lead + data[a + 4:a + 4 + alen]
                buf += [0] * max(0, sw - len(buf))
                si, bp, p, col, load = scol, herr, base, 0, True
                for _c in range(cols):
                    # one destination pixel per column, as 0x0DD3-0x0DF6
                    if load:
                        col = buf[si] if si < len(buf) else 0
                        si += 1
                        load = False
                    if col:
                        out.append((p & 0xFFFF, col))
                    p += step
                    bp += sw
                    if bp < 0:
                        continue           # the same source pixel again
                    bp -= w
                    while bp >= 0:
                        si += 1
                        bp -= w
                    load = True
        # next destination row
        base += vstep
        ax = verr + sh
        if ax >= 0:
            while True:
                r += 1
                ax -= h
                if ax < 0:
                    break
        verr = ax
    return out


def sprite_clip(blk):
    """Driver 0E41 (entries 71, 19): the sprite's block clipped to x in
    [+14h, +16h] and y in [+10h, +12h], signed, as the original adjusts it -
    the far end compared against the original near end - or None when
    nothing is left. Returns (sx, sy, dx, dy, w, h) and the clip's offsets
    into the source (left, top)."""
    b = [s16(x) for x in blk]
    sx, sy, dx, dy, w, h = b[1], b[2], b[4], b[5], b[6], b[7]
    left = top = 0
    for axis in (0, 1):
        pos, size = (dx, w) if axis == 0 else (dy, h)
        lo, hi = (b[10], b[11]) if axis == 0 else (b[8], b[9])
        ax = pos
        end = pos + size - 1
        if ax < lo:
            if end < lo:
                return None
            cut = lo - ax
            pos += cut
            size -= cut
            if axis == 0:
                sx += cut
                left = cut
            else:
                sy += cut
                top = cut
        if end > hi:
            if ax > hi:
                return None
            size -= end - hi
        if axis == 0:
            dx, w = pos, size
        else:
            dy, h = pos, size
    return (sx, sy, dx, dy, w, h), (left, top)


def tick_pixels(si0, bx, dl, cl, x, lo, hi):
    """Graphics entry 11 (driver 071A): the tick scale's pixels as (x, row)."""
    out = []
    step = -1 if si0 == 0 else 1             # STD unless SI is non-zero
    row = (bx - 1 + (0x14 if dl >= 1 else 0) + (1 if cl else 0)) & 0xFFFF
    ch = 10
    while row >= lo:
        if row <= hi:
            n = 2 if ch == 5 else (2 if cl else 3) if ch == 10 else 1
            for k in range(n):
                out.append((x + step * k, row))
        row = (row - 2) & 0xFFFF
        if row > 0xFFF0:
            break
        ch = 1 if ch == 10 else ch + 1
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
    pages = {}                # segment -> the replay of that page (the work page 'Z', the display 'Y')
    dumps = {}                # segment -> the dump at the phase's start
    seg = None
    for line in open(sys.argv[1]):
        f = line.split()
        if not f:
            continue
        k = f[0]
        if k in ("Z", "Y"):
            v = f[2:]
            dseg = int(v[0])
            z = bytes(int(x) for x in v[2:])
            name = "work" if k == "Z" else "display"
            if dseg in pages:
                rep, start = pages[dseg], dumps[dseg]
                size = min(len(z), len(rep))
                diff = [a for a in range(size) if rep[a] != z[a]]
                changed = sum(1 for a in range(size) if start[a] != z[a])
                print("phase %d %s page (origin at start %04X): %d bytes changed; replay differs at %d%s%s" % (
                    phase, name, start_origin, changed, len(diff),
                    "" if not diff else " (first %s)" % ", ".join("%04X:%d/%d" % (a, rep[a], z[a]) for a in diff[:4]),
                    "; %d bytes copied, not replayed" % len(copied) if k == "Z" else ""))
                totals[name + " phases"] += 1
                totals[name + " exact"] += not diff
                totals[name + " changed"] += changed
                if k == "Z":
                    totals["copied"] += len(copied)
            if k == "Z":
                seg, origin = dseg, int(v[1])                # the origin at the phase's start
                start_origin = origin
                phase += 1
                copied = set()
            pages[dseg] = bytearray(z)
            dumps[dseg] = z
            continue
        if seg is None:
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
            page = pages.get(es)
            if page is None:
                continue
            got = paint(fill_colour, y, x0, [page[(at + i) & 0xFFFF] for i in range(n)])
            if got is None:
                fill_rows.append((at, n))
                continue
            for i, b in enumerate(got):
                page[(at + i) & 0xFFFF] = b
        elif k == "a" and fill_rows:
            y, x0, n, es, at = (int(x) for x in f[2:7])
            page = pages.get(es)
            if page is not None and (at, n) in fill_rows:    # a style with no rule yet: the original's bytes
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
            page = pages.get(es)
            if page is None:
                continue
            for x, y in outline_pixels(x0, y0, x1, y1):
                page[(row0 + 320 * y + x) & 0xFFFF] = col & 0xFF
        elif k == "N":
            v = [int(x) for x in f[2:]]
            x0, y0, x1, y1 = v[:4]
            lseg, lorg = (v[5], v[6]) if len(v) >= 7 else (seg, origin)
            page = pages.get(lseg)
            if page is None:
                continue
            for x, y in line_pixels(x0, y0, x1, y1):
                if 0 <= x < 320 and 0 <= y < 200:
                    a = (320 * y + lorg + x) & 0xFFFF
                    if a < len(page):
                        page[a] = colour
        elif k == "Q":
            v = [int(x) for x in f[2:]]
            ya, mode, nrows = v[0], v[2], v[3]
            qseg, qorg = (v[4 + 2 * nrows], v[5 + 2 * nrows]) if len(v) >= 6 + 2 * nrows else (seg, origin)
            page = pages.get(qseg)
            if page is None:
                continue
            for r in range(nrows):
                l, rt = v[4 + 2 * r], v[5 + 2 * r]
                y = ya + r
                if rt < l or (rt == l and rt in (0, 0x13F)) or not 0 <= y < 200:
                    continue
                for x in range(l, rt + 1):
                    a = (320 * y + qorg + x) & 0xFFFF
                    if a >= len(page):
                        continue
                    b = page[a]
                    page[a] = (colour if mode == 0 else b | colour if mode == 1 else
                               b & colour if mode == 2 else ((b & 0x0E) >> 1) | 0x98)
        elif k == "C":
            v = [int(x) for x in f[2:]]
            sp, sx, sy, dp, dx, dy, w, h, sseg, dseg = v[:10]
            src = v[10:]
            page = pages.get(dseg)
            if page is None:
                continue
            if sseg in pages:                                # from a replayed page: the replay itself
                rows = [bytes(pages[sseg][(320 * (sy + y) + sx) & 0xFFFF:][:w]) for y in range(h)]
            else:
                if len(src) != w * h:
                    totals["blits without source bytes"] += 1
                    continue
                rows = [bytes(src[y * w:(y + 1) * w]) for y in range(h)]
            for y in range(h):
                for x in range(w):
                    a = (320 * (dy + y) + dx + x) & 0xFFFF
                    if a < len(page):
                        page[a] = rows[y][x]
        elif k == "W":                                       # entry 22, the scaled RLE sprite
            v = [int(x) for x in f[2:]]
            page = pages.get(v[1])
            if page is not None:
                for a, col in scaled_sprite(v):
                    if a < len(page):
                        page[a] = col
            totals["scaled sprites"] += 1
        elif k == "T":
            v = [int(x) for x in f[2:]]
            entry, tseg, blk = v[0], v[1], v[2:13]
            n = v[13]
            chars, rest = v[14:14 + n], v[14 + n:]
            if not rest or not rest[0]:
                totals["text with no font"] += 1
                continue
            first, last, shift, fixed, height, spacing, extra = rest[1:8]
            count = last - first + 1
            widths = rest[8:8 + count]
            nbytes = rest[8 + count]
            glyphs = rest[9 + count:9 + count + nbytes]
            pixels, end = text_pixels(entry, blk, chars, (first, last, shift, fixed, height, spacing, extra, widths, glyphs))
            if end is not None:                             # the painter ran: it set the library colour to the foreground
                colour = blk[2] & 0xFF
            page = pages.get(tseg)
            if page is not None:
                for x, y, col in pixels:
                    a = (320 * y + x) & 0xFFFF
                    if a < len(page):
                        page[a] = col
            totals["texts"] += 1
        elif k == "D":                                       # whole-page copies: 44 present, 48 copy, 79 dissolve
            v = [int(x) for x in f[2:]]
            entry, count, sseg, dseg = v[:4]
            page = pages.get(dseg)
            if page is None or not count:
                continue
            if len(v) >= 5 + count:
                src = bytes(v[5:5 + count])
            elif sseg in pages:
                src = bytes(pages[sseg][:count])
            else:
                totals["page copies without source bytes"] += 1
                continue
            page[:count] = src[:count]
            totals["page copies"] += 1
        elif k == "H":
            v = [int(x) for x in f[2:]]
            si0, bx, dl, cl, col, x, lo, hi = v[:8]
            page = pages.get(v[8] if len(v) > 8 else seg)
            if page is not None:
                for px, row in tick_pixels(si0, bx, dl, cl, x, lo, hi):
                    a = (320 * row + px) & 0xFFFF
                    if a < len(page):
                        page[a] = col
            totals["tick scales"] += 1
        elif k == "S":
            v = [int(x) for x in f[2:]]
            entry, dseg, blk, src = v[0], v[1], v[2:14], v[14:]
            w0, h0 = s16(blk[6]), s16(blk[7])
            if len(src) != max(w0, 0) * max(h0, 0):
                totals["sprites without source bytes"] += 1
                continue
            if entry in (71, 19):
                got = sprite_clip(blk)
                if got is None:
                    continue
                (sx, sy, dx, dy, w, h), (left, top) = got
            else:
                dx, dy, w, h, left, top = s16(blk[4]), s16(blk[5]), w0, h0, 0, 0
            totals["sprites"] += 1
            page = pages.get(dseg)
            if page is None:
                continue
            for y in range(h):
                for x in range(w):
                    b = src[(top + y) * w0 + left + x]
                    a = (320 * (dy + y) + dx + x) & 0xFFFF
                    if b and a < len(page):                  # colour 0 is transparent
                        page[a] = b
        elif k == "x":
            v = [int(x) for x in f[2:]]
            entry, xseg, n = v[0], v[1], v[2]
            page = pages.get(xseg)
            if (entry in (1, 2, 3, 4, 5, 6, 11, 22, 73, 18, 71, 19) or (entry == 46 and xseg != 0xA000) or page is None or
                    (entry == 42 and xseg != 0xA000)):
                continue
            for j in range(n):
                a, now = v[3 + 3 * j], v[4 + 3 * j]
                if a < len(page):
                    page[a] = now
                    if xseg == seg:
                        copied.add(a)
    t = totals
    for k in sorted(t):
        if "no rule" in k or "without" in k:
            print("%s: %d" % (k, t[k]))
    print("%d phases rebuilt from the draw list, %d exact; %d bytes changed in all, %d of them copied from the original rather than replayed" % (
        t["work phases"], t["work exact"], t["work changed"], t["copied"]))
    if t["display phases"]:
        print("display page: %d phases rebuilt, %d exact; %d bytes changed in all" % (
            t["display phases"], t["display exact"], t["display changed"]))

if __name__ == "__main__":
    main()
