/* band_state.c - see band_state.h. The EXACT half follows VGAME 0000:1450 as matched.c's vgame_horizon_bands
 * runs it; the FINE half is the same formulas without their roundings. */
#include "band_state.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int s16(int64_t v) { return (int16_t)(uint16_t)(uint64_t)v; }
static int s8(int v) { return (int8_t)(uint8_t)v; }

static int g_enabled = -1;

void band_state_enable(int on) { g_enabled = on != 0; }

int band_state_enabled(void)
{
    if (g_enabled < 0) {
        const char *e = getenv("F117_PRESENT_BANDS");
        g_enabled = !(e && (!strcmp(e, "old") || !strcmp(e, "0") || !strcmp(e, "off")));
    }
    return g_enabled;
}

/* ---- loading ----------------------------------------------------------------------------------- */
enum { R_VERSION, R_VIEW, R_LOWCOLOUR, R_DETAIL, R_FLAT_A, R_FLAT_B, R_HEIGHT, R_SP, R_CP, R_SR, R_CR, R_SHIFT, R_HALVE,
       R_CX, R_CY, R_TABLE, R_XMAX = R_TABLE + 16, R_YMAX };

int band_state_load(const int32_t *v, int n, band_state *st)
{
    if (n < BAND_REC_BASE || v[R_VERSION] != BAND_REC_VERSION) return 0;
    memset(st, 0, sizeof *st);
    st->view = v[R_VIEW] & 0xFFFF;
    st->low_colour = v[R_LOWCOLOUR] & 0xFFFF;
    st->detail = s8(v[R_DETAIL]);
    st->flat_a = s16(v[R_FLAT_A]);
    st->flat_b = s16(v[R_FLAT_B]);
    st->height = s16(v[R_HEIGHT]);
    st->sp = s16(v[R_SP]); st->cp = s16(v[R_CP]);
    st->sr = s16(v[R_SR]); st->cr = s16(v[R_CR]);
    st->shift = v[R_SHIFT] & 0xFF;
    st->halve = v[R_HALVE] & 0xFF;
    st->cx = s16(v[R_CX]); st->cy = s16(v[R_CY]);
    for (int k = 0; k < 16; k++) st->table[k] = s16(v[R_TABLE + k]);
    st->xmax = s16(v[R_XMAX]); st->ymax = s16(v[R_YMAX]);
    st->f_height = st->height; st->f_sp = st->sp; st->f_cp = st->cp; st->f_sr = st->sr; st->f_cr = st->cr;
    if (n >= BAND_REC_FINE && v[BAND_REC_BASE] == (int32_t)BAND_FINE_MAGIC) {
        st->f_height = v[BAND_REC_BASE + 1] / 256.0;
        st->f_sp = v[BAND_REC_BASE + 2] / 256.0; st->f_cp = v[BAND_REC_BASE + 3] / 256.0;
        st->f_sr = v[BAND_REC_BASE + 4] / 256.0; st->f_cr = v[BAND_REC_BASE + 5] / 256.0;
        st->interpolated = 1;
    }
    return 1;
}

/* ---- EXACT ------------------------------------------------------------------------------------- */
/* 104E:0000: the 1.15 multiply. IMUL to DX:AX, the pair shifted left once (SHL AX / RCL DX), then the next
 * bit down added to DX (SHL AX / ADC DX,0): the product over 32,768, rounded half up, in a word. */
static int mul16(int a, int b)
{
    const uint32_t p = (uint32_t)((int32_t)(int16_t)a * (int32_t)(int16_t)b);
    const uint32_t p1 = p << 1;
    return s16((int64_t)((p1 >> 16) + ((p1 >> 15) & 1u)));
}

/* the same without a word to hold it (for telling a wrapped corner from a true one) */
static int64_t mulw(int64_t a, int64_t b)
{
    const int64_t p = a * b;
    return (p >> 15) + ((p >> 14) & 1);
}

static int sar16(int v, int n) { return (int16_t)v >> n; }

typedef struct { int x[2], y[2]; int64_t wx[2], wy[2]; } edge_pts;   /* right and left, as words and unwrapped */

/* One band edge (offset off from the horizon, along the view's vertical, rotated by roll), its two points at
 * +-22600 along the horizon: the arithmetic of 1500..159B (sky) and 167A..1718 (ground). */
static void edge_points(const band_state *st, int off, int64_t off_w, edge_pts *e)
{
    const int neg_s = s16(-(int64_t)st->sr);
    const int64_t neg_s_w = -(int64_t)st->sr;
    int si = mul16(-22600, st->cr), di = mul16(off, neg_s);
    int64_t siw = mulw(-22600, st->cr), diw = mulw(off_w, neg_s_w);
    e->x[0] = s16((int64_t)st->cx - di + si);  e->wx[0] = st->cx - diw + siw;
    e->x[1] = s16((int64_t)st->cx - di - si);  e->wx[1] = st->cx - diw - siw;
    si = mul16(off, st->cr); di = mul16(-22600, neg_s);
    siw = mulw(off_w, st->cr); diw = mulw(-22600, neg_s_w);
    const int t = s16((int64_t)di + si), u = s16((int64_t)si - di);
    e->y[0] = s16((int64_t)sar16(t, 2) - di - si + st->cy);
    e->y[1] = s16((int64_t)sar16(u, 2) - si + st->cy + di);
    const int64_t tw = diw + siw, uw = siw - diw;
    e->wy[0] = (tw >> 2) - tw + st->cy;
    e->wy[1] = (uw >> 2) - uw + st->cy;
}

/* the horizon's offset (14B9..14F2): 256 tan(-pitch), the cosine floored at 512, shifted and halved */
static void horizon(const band_state *st, int *h16, int64_t *hw)
{
    const int den = st->cp >= 512 ? st->cp : 512;
    const int32_t num = (int32_t)st->sp * 256;
    const int32_t q = num / den;
    int h = s16(q);
    int64_t w = q;
    if (st->shift) { h = s16((int64_t)(uint16_t)h << (st->shift & 31)); w = w * ((int64_t)1 << (st->shift & 31)); }
    if (st->halve) { h = sar16(h, 1); w >>= 1; }
    *h16 = h; *hw = w;
}

static void make_quads(band_exact_list *out, const edge_pts *pts, int first, int base_colour, int flat, int flat_colour)
{
    for (int b = 0; b < 16; b++) {
        band_quad *q = &out->q[first + b];
        const edge_pts *a = &pts[b], *c = &pts[b + 1];
        q->colour = (uint8_t)(flat ? flat_colour : base_colour + b);
        q->x[0] = (int16_t)a->x[0]; q->y[0] = (int16_t)a->y[0];
        q->x[1] = (int16_t)a->x[1]; q->y[1] = (int16_t)a->y[1];
        q->x[2] = (int16_t)c->x[1]; q->y[2] = (int16_t)c->y[1];
        q->x[3] = (int16_t)c->x[0]; q->y[3] = (int16_t)c->y[0];
        q->wrapped = 0;
        for (int k = 0; k < 2; k++)
            if (a->x[k] != a->wx[k] || a->y[k] != a->wy[k] || c->x[k] != c->wx[k] || c->y[k] != c->wy[k]) q->wrapped = 1;
    }
}

int band_exact(const band_state *st, band_exact_list *out)
{
    out->n = 0;
    if (st->detail < 3) return 1;
    const int flat = st->flat_a != 0 && st->flat_b != 0;
    int h16;
    int64_t hw;
    horizon(st, &h16, &hw);
    edge_pts pts[17];
    for (int b = 0; b <= 16; b++) {
        const int off = b < 16 ? s16((int64_t)st->table[b] + h16) : 22600;
        const int64_t off_w = b < 16 ? st->table[b] + hw : 22600;
        edge_points(st, off, off_w, &pts[b]);
    }
    make_quads(out, pts, 0, 0x80, flat, 0x88);
    /* the ground's offsets (1633..1673): height / ((16 - b) * 32) less height / 512, the first band at zero */
    int g[16];
    const int g0 = st->height / 512;
    g[0] = 0;
    for (int b = 1; b < 16; b++) g[b] = s16((int64_t)(st->height / ((16 - b) * 32)) - g0);
    for (int b = 0; b <= 16; b++) {
        const int off = b < 16 ? s16((int64_t)h16 - g[b]) : -22600;
        const int64_t off_w = b < 16 ? hw - g[b] : -22600;
        edge_points(st, off, off_w, &pts[b]);
    }
    make_quads(out, pts, 16, 0x70, flat, 0x7C);
    out->n = BAND_QUADS;
    return 0;
}

/* ---- FINE -------------------------------------------------------------------------------------- */
static double fmulf(double a, double b) { return a * b / 32768.0; }

static void fine_edge(const band_state *st, double off, double *x, double *y)
{
    const double s = st->f_sr, c = st->f_cr;
    double si = fmulf(-22600, c), di = fmulf(off, -s);
    x[0] = st->cx - di + si;
    x[1] = st->cx - di - si;
    si = fmulf(off, c); di = fmulf(-22600, -s);
    y[0] = st->cy - 0.75 * (di + si);
    y[1] = st->cy - 0.75 * (si - di);
}

int band_fine(const band_state *st, const band_exact_list *ex, band_fine_list *out)
{
    out->n = 0;
    if (st->detail < 3) return 1;
    const int flat = st->flat_a != 0 && st->flat_b != 0;
    const double den = st->f_cp >= 512 ? st->f_cp : 512;
    double h = 256.0 * st->f_sp / den;
    if (st->shift) h *= ldexp(1.0, st->shift & 31);
    if (st->halve) h /= 2;
    double px[17][2], py[17][2];
    for (int half = 0; half < 2; half++) {
        for (int b = 0; b <= 16; b++) {
            double off;
            if (half == 0) off = b < 16 ? st->table[b] + h : 22600;
            else {
                const double g = b == 0 ? 0 : st->f_height / ((16 - b) * 32.0) - st->f_height / 512.0;
                off = b < 16 ? h - g : -22600;
            }
            fine_edge(st, off, px[b], py[b]);
        }
        for (int b = 0; b < 16; b++) {
            band_fquad *q = &out->q[16 * half + b];
            q->colour = (uint8_t)(flat ? (half ? 0x7C : 0x88) : (half ? 0x70 : 0x80) + b);
            q->x[0] = px[b][0]; q->y[0] = py[b][0];
            q->x[1] = px[b][1]; q->y[1] = py[b][1];
            q->x[2] = px[b + 1][1]; q->y[2] = py[b + 1][1];
            q->x[3] = px[b + 1][0]; q->y[3] = py[b + 1][0];
            int ok = 1;
            for (int k = 0; k < 4; k++) ok &= isfinite(q->x[k]) && isfinite(q->y[k]) && fabs(q->x[k]) < 1e6 && fabs(q->y[k]) < 1e6;
            if (ex && (ex->n != BAND_QUADS || ex->q[16 * half + b].wrapped)) ok = 0;
            q->valid = (uint8_t)ok;
        }
    }
    out->n = BAND_QUADS;
    return 0;
}

/* ---- interpolation ----------------------------------------------------------------------------- */
static void rec_continuous(const int32_t *v, int n, double c[5])
{
    if (n >= BAND_REC_FINE && v[BAND_REC_BASE] == (int32_t)BAND_FINE_MAGIC)
        for (int k = 0; k < 5; k++) c[k] = v[BAND_REC_BASE + 1 + k] / 256.0;
    else {
        c[0] = s16(v[R_HEIGHT]);
        c[1] = s16(v[R_SP]); c[2] = s16(v[R_CP]); c[3] = s16(v[R_SR]); c[4] = s16(v[R_CR]);
    }
}

/* a sine/cosine pair moved by angle on the shortest arc, its length linearly */
static void arc_pair(double sa, double ca, double sb, double cb, double w, double *s, double *c)
{
    const double pi = 3.14159265358979323846;
    const double a = atan2(sa, ca), b = atan2(sb, cb);
    double d = b - a;
    while (d > pi) d -= 2 * pi;
    while (d < -pi) d += 2 * pi;
    const double ra = sqrt(sa * sa + ca * ca), rb = sqrt(sb * sb + cb * cb);
    const double r = ra + w * (rb - ra), t = a + w * d;
    *s = r * sin(t);
    *c = r * cos(t);
}

int band_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int32_t *out, int cap)
{
    if (ns < BAND_REC_BASE || no < BAND_REC_BASE || s[0] != BAND_REC_VERSION || o[0] != BAND_REC_VERSION) return 0;
    if (cap < BAND_REC_FINE) return 0;
    if (fabs(w) < 1e-6) {             /* a hair from a logic frame: that frame's own state */
        memcpy(out, s, BAND_REC_BASE * sizeof *out);
        return BAND_REC_BASE;
    }
    static const int discrete[] = { R_VIEW, R_LOWCOLOUR, R_DETAIL, R_FLAT_A, R_FLAT_B, R_SHIFT, R_HALVE, R_CX, R_CY, R_XMAX, R_YMAX };
    for (unsigned k = 0; k < sizeof discrete / sizeof discrete[0]; k++) if (s[discrete[k]] != o[discrete[k]]) return 0;
    for (int k = 0; k < 16; k++) if (s[R_TABLE + k] != o[R_TABLE + k]) return 0;
    double cs[5], co[5], sp, cp, sr, cr;
    rec_continuous(s, ns, cs);
    rec_continuous(o, no, co);
    const double height = cs[0] + w * (co[0] - cs[0]);
    arc_pair(cs[1], cs[2], co[1], co[2], w, &sp, &cp);
    arc_pair(cs[3], cs[4], co[3], co[4], w, &sr, &cr);
    memcpy(out, s, BAND_REC_BASE * sizeof *out);
    out[BAND_REC_BASE] = (int32_t)BAND_FINE_MAGIC;
    const double vals[5] = { height, sp, cp, sr, cr };
    for (int k = 0; k < 5; k++) out[BAND_REC_BASE + 1 + k] = (int32_t)floor(vals[k] * 256.0 + 0.5);
    return BAND_REC_FINE;
}

/* ---- following a pass -------------------------------------------------------------------------- */
int band_cover_begin(band_cover *c, const int32_t *v, int n, int with_fine)
{
    band_state st;
    c->active = 0;
    if (!band_state_load(v, n, &st)) return 0;
    c->passes++;
    if (band_exact(&st, &c->ex)) { c->low_detail++; return 1; }
    c->state_ok = with_fine && band_fine(&st, &c->ex, &c->fine) == 0;
    c->xmax = st.xmax; c->ymax = st.ymax;
    c->n = c->ex.n;
    c->at = 0;
    c->active = 1;
    return 1;
}

int band_cover_step(band_cover *c, const int32_t *v, int n, const band_fquad **fq)
{
    if (!c->active) return BC_NONE;
    const band_quad *q = &c->ex.q[c->at];
    int same = n >= 10 && v[1] == 4 && (v[0] & 0xFF) == q->colour;
    for (int k = 0; k < 4 && same; k++) same = s16(v[2 + 2 * k]) == q->x[k] && s16(v[3 + 2 * k]) == q->y[k];
    if (!same) { c->mismatched++; c->active = 0; return BC_MISMATCH; }
    const int at = c->at++;
    if (c->at >= c->n) c->active = 0;
    if (!c->state_ok || !c->fine.q[at].valid) { c->kept++; return BC_KEPT; }
    c->replaced++;
    if (fq) *fq = &c->fine.q[at];
    return BC_REPLACED;
}

void band_cover_end(band_cover *c) { c->active = 0; }
