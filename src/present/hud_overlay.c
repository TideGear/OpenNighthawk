/* hud_overlay.c - see hud_overlay.h. */
#include "hud_overlay.h"

#include "diagtag.h"
#include "hud_cover.h"
#include "hud_state.h"
#include "tls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int g_enabled = -1;

void hud_overlay_enable(int on) { g_enabled = on != 0; }

int hud_overlay_enabled(void)
{
    if (g_enabled < 0) {
        const char *e = getenv("F117_PRESENT_HUD");
        g_enabled = !(e && (!strcmp(e, "old") || !strcmp(e, "0") || !strcmp(e, "off")));
    }
    return g_enabled;
}

/* ---- fine pixels ------------------------------------------------------------------------------- */
typedef struct { drawlist_page *p; int W, H, n; uint8_t tag; } canvas;

static void put_fine(const canvas *c, int x, int y, int colour, int cl, int ct, int cr, int cb)
{
    if (x < cl || x >= cr || y < ct || y >= cb) return;
    drawlist_page *p = c->p;
    p->hi[(size_t)y * c->W + x] = (uint8_t)colour;
    if (p->tag) p->tag[(size_t)y * c->W + x] = c->tag;
    const unsigned a = (unsigned)((y / c->n) * 320 + x / c->n);
    p->strokes[a >> 3] |= (uint8_t)(1u << (a & 7));
    p->hud_strokes[a >> 3] |= (uint8_t)(1u << (a & 7));
}

static void fill_rect(const canvas *c, double x0, double y0, double x1, double y1, int colour, int cl, int ct, int cr, int cb)
{
    /* pixels whose centres lie in [x0, x1) x [y0, y1) */
    int ix0 = (int)ceil(x0 - 0.5), ix1 = (int)ceil(x1 - 0.5);
    int iy0 = (int)ceil(y0 - 0.5), iy1 = (int)ceil(y1 - 0.5);
    if (ix0 < cl) ix0 = cl;
    if (ix1 > cr) ix1 = cr;
    if (iy0 < ct) iy0 = ct;
    if (iy1 > cb) iy1 = cb;
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) put_fine(c, x, y, colour, cl, ct, cr, cb);
}

/* a stroke of width w from (ax, ay) to (bx, by) with square end caps: the pixels whose centres lie in the
 * oriented rectangle that runs w/2 past each end and w/2 to each side. Axis-aligned strokes at cell
 * centres with w = N cover exactly the original's cells. */
static void thick_line(const canvas *c, double ax, double ay, double bx, double by, double w, int colour, int cl, int ct, int cr, int cb)
{
    const double h = w / 2.0;
    const double dx = bx - ax, dy = by - ay;
    const double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9 || fabs(dy) < 1e-9 || fabs(dx) < 1e-9) {
        const double xa = ax < bx ? ax : bx, xb = ax < bx ? bx : ax, ya = ay < by ? ay : by, yb = ay < by ? by : ay;
        fill_rect(c, xa - h, ya - h, xb + h, yb + h, colour, cl, ct, cr, cb);
        return;
    }
    const double ux = dx / len, uy = dy / len;
    /* the rectangle's extent in y */
    const double ey = fabs(uy) * (len + w) / 2 + fabs(ux) * h;
    const double my = (ay + by) / 2;
    int j0 = (int)floor(my - ey), j1 = (int)ceil(my + ey);
    if (j0 < ct) j0 = ct;
    if (j1 > cb) j1 = cb;
    for (int j = j0; j < j1; j++) {
        const double yc = j + 0.5;
        double xlo = -1e18, xhi = 1e18;
        /* t = (x - ax) ux + (yc - ay) uy in [-h, len + h);  s = -(x - ax) uy + (yc - ay) ux in [-h, h) */
        for (int k = 0; k < 2; k++) {
            const double slope = k == 0 ? ux : -uy;
            const double base = k == 0 ? (yc - ay) * uy - ax * ux : (yc - ay) * ux + ax * uy;
            const double lo = k == 0 ? -h : -h, hi = k == 0 ? len + h : h;
            /* lo <= slope * x + base < hi */
            if (fabs(slope) < 1e-12) { if (base < lo || base >= hi) { xlo = 1; xhi = 0; } continue; }
            double a = (lo - base) / slope, b = (hi - base) / slope;
            if (a > b) { const double t = a; a = b; b = t; }
            if (a > xlo) xlo = a;
            if (b < xhi) xhi = b;
        }
        if (xlo >= xhi) continue;
        int i0 = (int)ceil(xlo - 0.5), i1 = (int)ceil(xhi - 0.5);
        if (i0 < cl) i0 = cl;
        if (i1 > cr) i1 = cr;
        for (int i = i0; i < i1; i++) put_fine(c, i, j, colour, cl, ct, cr, cb);
    }
}

static int gl(const hud_font *f, int at) { return at >= 0 && at < f->nglyph ? f->glyph[at] : 0; }

static void draw_text(const canvas *c, const hud_fprim *f, const hud_font *ft)
{
    const int N = c->n;
    const int mode = f->blk[1] & 0xFF, bg = f->blk[3] & 0xFF;
    int fg = f->blk[2] & 0xFF;
    const int rows = ft->height + ((f->blk[1] & 1) ? ft->extra : 0);
    int cl = 0, ct = 0, cr = c->W, cb = c->H;
    if (f->entry & 1) {
        const int t = (int16_t)f->blk[7] * N, b = ((int16_t)f->blk[8] + 1) * N;
        if (t > ct) ct = t;
        if (b < cb) cb = b;
    }
    if (f->entry & 2) { const int l = (int16_t)f->blk[9] * N; if (l > cl) cl = l; }
    if (f->entry & 4) { const int r = ((int16_t)f->blk[10] + 1) * N; if (r < cr) cr = r; }
    const int X0 = (int)floor(f->x0 + 0.5), Y0 = (int)floor(f->y0 + 0.5);
    const int fixed_w = ft->fixed ? (ft->fixed + ft->spacing) & 0xFF : 0;
    const int sh = (ft->shift - 1) & 0xFF;
    const int stride = ft->count << sh;
    int cx = 0;
    for (int i = 0; i < f->ntext; i++) {
        const int ch = f->text[i], nxt = i + 1 < f->ntext ? f->text[i + 1] : 0;
        if (ch & 0x80) { fg = ch & 0x7F; continue; }
        if (ch > ft->last || ch < ft->first) continue;
        const int w = fixed_w ? fixed_w : (ft->widths[ch - ft->first] + ft->spacing) & 0xFF;
        int draw = w;
        if (mode != 1 && nxt && draw) draw = (draw - ft->spacing) & 0xFF;
        const int g = (ch - ft->first) << sh;
        for (int r = 0; r < rows; r++) {
            const int at = g + r * stride;
            unsigned bits = ((unsigned)gl(ft, at) << 8 | (unsigned)gl(ft, at + 1)) & 0xFFFF;
            for (int k = 0; k < draw; k++) {
                const int on = (bits & 0x8000) != 0;
                bits = (bits << 1) & 0xFFFF;
                if (!on && mode != 1) continue;
                const int colour = on ? fg : bg;
                const int px = X0 + (cx + k) * N, py = Y0 + r * N;
                for (int yy = 0; yy < N; yy++)
                    for (int xx = 0; xx < N; xx++) put_fine(c, px + xx, py + yy, colour, cl, ct, cr, cb);
            }
        }
        cx += w;
    }
}

/* ---- the pass ----------------------------------------------------------------------------------- */
enum { ST_PASSES, ST_DRAWN, ST_NO_TICKS, ST_NO_FONT, ST_NO_PAGE, ST_MISMATCH, ST_REPLACED, ST_KEPT, ST_UNSUPPORTED, ST_LEAKED, ST_PANEL };

void hud_overlay_stats(const drawlist *d, uint64_t out[HUD_STATS])
{
    out[ST_PASSES] = d->hud.passes; out[ST_DRAWN] = d->hud.drawn; out[ST_NO_TICKS] = d->hud.no_ticks;
    out[ST_NO_FONT] = d->hud.no_font; out[ST_NO_PAGE] = d->hud.no_page; out[ST_MISMATCH] = d->hud.mismatched;
    out[ST_REPLACED] = d->hud.replaced; out[ST_KEPT] = d->hud.kept; out[ST_UNSUPPORTED] = d->hud.unsupported;
    out[ST_LEAKED] = d->hud.leaked;
    out[ST_PANEL] = d->hud.panel_replaced;
}

const char *hud_overlay_stat_name(int k)
{
    static const char *const names[HUD_STATS] = { "HUD passes", "drawn from state", "no driver tick table", "no font yet", "no page",
                                          "records that left the prediction", "records replaced", "records kept", "unsupported",
                                          "replaced records that changed the fine picture", "panel items replaced" };
    return k >= 0 && k < HUD_STATS ? names[k] : "";
}

static int begin_pass(drawlist *d, const int32_t *v, int n)
{
    static F117_TLS hud_state st;
    static F117_TLS hud_exact_list ex;
    static F117_TLS hud_fine_list fl;
    hud_ov *o = &d->hud;
    o->cover.active = 0;
    o->passes++;
    if (!hud_overlay_enabled() || d->n <= 1) return 1;
    if (!hud_state_load(v, n, &st)) { o->unsupported++; return 1; }
    if (!st.have_ticks) { o->no_ticks++; return 1; }
    drawlist_page *p = drawlist_page_of(d, d->seg);
    if (!p || !p->hi || p->n != d->n) { o->no_page++; return 1; }
    if (hud_exact(&st, &ex) < 0) { o->unsupported++; return 1; }
    if (hud_fine(&st, d->n, &ex, &fl) < 0) { o->unsupported++; return 1; }
    for (int i = 0; i < fl.n; i++)
        if (fl.p[i].type == HF_TEXT) {
            const int idx = fl.p[i].blk[6];
            if (idx < 0 || idx >= HUD_FONTS || !o->font[idx].valid) { o->no_font++; return 1; }
        }
    canvas c = { p, 320 * d->n, 200 * d->n, d->n, DT_HUD_STATE_LINE };
    const double width = d->line_width ? d->line_width : d->n;
    for (int i = 0; i < fl.n; i++) {
        const hud_fprim *f = &fl.p[i];
        int cl = f->cl < 0 ? 0 : f->cl, ct = f->ct < 0 ? 0 : f->ct, cr = f->cr > c.W ? c.W : f->cr, cb = f->cb > c.H ? c.H : f->cb;
        if (f->type == HF_LINE) {
            c.tag = DT_HUD_STATE_LINE;
            thick_line(&c, f->x0, f->y0, f->x1, f->y1, width, f->colour, cl, ct, cr, cb);
        } else {
            c.tag = DT_HUD_STATE_TEXT;
            draw_text(&c, f, &o->font[f->blk[6]]);
        }
    }
    o->drawn++;
    hud_cover_begin(&o->cover, &ex);
    return 1;
}

/* run a record of the pass the fine picture replaces with every page's fine mirror off, so the coarse
 * replay (and the colour and text state it keeps) goes on exactly as before */
static int g_debug;
void hud_overlay_debug(int on) { g_debug = on != 0; }

static void run_without_fine(drawlist *d, char kind, const int32_t *v, int n)
{
    int save[DRAWLIST_PAGES];
    uint8_t *before[DRAWLIST_PAGES] = { NULL };
    for (int k = 0; k < d->npages; k++) {
        save[k] = d->page[k].suppress;
        d->page[k].suppress = 1;
        if (g_debug && d->page[k].hi) {
            const size_t bytes = (size_t)320 * d->page[k].n * 200 * d->page[k].n;
            before[k] = (uint8_t *)malloc(bytes);
            if (before[k]) memcpy(before[k], d->page[k].hi, bytes);
        }
    }
    drawlist_record_plain(d, kind, v, n);
    for (int k = 0; k < d->npages; k++) {
        d->page[k].suppress = save[k];
        if (before[k]) {
            const size_t bytes = (size_t)320 * d->page[k].n * 200 * d->page[k].n;
            if (memcmp(before[k], d->page[k].hi, bytes)) d->hud.leaked++;
            free(before[k]);
        }
    }
}

/* ---- the panel pass's items: the flight-path marker and the two bars --------------------------------------- */
/* Draw one at its fine position. The marker is the record's own art (colour 0 transparent) as N x N blocks at the
 * fine position, clipped to the record's clip rectangle; a bar is a stroke in the library colour of the moment. 0 when
 * it cannot (no fine page, no colour): the original's record then draws it as before. */
static int draw_panel_item(drawlist *d, const hud_panel_item *it, const int32_t *v, int n)
{
    const int N = d->n;
    const uint16_t seg = (uint16_t)(it->kind == HPI_FPM ? v[1] : v[5]);
    drawlist_page *p = drawlist_page_of(d, seg);
    if (!p || !p->hi || p->n != N) return 0;
    canvas c = { p, 320 * N, 200 * N, N, DT_HUD_STATE_LINE };
    if (it->kind == HPI_FPM) {
        const int32_t *blk = v + 2;
        const int w = (int16_t)blk[6], h = (int16_t)blk[7];
        if (w <= 0 || h <= 0 || n - 14 != w * h) return 0;
        const int cl = (int16_t)blk[10] * N, cr = ((int16_t)blk[11] + 1) * N, ct = (int16_t)blk[8] * N, cb = ((int16_t)blk[9] + 1) * N;
        const int X0 = (int)floor(it->x0 * N + 0.5), Y0 = (int)floor(it->y0 * N + 0.5);
        c.tag = DT_HUD_STATE_TEXT;
        for (int r = 0; r < h; r++)
            for (int k = 0; k < w; k++) {
                const int colour = v[14 + r * w + k] & 0xFF;
                if (!colour) continue;
                for (int yy = 0; yy < N; yy++)
                    for (int xx = 0; xx < N; xx++) put_fine(&c, X0 + k * N + xx, Y0 + r * N + yy, colour, cl > 0 ? cl : 0, ct > 0 ? ct : 0, cr < c.W ? cr : c.W, cb < c.H ? cb : c.H);
            }
        return 1;
    }
    if (d->colour < 0) return 0;
    const double width = d->line_width ? d->line_width : N;
    thick_line(&c, (it->x0 + 0.5) * N, (it->y0 + 0.5) * N, (it->x1 + 0.5) * N, (it->y1 + 0.5) * N, width, d->colour, 0, 0, c.W, 91 * N);
    return 1;
}

int hud_overlay_hook(drawlist *d, char kind, const int32_t *v, int n)
{
    hud_ov *o = &d->hud;
    switch (kind) {
    case 'P':
        o->cover.active = 0;
        o->panel_valid = 0;
        return 0;
    case 'h':
        return begin_pass(d, v, n);
    case 'p':
        o->panel_valid = hud_overlay_enabled() && d->n > 1 && hud_panel_load(v, n, &o->panel);
        return 1;
    case 'T':
        hud_fonts_note(o, v, n);
        /* fall through */
    case 'K': case 'N': case 'H': case 'S': {
        int handled = 0;
        if (o->cover.active) {
            const int r = hud_cover_step(&o->cover, kind, v, n);
            if (r == HC_REPLACED) { o->replaced++; run_without_fine(d, kind, v, n); return 1; }
            if (r == HC_KEPT) o->kept++;
            if (r == HC_MISMATCH) o->mismatched++;
            handled = r != HC_NONE;
        }
        if (!handled && o->panel_valid && (kind == 'S' || kind == 'N')) {
            hud_panel_item it;
            if (hud_panel_classify(&o->panel, kind, v, n, &it) && draw_panel_item(d, &it, v, n)) {
                o->panel_replaced++;
                run_without_fine(d, kind, v, n);
                return 1;
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}
