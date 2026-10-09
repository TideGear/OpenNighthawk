/* drawlist.c - see drawlist.h. Each rule cites the original routine it
 * replays; tools/drawlist_frame.py and tools/drawlist_spans.py say more. */
#include "drawlist.h"

#include <stdlib.h>
#include <string.h>

static int s16(int32_t v) { return (int16_t)(uint16_t)v; }

/* Python's floor division and remainder, as the line rule uses them */
static int32_t fdiv(int32_t a, int32_t b) { int32_t q = a / b; return (a % b && (a < 0) != (b < 0)) ? q - 1 : q; }
static int32_t fmod_(int32_t a, int32_t b) { int32_t r = a % b; return (r && (r < 0) != (b < 0)) ? r + b : r; }

void drawlist_init(drawlist *d)
{
    memset(d, 0, sizeof *d);
    d->colour = d->fill_colour = -1;
}

void drawlist_set_scale(drawlist *d, int n)
{
    d->n = n;
}

void drawlist_free(drawlist *d)
{
    for (int k = 0; k < DRAWLIST_PAGES; k++) free(d->page[k].hi);
    const int n = d->n;
    drawlist_init(d);
    d->n = n;
}

void drawlist_reset(drawlist *d)
{
    const int logged = d->prefer_logged;
    drawlist_free(d);
    d->prefer_logged = logged;
}

void drawlist_copy(drawlist *dst, const drawlist *src)
{
    uint8_t *keep[DRAWLIST_PAGES];
    for (int k = 0; k < DRAWLIST_PAGES; k++) keep[k] = dst->page[k].hi;
    memcpy(dst, src, sizeof *dst);
    for (int k = 0; k < DRAWLIST_PAGES; k++) {
        const drawlist_page *s = &src->page[k];
        drawlist_page *p = &dst->page[k];
        if (!s->hi) { free(keep[k]); p->hi = NULL; continue; }
        const size_t bytes = (size_t)320 * s->n * 200 * s->n;
        p->hi = (uint8_t *)realloc(keep[k], bytes);
        memcpy(p->hi, s->hi, bytes);
    }
}

static drawlist_page *page_of(drawlist *d, uint16_t seg)
{
    for (int k = 0; k < d->npages; k++)
        if (d->page[k].seg == seg) return &d->page[k];
    return NULL;
}

const drawlist_page *drawlist_get(const drawlist *d, uint16_t seg)
{
    return page_of((drawlist *)d, seg);
}

drawlist_page *drawlist_page_of(drawlist *d, uint16_t seg)
{
    return page_of(d, seg);
}

/* A byte of the first 320x200 also written to the fine picture, as an N x N
 * block (HiPage._mirror). */
static void mirror(drawlist_page *p, uint32_t a, uint8_t v)
{
    if (!p->hi || p->suppress || a >= 64000) return;
    const int n = p->n;
    const size_t W = (size_t)320 * n;
    uint8_t *q = p->hi + (size_t)(a / 320) * n * W + (size_t)(a % 320) * n;
    for (int j = 0; j < n; j++, q += W) memset(q, v, (size_t)n);
}

static void wr(drawlist_page *p, uint32_t a, uint8_t v)
{
    p->b[a] = v;
    mirror(p, a, v);
}

/* A copy from another held page: the fine rows come across with the coarse
 * ones (HiPage.copy_hi_rect). */
static void copy_hi_rect(drawlist_page *p, const drawlist_page *src, int32_t sx, int32_t sy, int32_t dx, int32_t dy,
                         int32_t w, int32_t h)
{
    if (!p->hi || !src->hi || p->n != src->n) return;
    const int n = p->n;
    const size_t W = (size_t)320 * n;
    for (int32_t y = 0; y < h; y++) {
        if (sy + y < 0 || sy + y >= 200 || dy + y < 0 || dy + y >= 200) continue;
        const int32_t x0 = sx > 0 ? sx : 0, x1 = sx + w < 320 ? sx + w : 320;
        if (x1 <= x0 || dx + x0 - sx < 0 || dx + x1 - sx > 320) continue;
        for (int j = 0; j < n; j++) {
            const size_t s_at = ((size_t)(sy + y) * n + j) * W + (size_t)x0 * n;
            const size_t d_at = ((size_t)(dy + y) * n + j) * W + (size_t)(dx + x0 - sx) * n;
            memmove(p->hi + d_at, src->hi + s_at, (size_t)(x1 - x0) * n);
        }
    }
}

void drawlist_seed(drawlist *d, char kind, uint16_t seg, uint16_t origin, const uint8_t *bytes, unsigned size)
{
    drawlist_page *p = page_of(d, seg);
    if (!p) {
        if (d->npages == DRAWLIST_PAGES) return;
        p = &d->page[d->npages++];
        p->seg = seg;
    }
    if (size > sizeof p->b) size = sizeof p->b;
    p->size = size;
    memcpy(p->b, bytes, size);
    p->suppress = 0;
    if (d->n > 0) {                                   /* the fine picture starts as the scaled copy */
        const int n = d->n;
        const size_t W = (size_t)320 * n;
        if (!p->hi || p->n != n) { free(p->hi); p->hi = (uint8_t *)malloc(W * 200 * n); }
        p->n = n;
        for (int y = 0; y < 200; y++) {
            uint8_t *row = p->hi + (size_t)y * n * W;
            for (int x = 0; x < 320; x++)
                memset(row + (size_t)x * n, (unsigned)(y * 320 + x) < size ? bytes[y * 320 + x] : 0, (size_t)n);
            for (int j = 1; j < n; j++) memcpy(row + j * W, row, W);
        }
    }
    if (kind == 'Z') drawlist_phase(d, seg, origin);
}

void drawlist_phase(drawlist *d, uint16_t seg, uint16_t origin)
{
    d->have_seg = 1;
    d->seg = seg;
    d->origin = origin;
    memset(d->copied, 0, sizeof d->copied);
    d->ncopied = 0;
}

static void put(drawlist_page *p, uint32_t a, int v)
{
    a &= 0xFFFF;
    if (a < p->size) wr(p, a, (uint8_t)v);
}

static void mark_copied(drawlist *d, uint16_t seg, uint32_t a)
{
    if (seg != d->seg) return;
    a &= 0xFFFF;
    if (!(d->copied[a >> 3] & (1u << (a & 7)))) { d->copied[a >> 3] |= (uint8_t)(1u << (a & 7)); d->ncopied++; }
}

/* A fill row in the original's style (VGAME 0x14279 dispatches on the colour
 * word's high byte): FF solid, FE AND, FD OR, FB discard. 0: no rule (stipple
 * FC and the dithers), the original's bytes follow in the row's 'a'. */
static int paint(int colour, uint8_t *row, int n)
{
    const uint8_t lo = (uint8_t)colour, hi = (uint8_t)(colour >> 8);
    if (hi == 0xFF) { for (int i = 0; i < n; i++) row[i] = lo; return 1; }
    if (hi == 0xFE) { for (int i = 0; i < n; i++) row[i] &= lo; return 1; }
    if (hi == 0xFD) { for (int i = 0; i < n; i++) row[i] |= lo; return 1; }
    return hi == 0xFB;
}

/* An outline polygon's edge (VGAME 1377:046F): the major axis is x unless
 * |dy| > |dx| (unsigned); the endpoints in order along it (signed), the minor
 * step's sign from the other coordinate, the count the major length + 1, the
 * error from -(count >> 1), adding the minor length a pixel and on reaching
 * zero taking a minor step and subtracting the major length. */
static void outline_edge(drawlist_page *p, int x0, int y0, int x1, int y1, int32_t row0, int col)
{
    const int32_t ady = abs(s16(y1 - y0)) & 0xFFFF, adx = abs(s16(x1 - x0)) & 0xFFFF;
    if (ady <= adx) {
        if (x1 < x0) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        const int step = y1 < y0 ? -1 : 1;
        const int32_t n = (x1 - x0 + 1) & 0xFFFF;
        int32_t err = -(n >> 1), x = x0, y = y0;
        for (int32_t k = 0; k < n; k++) {
            put(p, (uint32_t)(row0 + 320 * y + x), col);
            x++;
            err += ady;
            if (err >= 0) { y += step; err -= adx; }
        }
    } else {
        if (y1 < y0) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        const int step = x1 < x0 ? -1 : 1;
        const int32_t n = (y1 - y0 + 1) & 0xFFFF;
        int32_t err = -(n >> 1), x = x0, y = y0;
        for (int32_t k = 0; k < n; k++) {
            put(p, (uint32_t)(row0 + 320 * y + x), col);
            y++;
            err += adx;
            if (err >= 0) { x += step; err -= ady; }
        }
    }
}

/* Graphics entry 31, the library's line: an unsigned sort on x, the
 * single-pixel case decided by "were the two x's equal", a half-step error
 * term, and the major-axis step through a linear 320-byte-row address. Pixels
 * off the 320x200 screen are not drawn. */
static void lib_line(drawlist_page *p, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t org, int col)
{
    int32_t ax = x0, bx = y0, cx = x1, dx = y1;
    const int xeq = (ax & 0xFFFF) == (cx & 0xFFFF);
    if ((ax & 0xFFFF) > (cx & 0xFFFF)) { int32_t t = ax; ax = cx; cx = t; t = bx; bx = dx; dx = t; }
    int32_t si = 1, bp = 320;
    if (xeq && bx == dx) {                            /* one pixel, (ax, bx) itself */
        if (ax >= 0 && ax < 320 && bx >= 0 && bx < 200) put(p, (uint32_t)(320 * bx + org + ax), col);
        return;
    }
    cx = s16(cx - ax); dx = s16(dx - bx);
    if (dx < 0) { bp = -bp; dx = -dx; }
    if ((cx & 0xFFFF) < (dx & 0xFFFF)) { int32_t t = si; si = bp; bp = t; t = dx; dx = cx; cx = t; }
    const int32_t major = cx, minor = dx;
    int32_t di = bx * 320 + ax;
    int32_t err = s16(-(((major + 1) & 0xFFFF) >> 1));
    si -= 1;
    int32_t n = major;
    for (;;) {
        const int32_t x = fmod_(di, 320), y = fdiv(di, 320);
        if (x >= 0 && x < 320 && y >= 0 && y < 200) put(p, (uint32_t)(320 * y + org + x), col);
        di += 1;
        n -= 1;
        if (n < 0) break;
        di += si;
        err = s16(err + minor);
        if (err < 0) continue;
        err = s16(err - major);
        di += bp;
    }
}

/* Graphics entries 1-6, the library's text (driver 0x3E8 sets up, 0x31D cuts
 * the string at the width clip for entry 3, 0x2B9 skips what lies left of the
 * clip for 2, 0x37F clips the rows for entry 1, 0x61A paints). Returns whether
 * the painter ran (it leaves the library colour at the foreground). */
typedef struct {
    int first, last, shift, fixed, height, spacing, extra;
    const int32_t *widths, *glyphs;
    int nglyphs;
} font_t;

static int font_width(const font_t *f, int fixed_w, int c)
{
    return fixed_w ? fixed_w : (f->widths[c - f->first] + f->spacing) & 0xFF;
}

static int glyph(const font_t *f, int32_t at)
{
    return at >= 0 && at < f->nglyphs ? f->glyphs[at] & 0xFF : 0;
}

static int text(drawlist_page *p, int entry, const int32_t *blk, const int32_t *chars, int nchars, const font_t *f)
{
    const int mode = blk[1] & 0xFF, bg = blk[3] & 0xFF;
    int fg = blk[2] & 0xFF;
    int32_t x = blk[4], y = blk[5];
    if (nchars <= 0 || nchars > 256) return 0;
    int s[260];
    for (int i = 0; i < nchars; i++) s[i] = chars[i];
    s[nchars] = s[nchars + 1] = 0;
    int si = 0;                                       /* where the string starts (the left clip moves it) */
    int rows = f->height + ((blk[1] & 1) ? f->extra : 0);
    const int fixed_w = f->fixed ? (f->fixed + f->spacing) & 0xFF : 0;
    const int sh = (f->shift - 1) & 0xFF;
    const int32_t stride = (int32_t)(f->last - f->first + 1) << sh;
    int32_t base = 0;
    int cut = 0, skip = 0;                            /* [1808] the last character's overshoot, [1807] a left skip */
#define VALID(c) (!((c) & 0x80) && (c) <= f->last && (c) >= f->first)
    if (entry == 3 || entry == 6) {
        int32_t limit = blk[10] & 0xFFFF, dx = x & 0xFFFF;
        if (dx >= limit) return 0;
        limit += 1;
        for (int i = 0; s[i]; i++) {
            const int c = s[i];
            if (!VALID(c)) continue;
            dx += font_width(f, fixed_w, c);
            if (dx < limit) continue;
            cut = (dx - limit) & 0xFF;
            s[i + 1] = 0;
            break;
        }
    }
    if (entry == 2 || entry == 6) {
        const int32_t left = blk[9] & 0xFFFF, right = blk[10] & 0xFFFF;
        if (s16(right - left) < 0) return 0;
        if ((x & 0xFFFF) < left) {
            int32_t cx = x & 0xFFFF;
            x = left;
            for (int i = 0;; i++) {
                const int c = s[i];
                if (!c) return 0;                     /* the string ends inside the clipped part */
                if (!VALID(c)) continue;
                cx = (cx + font_width(f, fixed_w, c)) & 0xFFFF;
                if (s16(cx) <= s16(left)) continue;
                skip = (font_width(f, fixed_w, c) - (cx - left)) & 0xFF;
                si = i;                               /* earlier colour codes are lost */
                break;
            }
        }
    }
    if (entry == 1 || entry == 6) {
        const int top = blk[7] & 0xFF, bottom = blk[8] & 0xFF;
        const int dl = y & 0xFF, dh = (rows - 1 + dl) & 0xFF;
        if (dl > bottom || dh < top) return 0;
        if (dl < top) {
            rows = (dh - top + 1) & 0xFF;
            y = (y & 0xFF00) | top;
            base += (int32_t)(top - dl) * stride;
        }
        if (dh > bottom) rows = (bottom - dl + 1) & 0xFF;
    }
    for (int i = si; s[i]; i++) {
        const int c = s[i], nxt = s[i + 1];
        if (c & 0x80) { fg = c & 0x7F; continue; }
        if (c > f->last || c < f->first) continue;
        int w = font_width(f, fixed_w, c);
        if (!nxt) w = (w - cut) & 0xFF;
        w = (w - skip) & 0xFF;
        const int32_t g = base + ((int32_t)(c - f->first) << sh);
        int draw = w;
        if (mode != 1 && nxt && draw) draw = (draw - f->spacing) & 0xFF;
        if (draw)
            for (int r = 0; r < rows; r++) {
                const int32_t at = g + r * stride;
                uint32_t bits = skip >= 16 ? 0 : (((uint32_t)glyph(f, at) << 8 | (uint32_t)glyph(f, at + 1)) << skip) & 0xFFFF;
                for (int k = 0; k < draw; k++) {
                    const int on = (bits & 0x8000) != 0;
                    bits = (bits << 1) & 0xFFFF;
                    if (on) put(p, (uint32_t)(320 * (y + r) + x + k), fg);
                    else if (mode == 1) put(p, (uint32_t)(320 * (y + r) + x + k), bg);
                }
            }
        skip = 0;
        x += w;
    }
#undef VALID
    return 1;
}

/* Graphics entry 22 (driver 0x0B70-0x0E2D): the scaled, flippable RLE
 * sprite. v is the 'W' record: the entry, the page segment, the block's eleven
 * words, x, y, width, height, the source segment and offset, its width and
 * height, the length of its rows and their bytes. Clip (0x0BC0) to the
 * block's y range (+0Eh, +10h) and x range (+12h, +14h), step the source rows
 * and columns by the Bresenham-style error terms the driver keeps in
 * cs:[085E] and cs:[0860], colour 0 transparent, a negative width or height
 * flipping that axis. */
static int rle_byte(const int32_t *data, int32_t nbytes, int32_t at)
{
    return at >= 0 && at < nbytes ? data[at] & 0xFF : 0;
}

static void scaled_sprite(drawlist_page *p, const int32_t *v, int n)
{
    if (n < 22) return;
    const int32_t *blk = v + 2;
    const int32_t x = s16(v[13]), y = s16(v[14]), wd = s16(v[15]), hd = s16(v[16]);
    const int32_t sseg = v[17], sw = v[19], sh = v[20];
    int32_t nbytes = v[21];
    if (nbytes > n - 22) nbytes = n - 22;
    const int32_t *data = v + 22;
    if (!sseg || !sw || !sh || nbytes <= 0) return;
    const int32_t ymin = s16(blk[7]), ymax = s16(blk[8]), xmin = s16(blk[9]), xmax = s16(blk[10]);
    const int32_t w = abs(wd), h = abs(hd);
    const int flip_x = wd < 0, flip_y = hd < 0;
    int32_t top = 0, bottom = 0, left = 0, right = 0;
    int32_t di = y, dx = x, bx;
    if (di < 0) { top = -di; if (top >= h) return; }
    bx = ymax - ymin;
    if (bx < 0 || di > bx) return;
    if (di + h - 1 - bx > 0) bottom = di + h - 1 - bx;
    if (dx < 0) { left = -dx; if (left >= w) return; }
    bx = xmax - xmin;
    if (bx < 0 || dx > bx) return;
    if (dx + w - 1 - bx > 0) right = dx + w - 1 - bx;
    const int32_t cols = w - left - right, rows = h - top - bottom;
    if (cols <= 0 || rows <= 0) return;
    dx += xmin;
    di += ymin + top;
    int32_t vstep = 320, cx = top;
    if (flip_y) { vstep = -320; cx = bottom; di += rows - 1; }
    /* where each source row starts */
    static int32_t row_at[65536 + 1];
    int32_t nrow_at = 1, pos = 0;
    row_at[0] = 0;
    for (int32_t k = 0; k < sh; k++) {
        row_at[nrow_at] = pos + (rle_byte(data, nbytes, pos) | rle_byte(data, nbytes, pos + 1) << 8) + 4;
        pos = row_at[nrow_at++];
        if (pos >= nbytes) break;
    }
    /* the source row the first visible destination row starts on */
    int32_t r = 0, ax = -h;
    for (;;) {
        if (--cx < 0) break;
        ax += sh;
        if (ax < 0) continue;
        do { r++; ax -= h; } while (ax >= 0);
    }
    int32_t verr = ax;
    dx += left;
    cx = left;
    int32_t step = 1;
    if (flip_x) { cx = right; step = -1; dx += cols - 1; }
    int32_t scol = 0;
    ax = -w;
    for (;;) {
        if (--cx < 0) break;
        ax += sw;
        if (ax < 0) continue;
        do { scol++; ax -= w; } while (ax >= 0);
    }
    const int32_t herr = ax;
    int32_t base = 320 * di + dx;
    for (int32_t row = 0; row < rows; row++) {
        if (r < sh && r < nrow_at) {
            const int32_t a = row_at[r];
            if (a + 4 <= nbytes) {
                const int32_t alen = rle_byte(data, nbytes, a) | rle_byte(data, nbytes, a + 1) << 8;
                const int32_t lead = rle_byte(data, nbytes, a + 2) | rle_byte(data, nbytes, a + 3) << 8;
                int32_t have = nbytes - (a + 4);      /* the row's bytes the record holds */
                if (have > alen) have = alen;
                if (alen && alen + lead > scol) {
                    int32_t s = scol, bp = herr, at = base;
                    int col = 0, load = 1;
                    for (int32_t c = 0; c < cols; c++) {
                        /* one destination pixel per column, as 0x0DD3-0x0DF6 */
                        if (load) {
                            col = s >= lead && s - lead < have ? data[a + 4 + s - lead] & 0xFF : 0;
                            s++;
                            load = 0;
                        }
                        if (col) put(p, (uint32_t)at, col);
                        at += step;
                        bp += sw;
                        if (bp < 0) continue;     /* the same source pixel again */
                        bp -= w;
                        while (bp >= 0) { s++; bp -= w; }
                        load = 1;
                    }
                }
            }
        }
        /* next destination row */
        base += vstep;
        ax = verr + sh;
        if (ax >= 0) do { r++; ax -= h; } while (ax >= 0);
        verr = ax;
    }
}

/* Graphics entry 11 (driver 071A): the tick scale. From row BX - 1 (+20
 * when DL >= 1, +1 more when CL is set) upward two rows at a time while not
 * below the low limit: a tick at x, leftward when SI is 0, 3 pixels every
 * tenth (2 with CL), 2 every fifth, else 1, skipped above the high limit. */
static void ticks(drawlist_page *p, const int32_t *v)
{
    const int32_t si0 = v[0], bx = v[1], dl = v[2], cl = v[3], col = v[4], x = v[5], lo = v[6], hi = v[7];
    const int32_t step = si0 == 0 ? -1 : 1;
    int32_t row = (bx - 1 + (dl >= 1 ? 0x14 : 0) + (cl ? 1 : 0)) & 0xFFFF;
    int ch = 10;
    while (row >= lo) {
        if (row <= hi) {
            const int n = ch == 5 ? 2 : ch == 10 ? (cl ? 2 : 3) : 1;
            for (int k = 0; k < n; k++) put(p, (uint32_t)(320 * row + x + step * k), col);
        }
        row = (row - 2) & 0xFFFF;
        if (row > 0xFFF0) break;
        ch = ch == 10 ? 1 : ch + 1;
    }
}

/* Driver 0E41 (entries 71, 19): the sprite's block clipped to x in
 * [+14h, +16h] and y in [+10h, +12h], signed, the far end compared against
 * the original near end. 0 when nothing is left. */
static int sprite_clip(const int32_t *blk, int32_t *dx, int32_t *dy, int32_t *w, int32_t *h, int32_t *left, int32_t *top)
{
    int32_t b[12];
    for (int k = 0; k < 12; k++) b[k] = s16(blk[k]);
    *dx = b[4]; *dy = b[5]; *w = b[6]; *h = b[7];
    *left = *top = 0;
    for (int axis = 0; axis < 2; axis++) {
        int32_t pos = axis == 0 ? *dx : *dy, size = axis == 0 ? *w : *h;
        const int32_t lo = axis == 0 ? b[10] : b[8], hi = axis == 0 ? b[11] : b[9];
        const int32_t ax = pos, end = pos + size - 1;
        if (ax < lo) {
            if (end < lo) return 0;
            const int32_t cut = lo - ax;
            pos += cut;
            size -= cut;
            if (axis == 0) *left = cut; else *top = cut;
        }
        if (end > hi) {
            if (ax > hi) return 0;
            size -= end - hi;
        }
        if (axis == 0) { *dx = pos; *w = size; } else { *dy = pos; *h = size; }
    }
    return 1;
}

void drawlist_record(drawlist *d, char kind, const int32_t *v, int n)
{
    if (!d->have_seg) return;
    drawlist_page *p;
    switch (kind) {
    case 'X':                                         /* the library's origin: 24 resets it, 26 sets it */
        if (n >= 2 && (v[0] == 24 || v[0] == 26)) d->origin = (uint16_t)(v[0] == 24 ? 0 : v[1]);
        break;
    case 'K':
        if (n >= 1) d->colour = v[0] & 0xFF;
        break;
    case 'R':                                         /* the colour the fill really uses, after its fade */
        if (n >= 3 && 3 + 2 * v[2] < n) d->fill_colour = v[3 + 2 * v[2]] & 0xFFFF;
        d->nfill_rows = 0;
        break;
    case 'b': {                                       /* a fill row, painted from the replay's own bytes */
        if (n < 5 || d->fill_colour < 0 || !(p = page_of(d, (uint16_t)v[3]))) break;
        const int32_t len = v[2], at = v[4];
        uint8_t row[65536];
        for (int32_t i = 0; i < len; i++) row[i] = p->b[(at + i) & 0xFFFF];
        if (!paint(d->fill_colour, row, len)) {
            if (d->nfill_rows < 256) {
                d->fill_rows[d->nfill_rows].at = (uint16_t)at;
                d->fill_rows[d->nfill_rows++].n = (uint16_t)len;
            }
            break;
        }
        for (int32_t i = 0; i < len; i++) wr(p, (uint32_t)(at + i) & 0xFFFF, row[i]);
        break;
    }
    case 'a': {                                       /* a style with no rule yet: the original's bytes */
        if (n < 5 || !d->nfill_rows || !(p = page_of(d, (uint16_t)v[3]))) break;
        const int32_t len = v[2], at = v[4];
        int k;
        for (k = 0; k < d->nfill_rows; k++)
            if (d->fill_rows[k].at == (uint16_t)at && d->fill_rows[k].n == len) break;
        if (k == d->nfill_rows) break;
        for (int32_t i = 0; i < len && 5 + i < n; i++) {
            wr(p, (uint32_t)(at + i) & 0xFFFF, (uint8_t)v[5 + i]);
            mark_copied(d, p->seg, (uint32_t)(at + i));
        }
        break;
    }
    case 'L':                                         /* an outline edge, by VGAME's own line routine */
        if (n < 10) break;
        if (v[9] != 0x046F || v[8] - v[7] != 320) { d->outline_no_rule++; break; }
        if ((p = page_of(d, (uint16_t)v[6]))) outline_edge(p, v[1], v[2], v[3], v[4], v[7], v[5] & 0xFF);
        break;
    case 'N': {
        if (n < 4 || d->colour < 0) break;
        const uint16_t seg = n >= 7 ? (uint16_t)v[5] : d->seg;
        const int32_t org = n >= 7 ? v[6] : d->origin;
        if ((p = page_of(d, seg))) lib_line(p, v[0], v[1], v[2], v[3], org, d->colour);
        break;
    }
    case 'Q': {                                       /* entries 37 and 40: span rows by mode */
        if (n < 4) break;
        const int32_t ya = v[0], mode = v[2], nrows = v[3];
        uint16_t seg = d->seg;
        int32_t org = d->origin;
        if (n >= 6 + 2 * nrows) { seg = (uint16_t)v[4 + 2 * nrows]; org = v[5 + 2 * nrows]; }
        if (!(p = page_of(d, seg)) || d->colour < 0) break;
        const int colour = d->colour;
        for (int32_t r = 0; r < nrows && 5 + 2 * r < n; r++) {
            const int32_t l = v[4 + 2 * r], rt = v[5 + 2 * r], y = ya + r;
            if (rt < l || (rt == l && (rt == 0 || rt == 0x13F)) || y < 0 || y >= 200) continue;
            for (int32_t x = l; x <= rt; x++) {
                const uint32_t a = (uint32_t)(320 * y + org + x) & 0xFFFF;
                if (a >= p->size) continue;
                const uint8_t b = p->b[a];
                wr(p, a, (uint8_t)(mode == 0 ? colour : mode == 1 ? (b | colour) : mode == 2 ? (b & colour)
                                                                    : (((b & 0x0E) >> 1) | 0x98)));
            }
        }
        break;
    }
    case 'C': {                                       /* entry 42, the blit */
        if (n < 10 || !(p = page_of(d, (uint16_t)v[9]))) break;
        const int32_t sx = v[1], sy = v[2], dx = v[4], dy = v[5], w = v[6], h = v[7];
        const drawlist_page *src = page_of(d, (uint16_t)v[8]);
        if (!src && n - 10 != w * h) { d->blits_no_source++; break; }
        static uint8_t buf[65536 * 2];
        if (w <= 0 || h <= 0 || (int64_t)w * h > (int64_t)sizeof buf) break;
        if (src) {                                    /* from a replayed page: the replay itself */
            for (int32_t y = 0; y < h; y++)
                for (int32_t x = 0; x < w; x++) {
                    const uint32_t a = (uint32_t)((320 * (sy + y) + sx) & 0xFFFF) + (uint32_t)x;
                    buf[y * w + x] = a < src->size ? src->b[a] : 0;
                }
        } else
            for (int32_t k = 0; k < w * h; k++) buf[k] = (uint8_t)v[10 + k];
        for (int32_t y = 0; y < h; y++)
            for (int32_t x = 0; x < w; x++) put(p, (uint32_t)(320 * (dy + y) + dx + x), buf[y * w + x]);
        if (src) copy_hi_rect(p, src, sx, sy, dx, dy, w, h);
        break;
    }
    case 'W':                                         /* entry 22, the scaled RLE sprite */
        if (n >= 2 && (p = page_of(d, (uint16_t)v[1]))) scaled_sprite(p, v, n);
        break;
    case 'T': {
        if (n < 14) break;
        const int32_t nchars = v[13];
        if (nchars < 0 || 14 + nchars > n) break;
        const int32_t *rest = v + 14 + nchars;
        const int nrest = n - 14 - nchars;
        if (nrest < 1 || !rest[0]) { d->text_no_font++; break; }
        if (nrest < 8) break;
        font_t f;
        f.first = rest[1]; f.last = rest[2]; f.shift = rest[3]; f.fixed = rest[4];
        f.height = rest[5]; f.spacing = rest[6]; f.extra = rest[7];
        const int count = f.last - f.first + 1;
        if (count < 0 || 9 + count > nrest) break;
        f.widths = rest + 8;
        f.glyphs = rest + 9 + count;
        f.nglyphs = rest[8 + count];
        if (f.nglyphs > nrest - 9 - count) f.nglyphs = nrest - 9 - count;
        p = page_of(d, (uint16_t)v[1]);
        static drawlist_page none;                    /* a page not held: the painter still runs */
        none.size = 0;
        if (text(p ? p : &none, v[0], v + 2, v + 14, nchars, &f)) d->colour = v[2 + 2] & 0xFF;
        break;
    }
    case 'D': {                                       /* whole-page copies: 44 present, 48 copy, 79 dissolve */
        if (n < 4) break;
        const int32_t count = v[1];
        if (!(p = page_of(d, (uint16_t)v[3])) || !count) break;
        const drawlist_page *src = page_of(d, (uint16_t)v[2]);
        const int logged = n >= 5 + count;
        uint32_t len = (uint32_t)count;
        if (len > p->size) len = p->size;
        int same = 0;                                 /* the source is the held page's own bytes */
        if (logged && (d->prefer_logged || !src)) {
            same = src != NULL;
            for (uint32_t a = 0; a < len && same; a++) same = a < src->size && src->b[a] == (uint8_t)v[5 + a];
            for (uint32_t a = 0; a < len; a++) wr(p, a, (uint8_t)v[5 + a]);
        } else if (src) {
            if (len > src->size) len = src->size;
            if (src != p) memmove(p->b, src->b, len);
            for (uint32_t a = 0; a < len; a++) mirror(p, a, p->b[a]);
            same = 1;
        } else d->copies_no_source++;
        if (same) copy_hi_rect(p, src, 0, 0, 0, 0, 320, count / 320);
        break;
    }
    case 'H':
        if (n >= 8 && (p = page_of(d, n > 8 ? (uint16_t)v[8] : d->seg))) ticks(p, v);
        break;
    case 'S': {                                       /* entries 73, 18 (71, 19 clip first): colour 0 transparent */
        if (n < 14) break;
        const int32_t *blk = v + 2, *src = v + 14;
        const int32_t w0 = s16(blk[6]), h0 = s16(blk[7]);
        if (n - 14 != (w0 > 0 ? w0 : 0) * (h0 > 0 ? h0 : 0)) { d->sprites_no_source++; break; }
        int32_t dx, dy, w, h, left, top;
        if (v[0] == 71 || v[0] == 19) {
            if (!sprite_clip(blk, &dx, &dy, &w, &h, &left, &top)) break;
        } else { dx = s16(blk[4]); dy = s16(blk[5]); w = w0; h = h0; left = top = 0; }
        if (!(p = page_of(d, (uint16_t)v[1]))) break;
        for (int32_t y = 0; y < h; y++)
            for (int32_t x = 0; x < w; x++) {
                const int32_t at = (top + y) * w0 + left + x;
                const int b = at >= 0 && at < n - 14 ? src[at] & 0xFF : 0;
                if (b) put(p, (uint32_t)(320 * (dy + y) + dx + x), b);
            }
        break;
    }
    case 'M': {                                       /* entry 41: a colour replaced in a rectangle */
        if (n < 9 || !(p = page_of(d, (uint16_t)v[0]))) break;
        const int32_t rows = (v[4] - v[2] + 1) & 0xFFFF, cols = (v[3] - v[1] + 1) & 0xFFFF;
        if (v[8] - v[7] != 320 || !rows || rows > 256) { d->recolour_no_rule++; break; }
        for (int32_t r = 0; r < rows; r++) {
            const uint32_t di = (uint32_t)(v[7] + 320 * r + v[1]);
            for (int32_t k = 0; k < (cols ? cols : 65536); k++) {
                const uint32_t a = (di + (uint32_t)k) & 0xFFFF;
                if (a < p->size && p->b[a] == (uint8_t)v[5]) wr(p, a, (uint8_t)v[6]);
            }
        }
        break;
    }
    case 'x': {                                       /* the byte changes of an entry no rule decodes */
        if (n < 3) break;
        const int32_t entry = v[0], count = v[2];
        const uint16_t seg = (uint16_t)v[1];
        if ((entry >= 1 && entry <= 6) || entry == 11 || entry == 22 || entry == 41 || entry == 73 || entry == 18 || entry == 71 ||
            entry == 19 || ((entry == 46 || entry == 42) && seg != 0xA000) || !(p = page_of(d, seg)))
            break;
        for (int32_t j = 0; j < count && 4 + 3 * j < n; j++) {
            const uint32_t a = (uint32_t)v[3 + 3 * j];
            if (a < p->size) {
                wr(p, a, (uint8_t)v[4 + 3 * j]);
                mark_copied(d, seg, a);
            }
        }
        break;
    }
    default:
        break;
    }
}
