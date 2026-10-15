/* hud_state.c - see hud_state.h. The EXACT half is work/hudmodel.py (an integer model of VGAME
 * 1058:0016 written from the disassembly, validated on 288 captured passes) in C; the FINE half is
 * closedform.py's formulas without their truncations. */
#include "hud_state.h"
#include "tls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>


static int s16(int v) { return (int16_t)(uint16_t)v; }
static int s8(int v) { return (int8_t)(uint8_t)v; }

/* ---- data segment access over a working copy ------------------------------------------------- */
typedef struct { uint8_t *d; } mem_t;
#define MB(m, o) ((m)->d[(o) & 0xFFFF])
static int MW(const mem_t *m, int o) { o &= 0xFFFF; return m->d[o] | (m->d[(o + 1) & 0xFFFF] << 8); }
static int MSW(const mem_t *m, int o) { return s16(MW(m, o)); }
static void pB(mem_t *m, int o, int v) { m->d[o & 0xFFFF] = (uint8_t)v; }
static void pW(mem_t *m, int o, int v) { o &= 0xFFFF; m->d[o] = (uint8_t)v; m->d[(o + 1) & 0xFFFF] = (uint8_t)(v >> 8); }

/* ---- loading ----------------------------------------------------------------------------------- */
static void derive(hud_state *st)
{
    mem_t m = { st->ds };
    st->speed = MW(&m, HUD_SPEED_RAW) / 27.0;
    st->alt = (double)MW(&m, HUD_ALT);
    st->hdg = (double)MW(&m, HUD_HDG);
    st->pitch = (double)MSW(&m, HUD_PITCH);
    st->roll = (double)MW(&m, HUD_ROLL);
    st->interpolated = 0;
}

void hud_state_from_ds(const uint8_t *ds, hud_state *st)
{
    memcpy(st->ds, ds, sizeof st->ds);
    st->have_ticks = 0;
    memset(st->tick, 0, sizeof st->tick);
    derive(st);
}

int hud_state_load(const int32_t *v, int n, hud_state *st)
{
    if (n < HUD_REC_BASE || v[0] != HUD_REC_VERSION) return 0;
    memset(st->ds, 0, sizeof st->ds);
    mem_t m = { st->ds };
    for (int k = 0; k < HUD_NWORDS; k++) pW(&m, HUD_DS_LO + 2 * k, v[1 + k]);
    const int ex = 1 + HUD_NWORDS;
    for (int k = 0; k < HUD_NEXTRA; k++) pW(&m, hud_extra_off[k], v[ex + k]);
    const int tk = ex + HUD_NEXTRA;
    st->have_ticks = v[tk] != 0;
    for (int k = 0; k < 4; k++) {
        st->tick[k].x = v[tk + 1 + 3 * k];
        st->tick[k].lo = v[tk + 2 + 3 * k];
        st->tick[k].hi = v[tk + 3 + 3 * k];
    }
    derive(st);
    if (n >= HUD_REC_FINE && v[HUD_REC_BASE] == HUD_FINE_MAGIC) {
        st->speed = v[HUD_REC_BASE + 1] / 1024.0;
        st->alt = v[HUD_REC_BASE + 2] / 256.0;
        st->hdg = v[HUD_REC_BASE + 3] / 256.0;
        st->pitch = v[HUD_REC_BASE + 4] / 256.0;
        st->roll = v[HUD_REC_BASE + 5] / 256.0;
        st->interpolated = 1;
    }
    return 1;
}

/* ---- EXACT --------------------------------------------------------------------------------------- */
static int sine(const mem_t *m, int ang)
{
    ang &= 0xFFFF;
    const int i = ang >> 8, frac = ang & 0xFF;
    const int t0 = MSW(m, 0x2084 + 2 * i), t1 = MSW(m, 0x2086 + 2 * i);
    const int p = (t1 - t0) * frac;
    const int low = (p >> 8) & 0xFFFF, cf = (p >> 7) & 1;
    return s16(t0 + low + cf);
}

/* 0FB2:0330, the integer Cohen-Sutherland clipper with the original's rounding (hudmodel.clip_line).
 * 1 and out[] when something is drawn. */
static int idiv_q(int a, int b, int *rem)
{
    int q = abs(a) / abs(b);
    if ((a < 0) != (b < 0)) q = -q;
    *rem = a - q * b;
    return q;
}

static int clip_intersect(int edge, int frm, int other_from, int num_delta, int den_delta, int den_half, int *ok)
{
    if (den_delta == 0) { *ok = 0; return 0; }
    const int prod = s16(edge - frm) * num_delta;
    int rem;
    int q = idiv_q(prod, den_delta, &rem);
    if ((prod < 0) != (den_delta < 0)) { rem = -rem; q -= 1; }
    const int t = rem - den_half;
    if ((t < 0) == (prod < 0)) q += 1;
    return s16(q + other_from);
}

int hud_clip_line(int x0, int y0, int x1, int y1, int xmax, int ymax, int16_t out[4])
{
#define OUTCODE(x, y) (((x) > xmax ? 1 : 0) | ((y) > ymax ? 2 : 0) | ((y) < 0 ? 4 : 0) | ((x) < 0 ? 8 : 0))
    int cx = x0, bp = y0, si = x1, di = y1;
    int code_from = OUTCODE(cx, bp), code_clip = OUTCODE(si, di);
    int o[4] = { x0, y0, x1, y1 };
    if (code_clip == 0) {
        if (code_from == 0) { for (int k = 0; k < 4; k++) out[k] = (int16_t)o[k]; return 1; }
        int t = cx; cx = si; si = t; t = bp; bp = di; di = t;
        code_clip = code_from; code_from = 0;
        o[0] = cx; o[1] = bp;
    }
    if (code_from & code_clip) return 0;
    int dxx = si - cx, dyy = di - bp;
    if (dxx < -32768 || dxx > 32767 || dyy < -32768 || dyy > 32767) { dxx >>= 1; dyy >>= 1; }
    const int dxh = dxx >> 1, dyh = dyy >> 1;
    for (int guard = 0; guard < 8; guard++) {
        int nx = 0, ny = 0, to_y = 1, y_low = 0, ok = 1;
        if (code_clip & 9) {
            nx = si < 0 ? 0 : xmax;
            ny = clip_intersect(nx, cx, bp, dyy, dxx, dxh, &ok);
            if (!ok) return 0;
            if (ny < 0) y_low = 1;
            else if (ny <= ymax) to_y = 0;
        }
        if (to_y) {
            int edge = 0;
            if (!y_low && di > ymax) edge = ymax;
            nx = clip_intersect(edge, bp, cx, dxx, dyy, dyh, &ok);
            if (!ok || nx < 0 || nx > xmax) return 0;
            ny = edge;
        }
        if (code_from == 0) { o[2] = nx; o[3] = ny; for (int k = 0; k < 4; k++) out[k] = (int16_t)o[k]; return 1; }
        o[0] = nx; o[1] = ny;
        int t = cx; cx = si; si = t; t = bp; bp = di; di = t;
        code_clip = code_from;
        code_from = 0;
    }
    return 0;
#undef OUTCODE
}

typedef struct { hud_exact_list *out; } xout_t;

static F117_TLS int g_item;                   /* the item the prims being made belong to (HI_ / PI_) */
static F117_TLS int g_overflow;

static hud_prim *prim_new(hud_exact_list *l, int kind)
{
    if (l->n >= HUD_MAX_EXACT) { g_overflow = 1; return NULL; }
    hud_prim *p = &l->p[l->n++];
    memset(p, 0, sizeof *p);
    p->kind = (uint8_t)kind;
    p->item = (uint8_t)g_item;
    return p;
}

static void x_k(hud_exact_list *l, int colour)
{
    hud_prim *p = prim_new(l, HP_K);
    if (p) { p->colour = (uint8_t)colour; p->keep = 1; }
}

static void x_n(hud_exact_list *l, int x0, int y0, int x1, int y1, int origin, int keep)
{
    hud_prim *p = prim_new(l, HP_N);
    if (!p) return;
    p->x0 = (int16_t)x0; p->y0 = (int16_t)y0; p->x1 = (int16_t)x1; p->y1 = (int16_t)y1;
    p->origin = origin; p->keep = (uint8_t)keep;
}

static void x_t(hud_exact_list *l, const mem_t *m, int entry, int blk, int strp, int keep)
{
    hud_prim *p = prim_new(l, HP_T);
    if (!p) return;
    p->entry = (uint8_t)entry;
    p->keep = (uint8_t)keep;
    for (int k = 0; k < 11; k++) p->blk[k] = (uint16_t)MW(m, blk + 2 * k);
    int len = 0;
    while (len < HUD_TEXT_MAX && MB(m, strp + len)) { p->text[len] = MB(m, strp + len); len++; }
    p->ntext = (uint8_t)len;
}

static void x_h(hud_exact_list *l, int si, int bx, int dl, int cl)
{
    hud_prim *p = prim_new(l, HP_H);
    if (!p) return;
    p->si = (int16_t)si; p->bx = (int16_t)bx; p->dl = (int16_t)dl; p->cl = (int16_t)cl;
}

/* an entry-18 blit: its block is eight words in the data segment, the strip's at 28A4 (source segment, x, y,
 * destination page, x, y, width, height; the source y was just chosen by the phase) and the reticle's at 28C2 */
static void x_s18(hud_exact_list *l, const mem_t *m, int strip, int keep)
{
    hud_prim *p = prim_new(l, HP_S18);
    if (!p) return;
    p->strip = (uint8_t)strip; p->keep = (uint8_t)keep;
    for (int k = 0; k < 8; k++) p->blk[k] = (uint16_t)MW(m, (strip ? 0x28A4 : 0x28C2) + 2 * k);
}

/* the rotated ladder, as the model builds it */
typedef struct {
    int vx[40], vy[40];               /* unrotated vertices (integers) */
    int rx[40], ry[40];               /* rotated */
    int nv;
    int seg[16], nseg;                /* per-rung segment-group counts */
    int idx[10];                      /* (a, b) vertex byte offsets per rung, flattened */
    int nrung, ch, dl, bx;
} ladder_t;

static F117_TLS int g_lad_rx[40], g_lad_ry[40], g_lad_nv, g_lad_dl;
void hud_debug_ladder(int *nv, const int **rx, const int **ry, int *first_rung)
{
    *nv = g_lad_nv; *rx = g_lad_rx; *ry = g_lad_ry; *first_rung = g_lad_dl;
}

int hud_exact(const hud_state *st, hud_exact_list *out)
{
    static F117_TLS uint8_t buf[0x10000];
    memcpy(buf, st->ds, sizeof buf);
    mem_t m = { buf }, *d = &m;
    out->n = 0;
    g_overflow = 0;
    g_item = HI_ASPD;
    const int mode = MB(d, 0x294A), half = MB(d, 0x294B);

    /* the preamble: AX = AL of entry 45 into the eight blocks' page words */
    {
        const int al = MB(d, 0x284C);
        static const int blks[8] = { 0x284C, 0x2862, 0x2878, 0x288E, 0x28C8, 0x28AA, 0x2904, 0x28E6 };
        for (int k = 0; k < 8; k++) pW(d, blks[k], al);
    }

    /* ---- airspeed tape -------------------------------------------------------------------- */
    if (mode == 0) {
        const int v = MW(d, 0xB194);
        const int q = v / 50, r = v % 50;
        if (q >= 256) return -1;
        const int dl = (q - 1) & 0xFF;
        const int cl = MB(d, 0x2922);
        int ax = (r << cl) & 0xFFFF;
        const int q2 = ax / 5;
        if (q2 >= 256) return -1;
        int al = (q2 + MB(d, 0x291D)) & 0xFF;
        pB(d, 0x2856, al);
        pB(d, 0x2971, dl);
        al = (al - MB(d, 0x291F)) & 0xFF;
        pB(d, 0x2952, al);
        int di = (dl << 1) & 0xFF;
        pB(d, 0x2955, 5);
        for (;;) {
            pB(d, 0x2856, MB(d, 0x2856) - MB(d, 0x2920));
            pW(d, 0x2854, MW(d, 0x2923));
            if (di >= 0x28) {
                di = 0;
                pB(d, 0x2955, MB(d, 0x2955) - 1);
                if (MB(d, 0x2955) == 0) break;
                pW(d, 0x2952, MW(d, 0x2952) - MW(d, 0x2920));
                continue;
            }
            pW(d, 0x296C, MW(d, di + 0x297E));
            pW(d, 0x296E, MW(d, di + 0x29CE));
            x_t(out, d, 1, 0x284C, 0x296C, 0);
            di = (di + 2) & 0xFFFF;
            pB(d, 0x2955, MB(d, 0x2955) - 1);
            if (MB(d, 0x2955) == 0) break;
        }
        x_h(out, 0, MW(d, 0x2952), s8(MB(d, 0x2971)), MB(d, 0x294B));
    }
    /* ---- the fixed pointer marks ---------------------------------------------------------------- */
    g_item = HI_MARKS;
    if (mode <= 1) {
        x_k(out, MB(d, 0x2CA0));
        if (half) {
            if (mode == 0) { x_n(out, 0x7A, 0x3C, 0x7C, 0x3C, 0, 0); x_n(out, 0xC4, 0x3C, 0xC6, 0x3C, 0, 0); }
            x_n(out, 0x9F, 0x1D, 0x9F, 0x1E, 0, 0);
        } else {
            if (mode == 0) { x_n(out, 0x49, 0x34, 0x4C, 0x34, 0, 0); x_n(out, 0xF3, 0x34, 0xF6, 0x34, 0, 0); }
            x_n(out, 0x9F, 0x10, 0x9F, 0x12, 0, 0);
        }
    }
    /* ---- altitude tape -------------------------------------------------------------------- */
    g_item = HI_ALT;
    if (mode == 0) {
        const int alt = MW(d, 0x2DF6);
        const int band = alt / 1000, rem = alt % 1000;
        int bx = (band - 1) & 0xFFFF;
        pW(d, 0x294C, rem);
        int ax = (rem << MB(d, 0x2922)) & 0xFFFF;
        const int q100 = ax / 100;
        bx = (bx << 1) & 0xFFFF;
        int di = bx;
        pB(d, 0x2955, 5);
        if (s16(di) >= 0) {
            ax = (q100 + MW(d, 0x291D)) & 0xFFFF;
            pW(d, 0x2856, ax);
            pB(d, 0x2971, 2);
            pB(d, 0x2952, (ax - MB(d, 0x291F)) & 0xFF);
            for (;;) {
                pB(d, 0x2856, MB(d, 0x2856) - MB(d, 0x2920));
                pW(d, 0x2854, MW(d, 0x2925));
                int strp;
                if (di == 0) strp = 0x297A;
                else { pW(d, 0x2972, MW(d, di + 0x2A1E)); strp = 0x2972; }
                x_t(out, d, 1, 0x284C, strp, 0);
                di = (di + 2) & 0xFFFF;
                pB(d, 0x2955, MB(d, 0x2955) - 1);
                if (MB(d, 0x2955) != 0) continue;
                break;
            }
        } else {
            ax = MW(d, 0x294C);
            const int qq = ax / 100, rr = ax % 100;
            if (qq >= 256) return -1;
            const int dl = (qq - 1) & 0xFF;
            ax = (rr << MB(d, 0x2922)) & 0xFFFF;
            const int q2 = ax / 10;
            if (q2 >= 256) return -1;
            const int al = (q2 + MB(d, 0x291D)) & 0xFF;
            pB(d, 0x2856, al);
            pB(d, 0x2971, dl);
            pB(d, 0x2952, (al - MB(d, 0x291F)) & 0xFF);
            di = (dl << 1) & 0xFF;
            for (;;) {
                pB(d, 0x2856, MB(d, 0x2856) - MB(d, 0x2920));
                pW(d, 0x2854, MW(d, 0x2925));
                if (di >= 0x28) {
                    di = 0;
                    pB(d, 0x2955, MB(d, 0x2955) - 1);
                    pW(d, 0x2952, MW(d, 0x2952) - MW(d, 0x2920));
                    continue;
                }
                pB(d, 0x2976, MB(d, di + 0x2A1F));
                x_t(out, d, 1, 0x284C, 0x2976, 0);
                di = (di + 2) & 0xFFFF;
                pB(d, 0x2955, MB(d, 0x2955) - 1);
                if (MB(d, 0x2955) == 0) break;
                if (di < 0x14) continue;
                di = 2;
                for (;;) {
                    pB(d, 0x2856, MB(d, 0x2856) - MB(d, 0x2920));
                    pW(d, 0x2854, MW(d, 0x2925));
                    int strp;
                    if (di == 0) strp = 0x297A;
                    else { pW(d, 0x2972, MW(d, di + 0x2A1E)); strp = 0x2972; }
                    x_t(out, d, 1, 0x284C, strp, 0);
                    di = (di + 2) & 0xFFFF;
                    pB(d, 0x2955, MB(d, 0x2955) - 1);
                    if (MB(d, 0x2955) == 0) break;
                }
                break;
            }
        }
        x_h(out, 2, MW(d, 0x2952), s8(MB(d, 0x2971)), MB(d, 0x294B));
    }
    /* ---- heading strip ------------------------------------------------------------------------ */
    g_item = HI_HDG;
    if (mode <= 1) {
        const int hdg = MW(d, 0x2DEE);
        const int ax = (hdg - 0x2000) & 0xFFFF;
        const int dl = ax >> 8;
        const int vv0 = ax & 0x1F80;
        const int al = (vv0 >> 7) & 0xFF;
        const int sub = ((al * MB(d, 0x2927)) >> 6) & 0xFF;
        pB(d, 0x2954, sub);
        const int x0 = (MB(d, 0x291C) - sub) & 0xFF;
        pW(d, 0x2950, x0);
        pW(d, 0x286A, x0);
        const int idx = (dl >> 2) & 0x38;
        pW(d, 0x294E, idx);
        x_t(out, d, 2, 0x2862, 0x2AAA + idx, 0);
        for (int k = 0; k < 2; k++) {
            pW(d, 0x294E, (MW(d, 0x294E) + 8) & 0x3F);
            const int x = (MW(d, 0x2950) + MW(d, 0x2927)) & 0xFFFF;
            pW(d, 0x2950, x);
            pW(d, 0x286A, x);
            x_t(out, d, 4, 0x2862, 0x2AAA + MW(d, 0x294E), 0);
        }
        if (MW(d, 0x2950) < MW(d, 0x2929)) {
            pW(d, 0x294E, (MW(d, 0x294E) + 8) & 0x3F);
            int bx = 0x2AAA + MW(d, 0x294E);
            pW(d, bx + 4, MW(d, bx));
            pW(d, bx + 6, MW(d, bx + 2));
            bx += 4;
            const int x = (MW(d, 0x2950) + MW(d, 0x2927)) & 0xFFFF;
            pW(d, 0x2950, x);
            pW(d, 0x286A, x);
            x_t(out, d, 3, 0x2862, bx, 0);
            const int f = MB(d, 0x2954), dd = MB(d, 0x292B);
            int vv = dd ? f % dd : 0;
            if (MW(d, 0x2DEE) & 0x2000) vv = (vv + MW(d, 0x292D)) & 0xFFFF;
            if (s16(vv) >= s16(MW(d, 0x292B))) vv = (vv - MW(d, 0x292B)) & 0xFFFF;
            const int di = (vv << 1) & 0xFFFF;
            pW(d, 0x28A8, MW(d, di + (half == 1 ? 0x2AFE : 0x2AEA)));
            g_item = HI_STRIP;
            x_s18(out, d, 1, 0);
        }
    }
    /* ---- the frame -------------------------------------------------------------------------------- */
    g_item = HI_FRAME;
    {
        x_k(out, 15);
        if (half == 0) {
            x_n(out, 0x2E, 0x5A, 0x2E, 0x0E, 0, 0); x_n(out, 0x112, 0x5A, 0x112, 0x0E, 0, 0);
            x_k(out, 8);
            x_n(out, 0x2D, 0x5A, 0x2D, 0x0E, 0, 0); x_n(out, 0x113, 0x5A, 0x113, 0x0E, 0, 0);
            x_n(out, 0x113, 0x0E, 0xE8, 2, 0, 0); x_n(out, 0xE8, 2, 0x5A, 2, 0, 0); x_n(out, 0x2D, 0x0E, 0x59, 2, 0, 0);
        } else {
            x_n(out, 0x62, 0x5A, 0x62, 0x1E, 0, 0); x_n(out, 0xDD, 0x5A, 0xDD, 0x1E, 0, 0);
            x_k(out, 8);
            x_n(out, 0x61, 0x5A, 0x61, 0x1E, 0, 0); x_n(out, 0xDE, 0x5A, 0xDE, 0x1E, 0, 0);
            x_n(out, 0xDE, 0x1E, 0xD1, 0x11, 0, 0); x_n(out, 0xD1, 0x11, 0x6E, 0x11, 0, 0); x_n(out, 0x6E, 0x11, 0x61, 0x1E, 0, 0);
        }
    }
    /* ---- readout bitmap (the reticle) and the G meter ---------------------------------------------- */
    g_item = HI_RETICLE;
    if (MB(d, 0x2956)) x_s18(out, d, 0, 0);
    g_item = HI_G;
    pW(d, 0x2896, MW(d, 0x2948));
    x_t(out, d, 4, 0x288E, 0x9900, 0);

    /* ---- pitch ladder -------------------------------------------------------------------------------- */
    g_item = HI_LADDER;
    ladder_t L;
    memset(&L, 0, sizeof L);
    {
        const int pitch = MSW(d, 0x2DF0);
        int a = pitch < 0 ? -pitch : pitch;
        a = (a & 0xFFFF) >> 6;
        const int p = a * 0x168;
        const int mid = (p >> 8) & 0xFFFF;
        const int q = mid / 40, r = mid % 40;
        if (q >= 256) return -1;
        int rung = q;
        const int sp = MB(d, 0x2937);
        int off = (r * sp) / 40;
        if (pitch < 0) off = (sp - (off & 0xFF)) & 0xFF;
        const int bx = ((off & 0xFF) + MB(d, 0x2939)) & 0xFF;
        if (pitch < 0) rung = (-rung - 1) & 0xFF;
        const int dl = s8(rung) - 2;
        pW(d, 0x2C2C, dl & 0xFFFF);
        int X[4];
        for (int k = 0; k < 4; k++) X[k] = MSW(d, 0x292F + 2 * k);
        const int S = MSW(d, 0x2937);
        int di = 0, ch = 0, y = bx;
        int dlb = dl & 0xFF;
        for (int n = 0; n < 5; n++) {
            const int dd = s8(dlb);
            const int x0 = X[0], x1 = X[1], x2 = X[2], x3 = X[3];
            int *vx = L.vx + L.nv, *vy = L.vy + L.nv;
            if (dd == 9) {
                vx[0] = x0; vx[1] = x3; vy[0] = vy[1] = y; L.nv += 2;
                L.seg[L.nseg++] = 1; L.seg[L.nseg++] = 0;
                L.idx[2 * n] = di; L.idx[2 * n + 1] = di + 2; di += 4; ch += 1;
            } else if (dd > 9 || (dd > 0 && dd < 9)) {
                const int tick = dd > 9 ? y - 5 : y + 5;
                vx[0] = x0; vx[1] = x0; vx[2] = x3; vx[3] = x3; vy[0] = tick; vy[1] = y; vy[2] = y; vy[3] = tick; L.nv += 4;
                L.seg[L.nseg++] = 3; L.seg[L.nseg++] = 0;
                L.idx[2 * n] = di + 2; L.idx[2 * n + 1] = di + 4; di += 8; ch += 3;
            } else if (dd == 0 || dd == -9) {
                vx[0] = x0; vx[1] = x1; vx[2] = x2; vx[3] = x3; vy[0] = vy[1] = vy[2] = vy[3] = y; L.nv += 4;
                L.seg[L.nseg++] = 1; L.seg[L.nseg++] = 1;
                L.idx[2 * n] = di; L.idx[2 * n + 1] = di + 6; di += 8; ch += 2;
            } else {
                const int tick = dd < -9 ? y + 5 : y - 5;
                vx[0] = x0; vx[1] = x0; vx[2] = x1; vx[3] = x2; vx[4] = x3; vx[5] = x3;
                vy[0] = tick; vy[1] = vy[2] = vy[3] = vy[4] = y; vy[5] = tick; L.nv += 6;
                L.seg[L.nseg++] = 2; L.seg[L.nseg++] = 2;
                L.idx[2 * n] = di + 2; L.idx[2 * n + 1] = di + 8; di += 12; ch += 4;
            }
            y = (y - S) & 0xFFFF;
            dlb = (dlb + 1) & 0xFF;
        }
        L.ch = ch; L.dl = dl; L.bx = bx;
        const int roll = MW(d, 0x2DF2);
        const int c = sine(d, (0x4000 - roll) & 0xFFFF), sn = sine(d, (-roll) & 0xFFFF);
        for (int k = 0; k < L.nv; k++) {
            const int x = s16(L.vx[k]), yv = s16(L.vy[k]);
            const int t1 = (c * x) >> 15, t2 = (sn * yv) >> 15;
            L.rx[k] = s16(t1 - t2);
            const int u1 = (3 * c * yv) >> 17, u2 = (3 * sn * x) >> 17;
            L.ry[k] = s16(u1 + u2);
        }
        g_lad_nv = L.nv;
        g_lad_dl = L.dl;
        memcpy(g_lad_rx, L.rx, sizeof L.rx);
        memcpy(g_lad_ry, L.ry, sizeof L.ry);
    }
    /* the ladder's drawing half (1058:0806 .. 09D2) */
    if (MB(d, HUD_LADDER_ON) == 0) return g_overflow ? -1 : 0;
    x_k(out, MB(d, 0x2CA1));
    {
        const int origin = MW(d, 0x2942);
        const int xmax = MSW(d, 0x2944), ymax = MSW(d, 0x2946);
        const int ox = MSW(d, 0x293A), oy = MSW(d, 0x293C);
        int counts[20];
        memset(counts, 0, sizeof counts);
        for (int k = 0; k < L.nseg && k < 16; k++) counts[k] = L.seg[k];
        int di = 0, si = 0, cnt = 0;
        for (int guard = 0; guard < 64; guard++) {
            const int x0 = s16(L.rx[di / 2] + ox), y0 = s16(L.ry[di / 2] + oy);
            const int x1 = s16(L.rx[di / 2 + 1] + ox), y1 = s16(L.ry[di / 2 + 1] + oy);
            int16_t c[4];
            if (hud_clip_line(x0, y0, x1, y1, xmax, ymax, c)) x_n(out, c[0], c[1], c[2], c[3], origin, 0);
            di += 2;
            cnt += 1;
            counts[si] = (counts[si] - 1) & 0xFF;
            if (counts[si] != 0) continue;
            di += 2;
            si += 1;
            if (counts[si] == 0) si += 1;
            if (s8(cnt) < s8(L.ch)) continue;
            break;
        }
        /* the labels */
        g_item = HI_LADDER_LABEL;
        int first = (L.dl + 11) & 0xFFFF;
        const int roll = MW(d, 0x2DF2);
        const int i0 = ((roll >> 8) >> 1 >> 1) & 0xFF;
        const int i1 = ((((roll >> 8) + 0x80) & 0xFF) >> 2) & 0xFF;
        int t[0x100];
#define T(a) t[((a) - 0x2C90) / 2]
        T(0x2C98) = (MW(d, 0x264C + 2 * i0) + 2) & 0xFFFF;
        T(0x2C9A) = MW(d, 0x26CC + 2 * i0);
        T(0x2C9C) = (MW(d, 0x274C + 2 * i0) + 2) & 0xFFFF;
        T(0x2C9E) = MW(d, 0x27CC + 2 * i0);
        T(0x2C90) = (MW(d, 0x264C + 2 * i1) - 1) & 0xFFFF;
        T(0x2C92) = MW(d, 0x26CC + 2 * i1);
        T(0x2C94) = (MW(d, 0x274C + 2 * i1) - 1) & 0xFFFF;
        T(0x2C96) = MW(d, 0x27CC + 2 * i1);
        uint8_t idxb[40];
        memset(idxb, 0, sizeof idxb);
        for (int k = 0; k < 5; k++) { idxb[2 * k] = (uint8_t)L.idx[2 * k]; idxb[2 * k + 1] = (uint8_t)L.idx[2 * k + 1]; }
        int lsi = 0;
        for (int rungi = 0; rungi < 5; rungi++) {
            for (int end = 0; end < 2; end++) {
                const int bxv = (first << 2) & 0xFFFF;
                pW(d, 0x2C8C, MW(d, (0x2C30 + bxv) & 0xFFFF));
                pW(d, 0x2C8E, MW(d, (0x2C32 + bxv) & 0xFFFF));
                const int vi = idxb[lsi] & 0xFF;
                int ax = s16(L.rx[vi / 2]), ay = s16(L.ry[vi / 2]);
                if (s16(bxv) < 0x2C) ax += s16(T(end ? 0x2C9C : 0x2C94));
                else ax += s16(T(end ? 0x2C98 : 0x2C90));
                ax += MSW(d, 0x2940);
                if (vi < 0x2C) ay += s16(T(end ? 0x2C9E : 0x2C96));
                else ay += s16(T(end ? 0x2C9A : 0x2C92));
                ay += MSW(d, 0x293E);
                pW(d, 0x2880, ax);
                pW(d, 0x2882, ay);
                x_t(out, d, 6, 0x2878, 0x2C8C, 0);
                lsi++;
            }
            first = (first + 1) & 0xFFFF;
        }
#undef T
    }
    return g_overflow ? -1 : 0;
}

/* ---- FINE ----------------------------------------------------------------------------------------- */
static hud_fprim *fine_new(hud_fine_list *l)
{
    if (l->n >= HUD_MAX_FINE) { l->overflow = 1; return NULL; }
    hud_fprim *p = &l->p[l->n++];
    memset(p, 0, sizeof *p);
    p->item = (uint8_t)g_item;
    return p;
}

static void f_line(hud_fine_list *l, int N, double cx0, double cy0, double cx1, double cy1, int colour,
                   int cl, int ct, int cr, int cb)
{
    hud_fprim *p = fine_new(l);
    if (!p) return;
    p->type = HF_LINE;
    p->colour = (uint8_t)colour;
    /* vertices are cell centres */
    p->x0 = (cx0 + 0.5) * N; p->y0 = (cy0 + 0.5) * N;
    p->x1 = (cx1 + 0.5) * N; p->y1 = (cy1 + 0.5) * N;
    p->cl = cl * N; p->ct = ct * N; p->cr = (cr + 1) * N; p->cb = (cb + 1) * N;
}

static void f_text(hud_fine_list *l, int N, const mem_t *m, int blk, int entry, double x, double y, const uint8_t *text, int ntext)
{
    hud_fprim *p = fine_new(l);
    if (!p) return;
    p->type = HF_TEXT;
    p->entry = (uint8_t)entry;
    for (int k = 0; k < 11; k++) p->blk[k] = (uint16_t)MW(m, blk + 2 * k);
    p->colour = (uint8_t)(p->blk[2] & 0xFF);
    p->x0 = x * N; p->y0 = y * N;
    p->ntext = (uint8_t)(ntext > HUD_TEXT_MAX ? HUD_TEXT_MAX : ntext);
    memcpy(p->text, text, p->ntext);
    p->cl = 0; p->ct = 0; p->cr = 320 * N; p->cb = 200 * N;
}

static int text_from(const mem_t *m, int at, uint8_t *out)
{
    int n = 0;
    while (n < 11 && MB(m, at + n)) { out[n] = MB(m, at + n); n++; }
    return n;
}

/* a bitmap the original blits from the data segment's eight-word block at blk (source segment, x, y, page,
 * destination x, y, width, height), placed at (x, y) cells */
static void f_sprite(hud_fine_list *l, int N, const mem_t *m, int blk, double x, double y)
{
    hud_fprim *p = fine_new(l);
    if (!p) return;
    p->type = HF_SPRITE;
    for (int k = 0; k < 8; k++) p->blk[k] = (uint16_t)MW(m, blk + 2 * k);
    p->x0 = x * N; p->y0 = y * N;
    p->cl = 0; p->ct = 0; p->cr = 320 * N; p->cb = 200 * N;
}

static const double PI_ = 3.14159265358979323846;

static F117_TLS double g_fl_rx[40], g_fl_ry[40];
static F117_TLS int g_fl_nv, g_fl_first;
void hud_debug_fine_ladder(int *nv, const double **rx, const double **ry, int *first_rung)
{
    *nv = g_fl_nv; *rx = g_fl_rx; *ry = g_fl_ry; *first_rung = g_fl_first;
}

int hud_fine(const hud_state *st, int N, const hud_exact_list *ex, hud_fine_list *out)
{
    /* the formulas poke a few scratch strings into the data segment, so they work on a copy */
    static F117_TLS uint8_t buf[0x10000];
    memcpy(buf, st->ds, sizeof buf);
    mem_t m = { buf }, *d = &m;
    int has_strip = 1;
    if (ex) {
        has_strip = 0;
        for (int i = 0; i < ex->n; i++) if (ex->p[i].kind == HP_S18 && ex->p[i].strip) has_strip = 1;
    }
    out->n = 0; out->overflow = 0;
    const int mode = MB(d, 0x294A), half = MB(d, 0x294B);
    const int top = MB(d, 0x291D), step = MB(d, 0x2920), sh = MB(d, 0x2922);
    const double yref = top - 2.0 * step;
    const int tape_colour = MW(d, 0x284C + 4) & 0xFF;

    /* a tape (airspeed: ti 0/2 by the small flag; altitude: 1/3) */
    g_item = HI_ASPD;
    if (mode == 0) {
        /* airspeed labels and ticks */
        const double sc = (double)(1 << sh) / 5.0;
        const double v = st->speed < 0 ? 0 : st->speed;
        const int x = MW(d, 0x2923);
        for (int mm = 0; mm < 20; mm++) {
            const double y = yref + sc * (v - 50.0 * mm);
            if (y < MW(d, 0x284C + 14) - 8 || y > MW(d, 0x284C + 16) + 1) continue;
            uint8_t text[12];
            pW(d, 0x296C, MW(d, 0x297E + 2 * mm));
            pW(d, 0x296E, MW(d, 0x29CE + 2 * mm));
            const int nt = text_from(d, 0x296C, text);
            f_text(out, N, d, 0x284C, 1, x, y, text, nt);
            out->p[out->n - 1].cl = 0;
        }
        /* ticks: from the lattice's lowest mark upward */
        if (st->have_ticks) {
            const int ti = half ? 2 : 0, xx = st->tick[ti].x, lo = st->tick[ti].lo, hi = st->tick[ti].hi;
            const double unit = 2.0 / sc;
            const int q = (int)floor(v / 50.0);
            const double vmin = q >= 2 ? 50.0 * (q - 2) : 0.0;
            for (int k = (int)floor(vmin / unit + 0.5); k < (int)(vmin / unit) + 600; k++) {
                const double y = yref + 2.0 + sc * (v - unit * k);
                if (y < lo - 1.5) break;
                if (y > hi + 1.5) continue;
                const int nn = (k % 10 == 0) ? (half ? 2 : 3) : (k % 5 == 0 ? 2 : 1);
                f_line(out, N, xx, y, xx - (nn - 1), y, tape_colour, 0, lo, 319, hi);
            }
        }
    }
    /* the fixed pointer marks beside the tapes and under the heading strip (the original's own lines) */
    g_item = HI_MARKS;
    if (mode <= 1) {
        const int pc = MB(d, 0x2CA0);
        if (half) {
            if (mode == 0) { f_line(out, N, 0x7A, 0x3C, 0x7C, 0x3C, pc, 0, 0, 319, 199); f_line(out, N, 0xC4, 0x3C, 0xC6, 0x3C, pc, 0, 0, 319, 199); }
            f_line(out, N, 0x9F, 0x1D, 0x9F, 0x1E, pc, 0, 0, 319, 199);
        } else {
            if (mode == 0) { f_line(out, N, 0x49, 0x34, 0x4C, 0x34, pc, 0, 0, 319, 199); f_line(out, N, 0xF3, 0x34, 0xF6, 0x34, pc, 0, 0, 319, 199); }
            f_line(out, N, 0x9F, 0x10, 0x9F, 0x12, pc, 0, 0, 319, 199);
        }
    }
    g_item = HI_ALT;
    if (mode == 0) {
        /* altitude labels and ticks */
        const double alt = st->alt < 0 ? 0 : st->alt;
        const int xa = MW(d, 0x2925);
        const int high = alt >= 1000.0;
        const double sca = (double)(1 << sh) / (high ? 100.0 : 10.0);
        const int nmax = (0x2AAA - 0x2A1E) / 2;
        if (high) {
            for (int mm = 0; mm < nmax; mm++) {
                const double y = yref + sca * (alt - 1000.0 * mm);
                if (y < MW(d, 0x284C + 14) - 8 || y > MW(d, 0x284C + 16) + 1) continue;
                uint8_t text[12];
                int nt;
                if (mm == 0) nt = text_from(d, 0x297A, text);
                else { pW(d, 0x2972, MW(d, 0x2A1E + 2 * mm)); nt = text_from(d, 0x2972, text); }
                f_text(out, N, d, 0x284C, 1, xa, y, text, nt);
            }
        } else {
            for (int j = 0; j < 10; j++) {
                const double y = yref + sca * (alt - 100.0 * j);
                if (y < MW(d, 0x284C + 14) - 8 || y > MW(d, 0x284C + 16) + 1) continue;
                uint8_t text[12];
                pB(d, 0x2976, MB(d, 0x2A1F + 2 * j));
                const int nt = text_from(d, 0x2976, text);
                f_text(out, N, d, 0x284C, 1, xa, y, text, nt);
            }
            const double y = yref + sca * (alt - 1000.0);          /* the next thousand */
            if (!(y < MW(d, 0x284C + 14) - 8 || y > MW(d, 0x284C + 16) + 1)) {
                uint8_t text[12];
                pW(d, 0x2972, MW(d, 0x2A1E + 2));
                const int nt = text_from(d, 0x2972, text);
                f_text(out, N, d, 0x284C, 1, xa, y, text, nt);
            }
        }
        if (st->have_ticks) {
            const int ti = half ? 3 : 1, xx = st->tick[ti].x, lo = st->tick[ti].lo, hi = st->tick[ti].hi;
            const double unit = 2.0 / sca;
            double vmin;
            if (high) vmin = 1000.0 * ((int)(alt / 1000.0) - 2);
            else { const int q2 = (int)(alt / 100.0); vmin = q2 >= 2 ? 100.0 * (q2 - 2) : 0.0; }
            for (int k = (int)floor(vmin / unit + 0.5); k < (int)(vmin / unit) + 600; k++) {
                const double y = yref + 2.0 + sca * (alt - unit * k);
                if (y < lo - 1.5) break;
                if (y > hi + 1.5) continue;
                const int nn = (k % 10 == 0) ? (half ? 2 : 3) : (k % 5 == 0 ? 2 : 1);
                f_line(out, N, xx, y, xx + (nn - 1), y, tape_colour, 0, lo, 319, hi);
            }
        }
    }
    /* heading labels and the tick strip */
    g_item = HI_HDG;
    if (mode <= 1) {
        const double hdg = st->hdg * 360.0 / 65536.0;
        const int L = MB(d, 0x291C), I = MW(d, 0x2927);
        const double k = I / 45.0;
        const int left = MW(d, 0x2862 + 18), right = MW(d, 0x2862 + 20);
        const int row = MW(d, 0x2862 + 10);
        const double jlo = floor((hdg - 200.0) / 45.0), jhi = ceil((hdg + 200.0) / 45.0);
        for (int j = (int)jlo; j <= (int)jhi; j++) {
            const double x = L + I + k * (45.0 * j - hdg);
            if (x + 12 <= left || x > right + 1 || x - I >= MW(d, 0x2929)) continue;
            const int e = ((j % 8) + 8) % 8;
            uint8_t text[12];
            int nt = 0;
            while (nt < 4 && MB(d, 0x2AAA + 8 * e + nt)) { text[nt] = MB(d, 0x2AAA + 8 * e + nt); nt++; }
            f_text(out, N, d, 0x2862, 6, x, row, text, nt);
        }
        /* strip: marks every 10 degrees (the major, the whole height) and every 5 (the bottom row) */
        const int sx = MW(d, 0x28AC), sy = MW(d, 0x28AE), sw = MW(d, 0x28B0), shh = MW(d, 0x28B2);
        const double tlo = floor((hdg - 120.0) / 5.0), thi = ceil((hdg + 120.0) / 5.0);
        g_item = HI_STRIP;
        for (int j = (int)tlo; j <= (int)thi && has_strip; j++) {
            const double c = 159.0 + k * (5.0 * j - hdg);
            if (c < sx - 1.0 || c > sx + sw) continue;
            const int major = ((j % 2) + 2) % 2 == 0;
            f_line(out, N, c, major ? sy : sy + shh - 1, c, sy + shh - 1, tape_colour, sx, sy, sx + sw - 1, sy + shh - 1);
        }
    }
    /* the frame (constants: the vertical sides are the original's cells, the slanted top is a fine line) */
    g_item = HI_FRAME;
    if (half == 0) {
        f_line(out, N, 0x2E, 0x5A, 0x2E, 0x0E, 15, 0, 0, 319, 199); f_line(out, N, 0x112, 0x5A, 0x112, 0x0E, 15, 0, 0, 319, 199);
        f_line(out, N, 0x2D, 0x5A, 0x2D, 0x0E, 8, 0, 0, 319, 199); f_line(out, N, 0x113, 0x5A, 0x113, 0x0E, 8, 0, 0, 319, 199);
        f_line(out, N, 0x113, 0x0E, 0xE8, 2, 8, 0, 0, 319, 199); f_line(out, N, 0xE8, 2, 0x5A, 2, 8, 0, 0, 319, 199);
        f_line(out, N, 0x2D, 0x0E, 0x59, 2, 8, 0, 0, 319, 199);
    } else {
        f_line(out, N, 0x62, 0x5A, 0x62, 0x1E, 15, 0, 0, 319, 199); f_line(out, N, 0xDD, 0x5A, 0xDD, 0x1E, 15, 0, 0, 319, 199);
        f_line(out, N, 0x61, 0x5A, 0x61, 0x1E, 8, 0, 0, 319, 199); f_line(out, N, 0xDE, 0x5A, 0xDE, 0x1E, 8, 0, 0, 319, 199);
        f_line(out, N, 0xDE, 0x1E, 0xD1, 0x11, 8, 0, 0, 319, 199); f_line(out, N, 0xD1, 0x11, 0x6E, 0x11, 8, 0, 0, 319, 199);
        f_line(out, N, 0x6E, 0x11, 0x61, 0x1E, 8, 0, 0, 319, 199);
    }
    /* the centre reticle (its art, from the original's blit) and the G meter's text */
    g_item = HI_RETICLE;
    if (MB(d, 0x2956)) f_sprite(out, N, d, 0x28C2, MSW(d, 0x28CA), MSW(d, 0x28CC));
    g_item = HI_G;
    {
        pW(d, 0x2896, MW(d, 0x2948));
        uint8_t text[12];
        const int nt = text_from(d, 0x9900, text);
        f_text(out, N, d, 0x288E, 0, MSW(d, 0x2948), MSW(d, 0x2898), text, nt);
    }
    /* the pitch ladder */
    g_item = HI_LADDER;
    if (MB(d, HUD_LADDER_ON) != 0) {
        const double pitch_deg = st->pitch * 360.0 / 65536.0;
        const double Rr = st->roll * 2.0 * PI_ / 65536.0;
        const double c = cos(Rr), s = sin(Rr);
        const double S = (double)MSW(d, 0x2937);
        const int origin = MW(d, 0x2942);
        const int ox0 = origin % 320, oy0 = origin / 320;
        const int xmax = MSW(d, 0x2944), ymax = MSW(d, 0x2946);
        const int px = MSW(d, 0x293A), py = MSW(d, 0x293C);
        const int colour = MB(d, 0x2CA1);
        double X[4];
        for (int q = 0; q < 4; q++) X[q] = (double)MSW(d, 0x292F + 2 * q);
        const int n0 = (int)floor(pitch_deg / 10.0) - 2;
        double vx[40], vy[40], rx[40], ry[40];
        int nv = 0, va[5][2];
        struct { int a, b, c2, dd; int ns; int s0[4], s1[4]; } sg[5];
        memset(sg, 0, sizeof sg);
        for (int j = 0; j < 5; j++) {
            const int n = n0 + j;
            const double y = S / 10.0 * (pitch_deg - 10.0 * n);
            const int b0 = nv;
            if (n == 9) {
                vx[nv] = X[0]; vy[nv++] = y; vx[nv] = X[3]; vy[nv++] = y;
                sg[j].ns = 1; sg[j].s0[0] = b0; sg[j].s1[0] = b0 + 1; va[j][0] = b0; va[j][1] = b0 + 1;
            } else if (n > 9 || (n > 0 && n < 9)) {
                const double t = n > 9 ? y - 5 : y + 5;
                vx[nv] = X[0]; vy[nv++] = t; vx[nv] = X[0]; vy[nv++] = y; vx[nv] = X[3]; vy[nv++] = y; vx[nv] = X[3]; vy[nv++] = t;
                sg[j].ns = 3;
                for (int q = 0; q < 3; q++) { sg[j].s0[q] = b0 + q; sg[j].s1[q] = b0 + q + 1; }
                va[j][0] = b0 + 1; va[j][1] = b0 + 2;
            } else if (n == 0 || n == -9) {
                vx[nv] = X[0]; vy[nv++] = y; vx[nv] = X[1]; vy[nv++] = y; vx[nv] = X[2]; vy[nv++] = y; vx[nv] = X[3]; vy[nv++] = y;
                sg[j].ns = 2; sg[j].s0[0] = b0; sg[j].s1[0] = b0 + 1; sg[j].s0[1] = b0 + 2; sg[j].s1[1] = b0 + 3;
                va[j][0] = b0; va[j][1] = b0 + 3;
            } else {
                const double t = n < -9 ? y + 5 : y - 5;
                vx[nv] = X[0]; vy[nv++] = t; vx[nv] = X[0]; vy[nv++] = y; vx[nv] = X[1]; vy[nv++] = y;
                vx[nv] = X[2]; vy[nv++] = y; vx[nv] = X[3]; vy[nv++] = y; vx[nv] = X[3]; vy[nv++] = t;
                sg[j].ns = 4;
                sg[j].s0[0] = b0; sg[j].s1[0] = b0 + 1; sg[j].s0[1] = b0 + 1; sg[j].s1[1] = b0 + 2;
                sg[j].s0[2] = b0 + 3; sg[j].s1[2] = b0 + 4; sg[j].s0[3] = b0 + 4; sg[j].s1[3] = b0 + 5;
                va[j][0] = b0 + 1; va[j][1] = b0 + 4;
            }
        }
        for (int q = 0; q < nv; q++) {
            rx[q] = c * vx[q] + s * vy[q];
            ry[q] = 0.75 * (c * vy[q] - s * vx[q]);
        }
        g_fl_nv = nv; g_fl_first = n0;
        memcpy(g_fl_rx, rx, sizeof rx); memcpy(g_fl_ry, ry, sizeof ry);
        /* lines, clipped to the ladder's window by the pixel clip */
        for (int j = 0; j < 5; j++)
            for (int q = 0; q < sg[j].ns; q++) {
                const int a = sg[j].s0[q], b = sg[j].s1[q];
                f_line(out, N, ox0 + px + rx[a], oy0 + py + ry[a], ox0 + px + rx[b], oy0 + py + ry[b], colour,
                       ox0, oy0, ox0 + xmax, oy0 + ymax);
            }
        /* labels: both ends of each rung. The offsets are the original's tables (trunc(-3.5 + 6 cos), ...)
         * without the truncation, at the exact roll. */
        const int lx = MSW(d, 0x2940), ly = MSW(d, 0x293E);
        g_item = HI_LADDER_LABEL;
        for (int j = 0; j < 5; j++) {
            const int n = n0 + j, idx = n + 11;
            if (idx < 0 || idx > 22) continue;
            uint8_t text[12];
            pW(d, 0x2C8C, MW(d, 0x2C30 + 4 * idx));
            pW(d, 0x2C8E, MW(d, 0x2C32 + 4 * idx));
            const int nt = text_from(d, 0x2C8C, text);
            for (int end = 0; end < 2; end++) {
                const int vi = va[j][end];
                /* the first end's offsets come from the tables at the opposite angle (-cos, -sin) */
                const double bxb = idx < 11 ? -5.5 : -3.5;
                const double offx = end == 0 ? bxb - 6.0 * c - 1.0 : bxb + 6.0 * c + 2.0;
                /* the vertical offset's table is chosen by the vertex index (a quirk the original has), but
                 * both tables hold the same values */
                const double offy = end == 0 ? -1.5 + 6.0 * s : -1.5 - 6.0 * s;
                const double ax = rx[vi] + offx + lx, ay = ry[vi] + offy + ly;
                f_text(out, N, d, 0x2878, 7, ax, ay, text, nt);
            }
        }
    }
    return out->overflow ? -1 : 0;
}

/* ---- the panel pass ----------------------------------------------------------------------------------------- */
static double arc(double a, double b, double w);

static void panel_derive(hud_panel *p)
{
    p->c_fpm = p->fpm; p->c_vs = p->vs; p->c_cmd = p->cmd; p->c_speed = p->raw / 27.0;
}

/* the length of the record's own words (0 when it is not a 'p' record) */
int hud_panel_base_len(const int32_t *v, int n)
{
    if (n < HUD_PANEL_N || v[0] != HUD_PANEL_VERSION) return 0;
    if (n <= HUD_PANEL_N || v[HUD_PANEL_N] != HUD_PANEL_EXT_MAGIC) return HUD_PANEL_N;
    int at = HUD_PANEL_N + 3;
    if (at >= n) return 0;
    const int pieces = v[at++];
    for (int k = 0; k < pieces; k++) {
        if (at + 2 > n || v[at + 1] < 0) return 0;
        at += 2 + v[at + 1];
        if (at > n) return 0;
    }
    return at;
}

int hud_panel_load_ds(const int32_t *v, int n, hud_panel *p, uint8_t *dsout)
{
    const int base = hud_panel_base_len(v, n);
    if (!base) return 0;
    memset(p, 0, sizeof *p);
    p->fpm = s16(v[1]); p->vs = s16(v[2]); p->cmd = v[3] & 0xFFFF; p->raw = v[4] & 0xFFFF; p->speed = v[5] & 0xFFFF;
    p->mode = v[6]; p->half = v[7];
    panel_derive(p);
    if (base > HUD_PANEL_N) {
        static F117_TLS uint8_t scratch[0x10000];
        uint8_t *ds = dsout ? dsout : scratch;
        memset(ds, 0, 0x10000);
        p->ext = 1;
        p->far40 = v[HUD_PANEL_N + 1] & 0xFFFF;
        p->far42 = v[HUD_PANEL_N + 2] & 0xFFFF;
        int at = HUD_PANEL_N + 4;
        for (int k = 0; k < v[HUD_PANEL_N + 3]; k++) {
            const int off = v[at] & 0xFFFF, len = v[at + 1];
            for (int b = 0; b < len; b++) ds[(off + b) & 0xFFFF] = (uint8_t)v[at + 2 + b];
            at += 2 + len;
        }
        mem_t m = { ds };
        p->c_hdg = MW(&m, 0x2DEE);
        p->c_x = MW(&m, 0xC0D0);
        p->c_y = MW(&m, 0xC0DE);
        if (n >= base + 1 + HUD_PANEL_NFINE && v[base] == HUD_PANEL_FINE2_MAGIC) {
            p->c_fpm = v[base + 1] / 256.0;
            p->c_vs = v[base + 2] / 256.0;
            p->c_cmd = v[base + 3] / 256.0;
            p->c_speed = v[base + 4] / 1024.0;
            p->c_hdg = v[base + 5] / 256.0;
            p->c_x = v[base + 6] / 256.0;
            p->c_y = v[base + 7] / 256.0;
        }
    } else if (n >= HUD_PANEL_REC_FINE && v[HUD_PANEL_N] == HUD_PANEL_FINE_MAGIC) {
        p->c_fpm = v[HUD_PANEL_N + 1] / 256.0;
        p->c_vs = v[HUD_PANEL_N + 2] / 256.0;
        p->c_cmd = v[HUD_PANEL_N + 3] / 256.0;
        p->c_speed = v[HUD_PANEL_N + 4] / 1024.0;
    }
    return 1;
}

int hud_panel_load(const int32_t *v, int n, hud_panel *p) { return hud_panel_load_ds(v, n, p, NULL); }

int hud_panel_load_drawn(const int32_t *v, int n, hud_panel *p, uint8_t *ds)
{
    const int base = hud_panel_base_len(v, n);
    if (!base) return 0;
    const int hold = base + 1 + HUD_PANEL_NFINE;
    if (base > HUD_PANEL_N && n >= hold + 2 && v[base] == HUD_PANEL_FINE2_MAGIC && v[hold] == HUD_HOLD_MAGIC &&
        v[hold + 1] > 0 && hold + 2 + v[hold + 1] <= n) {
        if (!hud_panel_load_ds(v + hold + 2, v[hold + 1], p, ds)) return 0;
        p->c_fpm = v[base + 1] / 256.0;
        p->c_vs = v[base + 2] / 256.0;
        p->c_cmd = v[base + 3] / 256.0;
        p->c_speed = v[base + 4] / 1024.0;
        p->c_hdg = v[base + 5] / 256.0;
        p->c_x = v[base + 6] / 256.0;
        p->c_y = v[base + 7] / 256.0;
        return 1;
    }
    return hud_panel_load_ds(v, n, p, ds);
}

void hud_panel_from_ds(const uint8_t *ds, hud_panel *p)
{
    mem_t m = { (uint8_t *)ds };
    memset(p, 0, sizeof *p);
    p->fpm = MSW(&m, 0x98F6); p->vs = MSW(&m, 0xE586); p->cmd = MW(&m, 0xE474); p->raw = MW(&m, 0xB07E);
    p->speed = MW(&m, 0xB194); p->mode = MB(&m, 0x294A); p->half = MB(&m, 0x294B);
    panel_derive(p);
    p->ext = 1;
    p->c_hdg = MW(&m, 0x2DEE); p->c_x = MW(&m, 0xC0D0); p->c_y = MW(&m, 0xC0DE);
}

static int clamp3(int v, int lo, int hi)
{
    v = s16(v);
    if (hi >= v) {
        if (lo <= v) return v;
        return v <= s16(0xC000) ? hi : lo;
    }
    return hi;
}

static double dclamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

int hud_panel_classify(const hud_panel *p, char kind, const int32_t *v, int n, hud_panel_item *out)
{
    memset(out, 0, sizeof *out);
    if (kind == 'S') {
        /* entry 71, the 11 x 7 marker from (55, 77) to (154, row - 4), row = 0x34 + ([98F6] >> 6) */
        if (n < 14 || v[0] != 71) return HPI_NONE;
        const int32_t *b = v + 2;
        if (s16(b[1]) != 55 || s16(b[2]) != 77 || s16(b[4]) != 154 || s16(b[6]) != 11 || s16(b[7]) != 7) return HPI_NONE;
        if (s16(b[5]) != 0x34 + (p->fpm >> 6) - 4) return HPI_NONE;
        out->kind = HPI_FPM;
        out->x0 = 154.0;
        out->y0 = 52.0 + p->c_fpm / 64.0 - 4.0;
        return HPI_FPM;
    }
    if (kind != 'N' || n < 7 || v[6] != 0) return HPI_NONE;
    const int x0 = s16(v[0]), y0 = s16(v[1]), x1 = s16(v[2]), y1 = s16(v[3]);
    if (x0 == 247 && x1 == 247 && y0 == 0x34) {
        /* the vertical-speed bar: to row clamp3(0x34 - ([E586] >> 4), 0x14, 0x51) */
        if (y1 != clamp3(0x34 - (p->vs >> 4), 0x14, 0x51)) return HPI_NONE;
        out->kind = HPI_VS;
        out->x0 = 247; out->y0 = 52;
        out->x1 = 247; out->y1 = dclamp(52.0 - p->c_vs / 16.0, 20.0, 81.0);
        return HPI_VS;
    }
    if (x0 == 72 && x1 == 72 && y1 == 0x51 && y0 != 0x51) {
        /* the stall-margin bar: 0x51 - n up to 0x51, n = clamp3(((command - speed) * 2) / 5 + 0x1D, 0, 0x39); the divide truncates */
        const int q = (s16(p->cmd - p->speed) * 2) / 5;
        const int nn = clamp3(q + 0x1D, 0, 0x39);
        if (nn == 0 || y0 != 0x51 - nn) return HPI_NONE;
        const double nf = dclamp((((p->c_cmd - p->c_speed) * 2.0) / 5.0) + 29.0, 0.0, 57.0);
        out->kind = HPI_STALL;
        out->x0 = 72; out->y0 = 81.0 - nf;
        out->x1 = 72; out->y1 = 81;
        return HPI_STALL;
    }
    return HPI_NONE;
}

int hud_panel_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int hold, int32_t *out, int cap)
{
    const int bs = hud_panel_base_len(s, ns), bo = hud_panel_base_len(o, no);
    if (!bs || !bo) return 0;
    const int same = s[6] == o[6] && s[7] == o[7];          /* the HUD mode and its size: else no common picture */
    if (bs == HUD_PANEL_N || bo == HUD_PANEL_N) {           /* a record without the pass's inputs: the first version's blend */
        if (!same || cap < HUD_PANEL_REC_FINE) return 0;
        if (fabs(w) < 1e-6) { memcpy(out, s, HUD_PANEL_N * sizeof *out); return HUD_PANEL_N; }
        hud_panel a, b;
        if (!hud_panel_load(s, ns, &a) || !hud_panel_load(o, no, &b)) return 0;
        const double fpm = a.c_fpm + w * (b.c_fpm - a.c_fpm), vs = a.c_vs + w * (b.c_vs - a.c_vs);
        const double cmd = a.c_cmd + w * (b.c_cmd - a.c_cmd), sp = a.c_speed + w * (b.c_speed - a.c_speed);
        memcpy(out, s, HUD_PANEL_N * sizeof *out);
        out[HUD_PANEL_N] = HUD_PANEL_FINE_MAGIC;
        out[HUD_PANEL_N + 1] = (int32_t)floor(fpm * 256.0 + 0.5);
        out[HUD_PANEL_N + 2] = (int32_t)floor(vs * 256.0 + 0.5);
        out[HUD_PANEL_N + 3] = (int32_t)floor(cmd * 256.0 + 0.5);
        out[HUD_PANEL_N + 4] = (int32_t)floor((sp < 0 ? 0 : sp) * 1024.0 + 0.5);
        return HUD_PANEL_REC_FINE;
    }
    if (!same && !hold) return 0;
    const int need = bs + 1 + HUD_PANEL_NFINE + (hold == 1 ? 2 + bo : 0);
    if (cap < need) return 0;
    if (fabs(w) < 1e-6 && hold != 1) { memcpy(out, s, (size_t)bs * sizeof *out); return bs; }
    static F117_TLS hud_panel a, b;
    if (!hud_panel_load(s, ns, &a) || !hud_panel_load(o, no, &b)) return 0;
    double c[HUD_PANEL_NFINE];
    const hud_panel *keep = hold == 1 ? &b : &a;
    if (same) {
        c[0] = a.c_fpm + w * (b.c_fpm - a.c_fpm); c[1] = a.c_vs + w * (b.c_vs - a.c_vs);
        c[2] = a.c_cmd + w * (b.c_cmd - a.c_cmd); c[3] = a.c_speed + w * (b.c_speed - a.c_speed);
        c[4] = arc(a.c_hdg, b.c_hdg, w); c[5] = a.c_x + w * (b.c_x - a.c_x); c[6] = a.c_y + w * (b.c_y - a.c_y);
    } else {
        c[0] = keep->c_fpm; c[1] = keep->c_vs; c[2] = keep->c_cmd; c[3] = keep->c_speed;
        c[4] = keep->c_hdg; c[5] = keep->c_x; c[6] = keep->c_y;
    }
    if (c[3] < 0) c[3] = 0;
    c[4] = fmod(c[4], 65536.0); if (c[4] < 0) c[4] += 65536.0;
    memcpy(out, s, (size_t)bs * sizeof *out);
    int n = bs;
    out[n++] = HUD_PANEL_FINE2_MAGIC;
    out[n++] = (int32_t)floor(c[0] * 256.0 + 0.5);
    out[n++] = (int32_t)floor(c[1] * 256.0 + 0.5);
    out[n++] = (int32_t)floor(c[2] * 256.0 + 0.5);
    out[n++] = (int32_t)floor(c[3] * 1024.0 + 0.5);
    out[n++] = (int32_t)floor(c[4] * 256.0 + 0.5);
    out[n++] = (int32_t)floor(c[5] * 256.0 + 0.5);
    out[n++] = (int32_t)floor(c[6] * 256.0 + 0.5);
    if (hold == 1) {
        out[n++] = HUD_HOLD_MAGIC;
        out[n++] = bo;
        memcpy(out + n, o, (size_t)bo * sizeof *out);
        n += bo;
    }
    return n;
}

/* ---- the panel pass: EXACT and FINE ----------------------------------------------------------------------------- */
static int abs16(int v) { v = s16(v); return s16(v < 0 ? -v : v); }

/* 0000:C702 */
int hud_bearing(int x, int y)
{
    x = s16(x); y = s16(y);
    if (x == 0) return y > 0 ? 0 : 0x8000;
    if (y == 0) return x > 0 ? 0x4000 : 0xC000;
    const int ay = abs16(y), ax = abs16(x);
    int flag;
    int32_t num, den;
    if (ax > ay) { num = (int32_t)((uint32_t)(int32_t)ay << 14); den = ax; flag = 1; }
    else { num = (int32_t)((uint32_t)(int32_t)ax << 14); den = ay; flag = 0; }
    const int r = s16(num / den);
    const int t = abs16(0x1333 - r);
    int32_t q = (int32_t)t * 0xB00;
    q >>= 14;
    const int32_t mm = 0x2800 - q;
    int32_t pr = (int32_t)((uint32_t)(int32_t)r * (uint32_t)mm);
    pr >>= 14;
    const int rr = (int)(pr & 0xFFFF);
    int res;
    if (x > 0) res = y > 0 ? (flag ? 0x4000 - rr : rr) : (flag ? rr + 0x4000 : 0x8000 - rr);
    else res = y > 0 ? (flag ? rr - 0x4000 : -rr) : (flag ? 0xC000 - rr : rr + 0x8000);
    return res & 0xFFFF;
}

/* the same arithmetic without its truncations (the original's octant polynomial, not the true arctangent) */
double hud_bearing_f(double x, double y)
{
    if (x == 0) return y > 0 ? 0 : 32768.0;
    if (y == 0) return x > 0 ? 16384.0 : 49152.0;
    const double ax = fabs(x), ay = fabs(y);
    const int flag = ax > ay;
    const double r = 16384.0 * (flag ? ay / ax : ax / ay);
    const double rr = r * (10240.0 - fabs(4915.0 - r) * 2816.0 / 16384.0) / 16384.0;
    double res;
    if (x > 0) res = y > 0 ? (flag ? 16384.0 - rr : rr) : (flag ? rr + 16384.0 : 32768.0 - rr);
    else res = y > 0 ? (flag ? rr - 16384.0 : -rr) : (flag ? 49152.0 - rr : rr + 32768.0);
    res = fmod(res, 65536.0);
    return res < 0 ? res + 65536.0 : res;
}

/* 0000:C6B3: the larger of |x| and |y| plus half the smaller, at most 7FFFh */
static int dist_i(int x, int y)
{
    const int a = abs16(x), b = abs16(y);
    const int32_t r = b >= a ? (int32_t)b + (a >> 1) : (int32_t)a + (b >> 1);
    if (r < 0) return s16(r & 0xFFFF);
    return r > 0x7FFF ? 0x7FFF : r;
}

/* the C runtime's itoa / ltoa in radix 10 */
static int put_dec(uint8_t *out, int32_t v)
{
    char tmp[16];
    int n = 0, len = 0;
    const int neg = v < 0;
    uint32_t u = neg ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) out[len++] = '-';
    while (n) out[len++] = (uint8_t)tmp[--n];
    out[len] = 0;
    return len;
}

static int str_len(const mem_t *m, int at) { int n = 0; while (n < 255 && MB(m, at + n)) n++; return n; }
static void str_cpy(mem_t *m, int to, int from)
{
    int k = 0;
    uint8_t c;
    do { c = MB(m, from + k); pB(m, to + k, c); k++; } while (c && k < 256);
}
static void str_cat(mem_t *m, int to, int from) { str_cpy(m, to + str_len(m, to), from); }
static void str_cat_bytes(mem_t *m, int to, const uint8_t *s)
{
    const int at = to + str_len(m, to);
    int k = 0;
    do { pB(m, at + k, s[k]); } while (s[k++]);
}
static void str_upr(mem_t *m, int at)
{
    for (int k = 0; k < 255 && MB(m, at + k); k++) {
        const int c = MB(m, at + k);
        if (c >= 'a' && c <= 'z') pB(m, at + k, c - 32);
    }
}

typedef struct { hud_exact_list *ex; hud_fine_list *fl; int N; mem_t *d; const hud_panel *p; } pctx;

/* pen 886A: the colour into both text blocks */
static void p_pen(pctx *c, int colour)
{
    pW(c->d, MW(c->d, 0x4010) + 4, colour);
    pW(c->d, MW(c->d, 0x4028) + 4, colour);
}

/* 895E / 898F -> 89C0: the string at strp, upper-cased in place (EDC2), through entry 5 with the block [40B6] chooses */
static void p_text(pctx *c, int strp, int x, int y, int colour)
{
    mem_t *d = c->d;
    const int blk = MB(d, 0x40B6) ? MW(d, 0x4028) : MW(d, 0x4010);
    pW(d, blk + 0xC, 0);
    pW(d, blk + 8, x);
    pW(d, blk + 0xA, y);
    pW(d, blk + 4, colour);
    str_upr(d, strp);
    x_t(c->ex, d, 5, blk, strp, 0);
    if (c->fl) {
        uint8_t text[HUD_TEXT_MAX + 1];
        int nt = 0;
        while (nt < HUD_TEXT_MAX && MB(d, strp + nt)) { text[nt] = MB(d, strp + nt); nt++; }
        f_text(c->fl, c->N, d, blk, 0, s16(x), s16(y), text, nt);
    }
}

/* 114A:0006: a line clipped to block [4010]'s rectangle, in its colour (entry 33, then the clipper and entry 31);
 * the fine stroke runs between (fx0, fy0) and (fx1, fy1) */
static void p_line(pctx *c, int x0, int y0, int x1, int y1, double fx0, double fy0, double fx1, double fy1)
{
    mem_t *d = c->d;
    const int b = MW(d, 0x4010);
    const int w = MSW(d, b + 0x14) - MSW(d, b + 0x12) + 1, h = MSW(d, b + 0x10) - MSW(d, b + 0xE) + 1;
    const int colour = MW(d, b + 4) & 0xFF;
    x_k(c->ex, colour);
    const int origin = (MSW(d, b + 0xE) * 320 + MSW(d, b + 0x12)) & 0xFFFF;
    int16_t o[4];
    if (hud_clip_line(x0, y0, x1, y1, w - 1, h - 1, o)) x_n(c->ex, o[0], o[1], o[2], o[3], origin, 0);
    if (c->fl) {
        const int ox = MSW(d, b + 0x12), oy = MSW(d, b + 0xE);
        f_line(c->fl, c->N, fx0 + ox, fy0 + oy, fx1 + ox, fy1 + oy, colour, ox, oy, ox + w - 1, oy + h - 1);
    }
}

/* 114A:014F: a rectangle round a readout, four lines through 114A:0070 (box 0..13Fh x 0..C7h) */
static void p_outline(pctx *c, int x0, int y0, int x1, int y1, int colour)
{
    mem_t *d = c->d;
    p_pen(c, colour);
    const int e[4][4] = { { x0, y0, x1, y0 }, { x0, y1, x1, y1 }, { x0, y0, x0, y1 }, { x1, y0, x1, y1 } };
    for (int k = 0; k < 4; k++) {
        x_k(c->ex, MW(d, MW(d, 0x4010) + 4) & 0xFF);
        int16_t o[4];
        if (hud_clip_line(e[k][0], e[k][1], e[k][2], e[k][3], 0x13F, 0xC7, o)) x_n(c->ex, o[0], o[1], o[2], o[3], 0, 0);
        if (c->fl) f_line(c->fl, c->N, e[k][0], e[k][1], e[k][2], e[k][3], colour & 0xFF, 0, 0, 319, 199);
    }
}

/* 89F7: a number at (x, y); 8A18: a long, right-aligned at x */
static void p_number(pctx *c, int32_t value, int x, int y, int colour, int right)
{
    uint8_t s[16];
    const int len = put_dec(s, value);
    for (int k = 0; k <= len; k++) pB(c->d, 0xC604 + k, s[k]);
    p_text(c, 0xC604, right ? x - 4 * len : x, y, colour);
}

/* the waypoint cue's position: the bearing to the waypoint less the heading, scaled and clamped (exact and fine) */
static void cue_x(const mem_t *d, const hud_panel *pn, int dx, int dy, double fdx, double fdy, int div, int lo, int hi, int *X, double *fX)
{
    const int brg = hud_bearing(dx, s16(-dy));
    *X = clamp3(s16(s16(brg - MW(d, 0x2DEE)) >> 6) / div + 0x9F, lo, hi);
    double dd = fmod(hud_bearing_f(fdx, -fdy) - pn->c_hdg, 65536.0);
    if (dd >= 32768.0) dd -= 65536.0;
    if (dd < -32768.0) dd += 65536.0;
    *fX = dclamp(dd / 64.0 / div + 159.0, lo, hi);
}

int hud_panel_exact(const uint8_t *ds0, const hud_panel *pn, int N, hud_exact_list *ex, hud_fine_list *fl)
{
    static F117_TLS uint8_t buf[0x10000];
    static F117_TLS hud_exact_list scratch;
    if (!ex) ex = &scratch;
    ex->n = 0;
    if (fl) { fl->n = 0; fl->overflow = 0; }
    g_overflow = 0;
    if (!pn->ext) return -1;
    memcpy(buf, ds0, sizeof buf);
    mem_t m = { buf }, *d = &m;
    pctx c = { ex, fl, N, d, pn };
    const int half = MB(d, 0x294B), mode = MB(d, 0x294A);
    const int pc = s8(MB(d, 0x2CA0)) & 0xFFFF, lc = s8(MB(d, 0x2CA1)) & 0xFFFF;
    const int alt = MW(d, 0x2DF6), alt5 = alt < 100 ? alt : (alt / 5) * 5;
    if (MW(d, 0x368C) == 0 || MW(d, 0xC0A6) != 0) goto message;
    /* the selected station: "<count> <name>" */
    g_item = PI_STATION;
    {
        const int st = MW(d, 0x3680);
        uint8_t num[16];
        str_cpy(d, 0x98A6, 0x40CE);
        put_dec(num, MSW(d, 0x3672 + 4 * st));
        str_cat_bytes(d, 0x98A6, num);
        str_cat(d, 0x98A6, 0x40CF);
        str_cat(d, 0x98A6, 0x369A + 26 * MW(d, 0x3670 + 4 * st));
        p_text(&c, 0x98A6, half == 0 ? 0x35 : 0x6A, 0x55, pc);
    }
    /* the cannon: "GUN <rounds>", unless the settings record hides it */
    g_item = PI_GUN;
    if (pn->far42 == 0) {
        uint8_t num[16];
        str_cpy(d, 0x98A6, 0x40D1);
        put_dec(num, MSW(d, 0x3682));
        str_cat_bytes(d, 0x98A6, num);
        p_text(&c, 0x98A6, half == 0 ? 0xEF : 0xBC, 0x55, pc);
    }
    /* the flight-path marker (entry 71 from page [E584] (37h, 4Dh), 11 x 7) */
    g_item = PI_FPM;
    if (MW(d, 0xE32A) == 0 || MW(d, 0xE32A) == 2) {
        p_pen(&c, lc);
        const int row = s16((half == 0 ? 0x34 : 0x3C) + (MSW(d, 0x98F6) >> 6));
        if (half == 0 && row > 10 && row < 0x5D) {
            const int blk[8] = { MW(d, 0xE584), 0x37, 0x4D, MB(d, 0x40B6) ? 1 : 0, 0x9A, s16(row - 4) & 0xFFFF, 0xB, 7 };
            hud_prim *q = prim_new(ex, HP_S);
            if (q) {
                q->entry = 71;
                for (int k = 0; k < 8; k++) q->blk[k] = (uint16_t)blk[k];
            }
            if (fl) {
                hud_fprim *f = fine_new(fl);
                if (f) {
                    f->type = HF_SPRITE;
                    f->entry = 71;
                    for (int k = 0; k < 8; k++) f->blk[k] = (uint16_t)blk[k];
                    f->x0 = 0x9A * N;
                    f->y0 = (0x34 + pn->c_fpm / 64.0 - 4.0) * N;
                    /* the clip of entry 71: rows [40A0]..[40A2], columns [40A4]..[40A6] */
                    f->ct = MSW(d, 0x40A0) * N; f->cb = (MSW(d, 0x40A2) + 1) * N;
                    f->cl = MSW(d, 0x40A4) * N; f->cr = (MSW(d, 0x40A6) + 1) * N;
                }
            }
        }
    }
    if (half) goto small;
    /* the tracking cross */
    g_item = PI_CROSS;
    if (MB(d, 0x9B35) & 2) {
        const int on = MW(d, 0xB2B8) == 0 ? 1 : (MB(d, 0x3D8E) & 1);
        if (on) {
            p_pen(&c, pc);
            p_line(&c, 0x9C, 0x55, 0xA4, 0x55, 0x9C, 0x55, 0xA4, 0x55);
            p_line(&c, 0xA0, 0x52, 0xA0, 0x58, 0xA0, 0x52, 0xA0, 0x58);
        }
    }
    if (mode == 0) {
        /* the stall-margin bar and the vertical-speed bar */
        g_item = PI_STALL;
        p_pen(&c, MW(d, 0x368A) >= 1 ? 0x2A : 0);
        const int nn = clamp3(s16(s16(s16(MW(d, 0xE474) - MW(d, 0xB194)) * 2) / 5) + 0x1D, 0, 0x39);
        if (nn != 0) {
            const double nf = dclamp((((pn->c_cmd - pn->c_speed) * 2.0) / 5.0) + 29.0, 0.0, 57.0);
            p_line(&c, 0x48, 0x51 - nn, 0x48, 0x51, 0x48, 81.0 - nf, 0x48, 0x51);
        }
        g_item = PI_VS;
        const int y = clamp3(0x34 - (MSW(d, 0xE586) >> 4), 0x14, 0x51);
        p_line(&c, 0xF7, 0x34, 0xF7, y, 0xF7, 0x34, 0xF7, dclamp(52.0 - pn->c_vs / 16.0, 20.0, 81.0));
        /* the sink marker, blinking */
        g_item = PI_SINK;
        if (!(MB(d, 0x9B34) & 1) && (MB(d, 0x3D8E) & 1) && pn->far40 != 0 && MSW(d, 0xE586) < 0) {
            const int q = (MB(d, 0xB2D0 + 16 * MW(d, 0xE00C) + 7) & 2) ? 0x100 : 0x80;
            const int sy = ((q / pn->far40) >> 4) + 0x34;
            p_pen(&c, pc);
            p_line(&c, 0xF2, sy - 2, 0xF4, sy, 0xF2, sy - 2, 0xF4, sy);
            p_line(&c, 0xF2, sy + 2, 0xF4, sy, 0xF2, sy + 2, 0xF4, sy);
        }
    }
    /* (the ILS and, with an air target, its box and the envelope ring are not modelled: other calls come here) */
    /* the digital readouts */
    g_item = PI_SPEED;
    p_number(&c, MW(d, 0xB194), mode == 0 ? 0x50 : 0x3A, 0x32, pc, 0);
    g_item = PI_ALT;
    p_number(&c, alt5, mode == 0 ? 0xF0 : 0x108, 0x32, pc, 1);
    if (mode != 0) {
        if (mode == 2) {
            g_item = PI_HDGNUM;
            p_number(&c, MW(d, 0x2DEE) / 0xB6, 0x9A, 8, pc, 0);
            g_item = PI_BOX;
            p_outline(&c, 0x97, 5, 0xA7, 0xF, pc);
        }
        g_item = PI_BOX;
        p_outline(&c, 0x37, 0x2F, 0x4F, 0x39, pc);
        p_outline(&c, 0xF1, 0x2F, 0x109, 0x39, pc);
    }
    g_item = PI_ACCEL;
    if (MSW(d, 0x3DA8) > 1) p_text(&c, 0x40D6, 0x6E, 0x55, pc);
    g_item = PI_TRAINING;
    if (MB(d, 0x9B35) & 0x10) p_text(&c, 0x40DC, 0xB6, 0x55, pc);
    /* the waypoint cue on the heading strip (and the next waypoint once this one is reached) */
    g_item = PI_CUE;
    if (!(MB(d, 0x3664) & 8)) {
        const int wp = MW(d, 0x2EAE);
        const int dx = s16(MW(d, 0x2E9E + 4 * wp) - MW(d, 0xC0D0)), dy = s16(MW(d, 0x2EA0 + 4 * wp) - MW(d, 0xC0DE));
        const double fdx = MW(d, 0x2E9E + 4 * wp) - pn->c_x, fdy = MW(d, 0x2EA0 + 4 * wp) - pn->c_y;
        if (dist_i(dx, dy) < 0x200 && MSW(d, 0x2EAE) < 3) {
            pW(d, 0x2EAE, MW(d, 0x2EAE) + 1);
            uint8_t num[16];
            str_cpy(d, 0x98A6, 0x40E5);
            put_dec(num, MSW(d, 0x2EAE));
            str_cat_bytes(d, 0x98A6, num);
            str_cat(d, 0x98A6, 0x40EF);
            str_cpy(d, 0xC5A4, 0x98A6);
            pW(d, 0x40CC, MW(d, 0x368E) * 3);
        }
        int X;
        double fX;
        cue_x(d, pn, dx, dy, fdx, fdy, 3, 0x59, 0xE5, &X, &fX);
        p_pen(&c, 0xB);
        if (mode == 2) {
            p_line(&c, X, 0x11, X, 0x11, fX, 0x11, fX, 0x11);
            p_line(&c, X - 1, 0x12, X + 1, 0x12, fX - 1, 0x12, fX + 1, 0x12);
        } else {
            p_line(&c, X - 2, 0xB, X, 0xD, fX - 2, 0xB, fX, 0xD);
            p_line(&c, X, 0xD, X + 2, 0xB, fX, 0xD, fX + 2, 0xB);
            p_line(&c, X - 2, 0xB, X + 2, 0xB, fX - 2, 0xB, fX + 2, 0xB);
        }
    }
    goto message;
small:
    g_item = PI_CUE;
    if (!(MB(d, 0x3664) & 8)) {
        const int wp = MW(d, 0x2EAE);
        const int dx = s16(MW(d, 0x2E9E + 4 * wp) - MW(d, 0xC0D0)), dy = s16(MW(d, 0x2EA0 + 4 * wp) - MW(d, 0xC0DE));
        const double fdx = MW(d, 0x2E9E + 4 * wp) - pn->c_x, fdy = MW(d, 0x2EA0 + 4 * wp) - pn->c_y;
        int X;
        double fX;
        cue_x(d, pn, dx, dy, fdx, fdy, 7, 0x82, 0xBC, &X, &fX);
        p_pen(&c, 0xB);
        p_line(&c, X, 0x1B, X, 0x1B, fX, 0x1B, fX, 0x1B);
        p_line(&c, X - 1, 0x1C, X + 1, 0x1C, fX - 1, 0x1C, fX + 1, 0x1C);
    }
    if (mode != 0) {
        g_item = PI_SPEED;
        p_number(&c, MW(d, 0xB194), 0x67, 0x3A, pc, 0);
        g_item = PI_ALT;
        p_number(&c, alt5, 0xDA, 0x3A, pc, 1);
        if (mode == 2) {
            g_item = PI_HDGNUM;
            p_number(&c, MW(d, 0x2DEE) / 0xB6, 0x9A, 0x14, pc, 0);
        }
    }
message:
    /* the message line, centred, while its count lasts */
    g_item = PI_MESSAGE;
    if (MW(d, 0x40CC) != 0 && MW(d, 0xC0A6) == 0 && MB(d, 0x294B) == 0) {
        const int len = str_len(d, 0xC5A4);
        p_text(&c, 0xC5A4, s16(0xA0 - 2 * (len & 0xFE)), 0x14, pc);
        pW(d, 0x40CC, MW(d, 0x40CC) - 1);
    }
    if (g_overflow || (fl && fl->overflow)) return -1;
    return 0;
}

/* ---- the moving map's aircraft marker -------------------------------------------------------------------------- */
static void map_parse(const int32_t *v, hud_map *m)
{
    memset(m, 0, sizeof *m);
    m->x0 = v[1] & 0xFFFF; m->y0 = v[2] & 0xFFFF;
    m->px = (int32_t)((uint32_t)(v[3] & 0xFFFF) | ((uint32_t)(v[4] & 0xFFFF) << 16));
    m->py = (int32_t)((uint32_t)(v[5] & 0xFFFF) | ((uint32_t)(v[6] & 0xFFFF) << 16));
    m->ox = v[7] & 0xFFFF; m->oy = v[8] & 0xFFFF; m->zoom = v[9] & 0xFF; m->hdg = v[10] & 0xFFFF;
    m->sx_screen = s16(v[11]); m->sy_screen = s16(v[12]); m->src_x = v[13];
    for (int k = 0; k < 16; k++) m->art[k] = (uint8_t)v[14 + k];
    m->c_x = hud_map_fine_x(m);
    m->c_y = hud_map_fine_y(m);
}

/* [C0D0] is [BA3C] / 32 and [C0DE] is 8000h - [C056] / 32, each rounded to a whole unit (test_hud_logs checks it): the
 * continuous position is the same without the rounding */
double hud_map_fine_x(const hud_map *m) { return m->px / 32.0; }
double hud_map_fine_y(const hud_map *m) { return 32768.0 - m->py / 32.0; }

int hud_map_load(const int32_t *v, int n, hud_map *m)
{
    if (n < HUD_MAP_N || v[0] != HUD_MAP_VERSION) return 0;
    map_parse(v, m);
    if (n >= HUD_MAP_N + 3 && v[HUD_MAP_N] == HUD_MAP_FINE_MAGIC) {
        m->c_x = v[HUD_MAP_N + 1] / 256.0;
        m->c_y = v[HUD_MAP_N + 2] / 256.0;
    }
    return 1;
}

int hud_map_load_drawn(const int32_t *v, int n, hud_map *m)
{
    if (!hud_map_load(v, n, m)) return 0;
    const int h = HUD_MAP_N + 3;
    if (n >= h + 2 + HUD_MAP_N && v[HUD_MAP_N] == HUD_MAP_FINE_MAGIC && v[h] == HUD_HOLD_MAGIC && v[h + 1] == HUD_MAP_N) {
        hud_map old;
        map_parse(v + h + 2, &old);
        memcpy(m->art, old.art, sizeof m->art);       /* the older frame's arrow, at the position of the moment */
        m->src_x = old.src_x;
        m->hdg = old.hdg;
    }
    return 1;
}

int hud_map_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int hold, int32_t *out, int cap)
{
    if (ns < HUD_MAP_N || no < HUD_MAP_N || s[0] != HUD_MAP_VERSION || o[0] != HUD_MAP_VERSION) return 0;
    if (cap < HUD_MAP_MAX) return 0;
    hud_map a, b;
    hud_map_load(s, ns, &a);
    hud_map_load(o, no, &b);
    /* the map's origin and zoom must agree, or the arrow stays where the skeleton's map has it */
    const int same = a.ox == b.ox && a.oy == b.oy && a.zoom == b.zoom;
    if (fabs(w) < 1e-6 && hold != 1) { memcpy(out, s, HUD_MAP_N * sizeof *out); return HUD_MAP_N; }
    memcpy(out, s, HUD_MAP_N * sizeof *out);
    int n = HUD_MAP_N;
    out[n++] = HUD_MAP_FINE_MAGIC;
    out[n++] = (int32_t)floor((same ? a.c_x + w * (b.c_x - a.c_x) : a.c_x) * 256.0 + 0.5);
    out[n++] = (int32_t)floor((same ? a.c_y + w * (b.c_y - a.c_y) : a.c_y) * 256.0 + 0.5);
    if (hold == 1) {
        out[n++] = HUD_HOLD_MAGIC;
        out[n++] = HUD_MAP_N;
        memcpy(out + n, o, HUD_MAP_N * sizeof *out);
        n += HUD_MAP_N;
    }
    return n;
}

int hud_map_classify(const hud_map *m, const int32_t *v, int n, double *x, double *y)
{
    if (n < 14 + 16 || v[0] != 73) return 0;
    const int32_t *b = v + 2;
    if (s16(b[1]) != m->src_x || s16(b[2]) != 4 || s16(b[6]) != 4 || s16(b[7]) != 4) return 0;
    if (s16(b[4]) != s16(m->sx_screen - 1) || s16(b[5]) != s16(m->sy_screen - 1)) return 0;
    /* the position the record's words give, the original's way: then the continuous one is the same formula */
    const int sh = 10 - m->zoom;
    if (sh < 0 || sh > 15) return 0;
    if (s16((s16(m->x0 - m->ox) >> sh) + 0x70) != m->sx_screen) return 0;
    if (s16((s16((s16(m->y0 - m->oy) >> sh) * 3) >> 2) + 0x8B) != m->sy_screen) return 0;
    const double scale = (double)(1 << sh);
    *x = (m->c_x - m->ox) / scale + 0x70 - 1;
    *y = (m->c_y - m->oy) / scale * 0.75 + 0x8B - 1;
    return 1;
}

/* ---- interpolation -------------------------------------------------------------------------------- */
static int rec_word(const int32_t *v, int off)
{
    if (off >= HUD_DS_LO && off < HUD_DS_HI) return v[1 + (off - HUD_DS_LO) / 2];
    for (int k = 0; k < HUD_NEXTRA; k++) if (hud_extra_off[k] == off) return v[1 + HUD_NWORDS + k];
    return 0;
}

static void rec_continuous(const int32_t *v, int n, double c[5])
{
    if (n >= HUD_REC_FINE && v[HUD_REC_BASE] == HUD_FINE_MAGIC) {
        c[0] = v[HUD_REC_BASE + 1] / 1024.0;
        c[1] = v[HUD_REC_BASE + 2] / 256.0;
        c[2] = v[HUD_REC_BASE + 3] / 256.0;
        c[3] = v[HUD_REC_BASE + 4] / 256.0;
        c[4] = v[HUD_REC_BASE + 5] / 256.0;
        return;
    }
    c[0] = (rec_word(v, HUD_SPEED_RAW) & 0xFFFF) / 27.0;
    c[1] = (double)(rec_word(v, HUD_ALT) & 0xFFFF);
    c[2] = (double)(rec_word(v, HUD_HDG) & 0xFFFF);
    c[3] = (double)s16(rec_word(v, HUD_PITCH));
    c[4] = (double)(rec_word(v, HUD_ROLL) & 0xFFFF);
}

static double arc(double a, double b, double w)
{
    double dlt = fmod(b - a, 65536.0);
    if (dlt >= 32768.0) dlt -= 65536.0;
    if (dlt < -32768.0) dlt += 65536.0;
    return a + w * dlt;
}

int hud_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int hold, int32_t *out, int cap)
{
    if (ns < HUD_REC_BASE || no < HUD_REC_BASE || s[0] != HUD_REC_VERSION || o[0] != HUD_REC_VERSION) return 0;
    if (cap < (hold == 1 ? HUD_REC_HELD : HUD_REC_FINE)) return 0;
    /* the layout and the switches must agree: the constants 291C..294B, the readout flag, the colours,
     * the ladder flag */
    int same = 1;
    for (int off = 0x291C; off < 0x294C && same; off += 2) same = s[1 + (off - HUD_DS_LO) / 2] == o[1 + (off - HUD_DS_LO) / 2];
    static const int sw[] = { 0x2956, 0x2CA0, HUD_LADDER_ON };
    for (unsigned k = 0; k < sizeof sw / sizeof sw[0] && same; k++) same = rec_word(s, sw[k]) == rec_word(o, sw[k]);
    if (!same && !hold) return 0;
    if (fabs(w) < 1e-6 && hold != 1) {    /* a hair from a logic frame: that frame's own state, not its quantised copy */
        memcpy(out, s, HUD_REC_BASE * sizeof *out);
        return HUD_REC_BASE;
    }
    double cs[5], co[5], c[5];
    rec_continuous(s, ns, cs);
    rec_continuous(o, no, co);
    if (same) {
        c[0] = cs[0] + w * (co[0] - cs[0]); c[1] = cs[1] + w * (co[1] - cs[1]);
        c[2] = arc(cs[2], co[2], w); c[3] = arc(cs[3], co[3], w); c[4] = arc(cs[4], co[4], w);
    } else memcpy(c, hold == 1 ? co : cs, sizeof c);   /* no common picture: the older frame, unmoved */
    if (c[0] < 0) c[0] = 0;
    if (c[1] < 0) c[1] = 0;
    c[2] = fmod(c[2], 65536.0); if (c[2] < 0) c[2] += 65536.0;
    c[4] = fmod(c[4], 65536.0); if (c[4] < 0) c[4] += 65536.0;
    memcpy(out, s, HUD_REC_BASE * sizeof *out);
    out[HUD_REC_BASE] = HUD_FINE_MAGIC;
    out[HUD_REC_BASE + 1] = (int32_t)floor(c[0] * 1024.0 + 0.5);
    out[HUD_REC_BASE + 2] = (int32_t)floor(c[1] * 256.0 + 0.5);
    out[HUD_REC_BASE + 3] = (int32_t)floor(c[2] * 256.0 + 0.5);
    out[HUD_REC_BASE + 4] = (int32_t)floor(c[3] * 256.0 + 0.5);
    out[HUD_REC_BASE + 5] = (int32_t)floor(c[4] * 256.0 + 0.5);
    if (hold != 1) return HUD_REC_FINE;
    out[HUD_REC_FINE] = HUD_HOLD_MAGIC;
    out[HUD_REC_FINE + 1] = HUD_REC_BASE;
    memcpy(out + HUD_REC_FINE + 2, o, HUD_REC_BASE * sizeof *out);
    return HUD_REC_HELD;
}

int hud_state_load_drawn(const int32_t *v, int n, hud_state *st)
{
    if (n >= HUD_REC_HELD && v[HUD_REC_FINE] == HUD_HOLD_MAGIC && v[HUD_REC_FINE + 1] == HUD_REC_BASE) {
        if (!hud_state_load(v + HUD_REC_FINE + 2, HUD_REC_BASE, st)) return 0;
        if (v[HUD_REC_BASE] == HUD_FINE_MAGIC) {
            st->speed = v[HUD_REC_BASE + 1] / 1024.0;
            st->alt = v[HUD_REC_BASE + 2] / 256.0;
            st->hdg = v[HUD_REC_BASE + 3] / 256.0;
            st->pitch = v[HUD_REC_BASE + 4] / 256.0;
            st->roll = v[HUD_REC_BASE + 5] / 256.0;
            st->interpolated = 1;
        }
        return 1;
    }
    return hud_state_load(v, n, st);
}
