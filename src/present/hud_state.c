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

static hud_prim *prim_new(hud_exact_list *l, int kind)
{
    if (l->n >= HUD_MAX_EXACT) return NULL;
    hud_prim *p = &l->p[l->n++];
    memset(p, 0, sizeof *p);
    p->kind = (uint8_t)kind;
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
    while (len < 11 && MB(m, strp + len)) { p->text[len] = MB(m, strp + len); len++; }
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
    if (mode <= 1) {
        x_k(out, MB(d, 0x2CA0));
        if (half) {
            if (mode == 0) { x_n(out, 0x7A, 0x3C, 0x7C, 0x3C, 0, 1); x_n(out, 0xC4, 0x3C, 0xC6, 0x3C, 0, 1); }
            x_n(out, 0x9F, 0x1D, 0x9F, 0x1E, 0, 1);
        } else {
            if (mode == 0) { x_n(out, 0x49, 0x34, 0x4C, 0x34, 0, 1); x_n(out, 0xF3, 0x34, 0xF6, 0x34, 0, 1); }
            x_n(out, 0x9F, 0x10, 0x9F, 0x12, 0, 1);
        }
    }
    /* ---- altitude tape -------------------------------------------------------------------- */
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
            x_s18(out, d, 1, 0);
        }
    }
    /* ---- the frame -------------------------------------------------------------------------------- */
    {
        x_k(out, 15);
        if (half == 0) {
            x_n(out, 0x2E, 0x5A, 0x2E, 0x0E, 0, 1); x_n(out, 0x112, 0x5A, 0x112, 0x0E, 0, 1);
            x_k(out, 8);
            x_n(out, 0x2D, 0x5A, 0x2D, 0x0E, 0, 1); x_n(out, 0x113, 0x5A, 0x113, 0x0E, 0, 1);
            x_n(out, 0x113, 0x0E, 0xE8, 2, 0, 1); x_n(out, 0xE8, 2, 0x5A, 2, 0, 1); x_n(out, 0x2D, 0x0E, 0x59, 2, 0, 1);
        } else {
            x_n(out, 0x62, 0x5A, 0x62, 0x1E, 0, 1); x_n(out, 0xDD, 0x5A, 0xDD, 0x1E, 0, 1);
            x_k(out, 8);
            x_n(out, 0x61, 0x5A, 0x61, 0x1E, 0, 1); x_n(out, 0xDE, 0x5A, 0xDE, 0x1E, 0, 1);
            x_n(out, 0xDE, 0x1E, 0xD1, 0x11, 0, 1); x_n(out, 0xD1, 0x11, 0x6E, 0x11, 0, 1); x_n(out, 0x6E, 0x11, 0x61, 0x1E, 0, 1);
        }
    }
    /* ---- readout bitmap (the reticle) and the G meter ---------------------------------------------- */
    if (MB(d, 0x2956)) x_s18(out, d, 0, 1);
    pW(d, 0x2896, MW(d, 0x2948));
    x_t(out, d, 4, 0x288E, 0x9900, 1);

    /* ---- pitch ladder -------------------------------------------------------------------------------- */
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
    if (MB(d, HUD_LADDER_ON) == 0) return 0;
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
    return 0;
}

/* ---- FINE ----------------------------------------------------------------------------------------- */
static hud_fprim *fine_new(hud_fine_list *l)
{
    if (l->n >= HUD_MAX_FINE) { l->overflow = 1; return NULL; }
    hud_fprim *p = &l->p[l->n++];
    memset(p, 0, sizeof *p);
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
    p->ntext = (uint8_t)(ntext > 11 ? 11 : ntext);
    memcpy(p->text, text, p->ntext);
    p->cl = 0; p->ct = 0; p->cr = 320 * N; p->cb = 200 * N;
}

static int text_from(const mem_t *m, int at, uint8_t *out)
{
    int n = 0;
    while (n < 11 && MB(m, at + n)) { out[n] = MB(m, at + n); n++; }
    return n;
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
        for (int j = (int)tlo; j <= (int)thi && has_strip; j++) {
            const double c = 159.0 + k * (5.0 * j - hdg);
            if (c < sx - 1.0 || c > sx + sw) continue;
            const int major = ((j % 2) + 2) % 2 == 0;
            f_line(out, N, c, major ? sy : sy + shh - 1, c, sy + shh - 1, tape_colour, sx, sy, sx + sw - 1, sy + shh - 1);
        }
    }
    /* the pitch ladder */
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

int hud_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int32_t *out, int cap)
{
    if (ns < HUD_REC_BASE || no < HUD_REC_BASE || s[0] != HUD_REC_VERSION || o[0] != HUD_REC_VERSION) return 0;
    if (cap < HUD_REC_FINE) return 0;
    if (fabs(w) < 1e-6) {             /* a hair from a logic frame: that frame's own state, not its quantised copy */
        memcpy(out, s, HUD_REC_BASE * sizeof *out);
        return HUD_REC_BASE;
    }
    /* the layout and the switches must agree: the constants 291C..294B, the readout flag, the colours,
     * the ladder flag */
    for (int off = 0x291C; off < 0x294C; off += 2) if (s[1 + (off - HUD_DS_LO) / 2] != o[1 + (off - HUD_DS_LO) / 2]) return 0;
    static const int sw[] = { 0x2956, 0x2CA0, HUD_LADDER_ON };
    for (unsigned k = 0; k < sizeof sw / sizeof sw[0]; k++) if (rec_word(s, sw[k]) != rec_word(o, sw[k])) return 0;
    double cs[5], co[5];
    rec_continuous(s, ns, cs);
    rec_continuous(o, no, co);
    double speed = cs[0] + w * (co[0] - cs[0]), alt = cs[1] + w * (co[1] - cs[1]);
    double hdg = arc(cs[2], co[2], w), pitch = arc(cs[3], co[3], w), roll = arc(cs[4], co[4], w);
    if (speed < 0) speed = 0;
    if (alt < 0) alt = 0;
    hdg = fmod(hdg, 65536.0); if (hdg < 0) hdg += 65536.0;
    roll = fmod(roll, 65536.0); if (roll < 0) roll += 65536.0;
    memcpy(out, s, HUD_REC_BASE * sizeof *out);
    out[HUD_REC_BASE] = HUD_FINE_MAGIC;
    out[HUD_REC_BASE + 1] = (int32_t)floor(speed * 1024.0 + 0.5);
    out[HUD_REC_BASE + 2] = (int32_t)floor(alt * 256.0 + 0.5);
    out[HUD_REC_BASE + 3] = (int32_t)floor(hdg * 256.0 + 0.5);
    out[HUD_REC_BASE + 4] = (int32_t)floor(pitch * 256.0 + 0.5);
    out[HUD_REC_BASE + 5] = (int32_t)floor(roll * 256.0 + 0.5);
    return HUD_REC_FINE;
}
