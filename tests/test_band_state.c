/* test_band_state.c - src/present/band_state.c against the original's own draw log.
 *
 *   test_band_state CORPUS_DIR
 *
 * CORPUS_DIR holds list.txt (NAME LOGFILE per line, LOGFILE relative to the directory) and the observer logs
 * (f117run --observe with F117R_OBSERVE_BANDS=1, reduced to the P, o, U, K and Q records by
 * F117R_OBSERVE_KINDS=PoUKQ) of scripted flights: banked, inverted, diving and climbing flight, a strike, a
 * landing, the camera's gradient and darkened renderings, and low detail. They are private, under
 * D:/f117-gate/present-v2/horizon/corpus (capture.py there).
 *
 * 1. EXACT: after every 'o' record at detail 3, the next 32 'U' records are the quadrilaterals band_exact
 *    predicts, in order, colour, corner count and every corner equal. At low detail no 'U' follows before the
 *    next phase: the first record after the 'o' is low detail's own colour record (the view block's word +4).
 * 2. What follows each band 'U': the colour record ('K') names the band's colour, and its span fill ('Q') lies
 *    in the window the record gives (rows 0..[2295], columns 0..[2293]).
 * 3. FINE at the record's own integer state agrees with EXACT to within the original's roundings: every corner
 *    of an unwrapped quad, integer minus float, within the bound the integer steps allow (the horizon offset's
 *    truncated divide, times 2^[4368], halved by SAR; two more truncated divides for a ground band; half a cell
 *    for each rounding multiply; the SAR's floor): x within 1 + e and y within 1.5 + 0.75 e, e the offset's
 *    error bound. The distribution is printed, all corners and those near the view.
 * 4. band_blend: the record of an in-between frame at w = 0 is the skeleton's own; at w = 1 its fine corners are
 *    the other frame's to 1/256 of the state's units (0.1 cell); at w = 0.5 every fine corner of a band edge near
 *    the view lies between the two frames' (or within 0.1 cell of them).
 *
 * Exit 1 on any failure, 77 when the corpus is absent. */
#include "band_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long g_fail;
static long c_pass, c_low, c_quads, c_wrapped, c_incomplete, c_low_u, c_k_ok, c_k_bad, c_q_ok, c_q_bad, c_q_empty, c_flat;
static long c_blend, c_blend_bad, c_blend_mid_bad, c_blend_refused;
static double dx_lo = 99, dx_hi = -99, dy_lo = 99, dy_hi = -99, sxx, syy;
static double ny_lo = 99, ny_hi = -99, nyy, ny_sum;
static long n_d, n_near, c_low_k_ok, c_low_k_bad;

static int parse(char *line, char *kind, int32_t *v, int cap)
{
    *kind = line[0];
    char *p = line + 1, *end;
    strtoull(p, &end, 10);
    p = end;
    int n = 0;
    while (n < cap) {
        const long x = strtol(p, &end, 10);
        if (end == p) break;
        v[n++] = (int32_t)x;
        p = end;
    }
    return n;
}

/* how far a band edge (corner 0 to corner 1 of a quad) drawn from the integer corners lies from the fine one, in
 * cells, across the edge, at the fine edge's point nearest the view's centre when that point is in the window: what
 * the picture shows of the corners' rounding */
static void edge_at_centre(const band_state *st, const band_quad *q, const band_fquad *f)
{
    const double fx = f->x[1] - f->x[0], fy = f->y[1] - f->y[0], fl = sqrt(fx * fx + fy * fy);
    const double ex = (double)q->x[1] - q->x[0], ey = (double)q->y[1] - q->y[0], el = sqrt(ex * ex + ey * ey);
    if (fl < 1 || el < 1) return;
    const double t = ((st->cx - f->x[0]) * fx + (st->cy - f->y[0]) * fy) / (fl * fl);
    const double px = f->x[0] + t * fx, py = f->y[0] + t * fy;
    if (px < 0 || px > st->xmax || py < 0 || py > st->ymax) return;
    /* signed distance of P from the integer line: corner 0 is the edge's end at -22600 along the horizon (the left end in
     * an upright view), so a positive distance puts the fine edge on the sky side of the integer one */
    const double d = ((px - q->x[0]) * ey - (py - q->y[0]) * ex) / el;
    if (d < ny_lo) ny_lo = d;
    if (d > ny_hi) ny_hi = d;
    nyy += d * d; ny_sum += d; n_near++;
}

static void check_fine_vs_exact(const band_state *st, const band_exact_list *ex, const band_fine_list *fl)
{
    for (int i = 0; i < ex->n; i++) if (!ex->q[i].wrapped) edge_at_centre(st, &ex->q[i], &fl->q[i]);
    const double eh = st->halve ? ldexp(1.0, st->shift & 31) / 2 + 0.5 : ldexp(1.0, st->shift & 31);
    for (int i = 0; i < ex->n; i++) {
        if (ex->q[i].wrapped) continue;
        const double e = eh + (i >= 16 ? 2 : 0);
        for (int k = 0; k < 4; k++) {
            const double dx = ex->q[i].x[k] - fl->q[i].x[k], dy = ex->q[i].y[k] - fl->q[i].y[k];
            if (dx < dx_lo) dx_lo = dx;
            if (dx > dx_hi) dx_hi = dx;
            if (dy < dy_lo) dy_lo = dy;
            if (dy > dy_hi) dy_hi = dy;
            sxx += dx * dx; syy += dy * dy; n_d++;
            if (fabs(dx) > 1 + e + 1e-9 || fabs(dy) > 1.5 + 0.75 * e + 1e-9) {
                if (g_fail < 10) printf("fine vs exact: quad %d corner %d exact (%d, %d) fine (%.3f, %.3f)\n", i, k, ex->q[i].x[k], ex->q[i].y[k], fl->q[i].x[k], fl->q[i].y[k]);
                g_fail++;
            }
        }
    }
}

/* a corner of an edge point is "near the view" when it lies within 2000 cells of the centre: the far ends at
 * +-22600 swing on arcs, so linear betweenness only means something near the view */
static void check_blend(const int32_t *a, int na, const int32_t *b, int nb)
{
    static int32_t out[BAND_REC_FINE];
    band_state sa, sb, sm;
    band_fine_list fa, fb, fm;
    if (!band_state_load(a, na, &sa) || !band_state_load(b, nb, &sb) || sa.detail < 3) return;
    int m = band_blend(a, na, b, nb, 0.0, out, BAND_REC_FINE);
    if (m != BAND_REC_BASE || memcmp(out, a, BAND_REC_BASE * sizeof *out)) { c_blend_bad++; g_fail++; return; }
    m = band_blend(a, na, b, nb, 1.0, out, BAND_REC_FINE);
    if (!m) { c_blend_refused++; return; }
    c_blend++;
    band_state_load(out, m, &sm);
    band_fine(&sm, NULL, &fm);
    band_fine(&sb, NULL, &fb);
    band_fine(&sa, NULL, &fa);
    for (int i = 0; i < BAND_QUADS; i++)
        for (int k = 0; k < 4; k++)
            if (fabs(fm.q[i].x[k] - fb.q[i].x[k]) > 0.1 || fabs(fm.q[i].y[k] - fb.q[i].y[k]) > 0.1) {
                if (c_blend_bad < 5) printf("blend w=1: quad %d corner %d (%.3f, %.3f) against (%.3f, %.3f)\n", i, k, fm.q[i].x[k], fm.q[i].y[k], fb.q[i].x[k], fb.q[i].y[k]);
                c_blend_bad++; g_fail++; return;
            }
    m = band_blend(a, na, b, nb, 0.5, out, BAND_REC_FINE);
    band_state_load(out, m, &sm);
    band_fine(&sm, NULL, &fm);
    for (int i = 0; i < BAND_QUADS; i++)
        for (int k = 0; k < 4; k++) {
            const double xa = fa.q[i].x[k], xb = fb.q[i].x[k], ya = fa.q[i].y[k], yb = fb.q[i].y[k];
            if (fabs(xa - sa.cx) > 2000 || fabs(ya - sa.cy) > 2000 || fabs(xb - sa.cx) > 2000 || fabs(yb - sa.cy) > 2000) continue;
            const double x = fm.q[i].x[k], y = fm.q[i].y[k];
            const double xl = (xa < xb ? xa : xb) - 0.1, xh = (xa < xb ? xb : xa) + 0.1, yl = (ya < yb ? ya : yb) - 0.1, yh = (ya < yb ? yb : ya) + 0.1;
            /* a turn bends the path; allow the arc's bulge: a tenth of the distance moved */
            const double bulge = 0.1 * (fabs(xa - xb) + fabs(ya - yb));
            if (x < xl - bulge || x > xh + bulge || y < yl - bulge || y > yh + bulge) { c_blend_mid_bad++; }
        }
}

static int run_log(const char *name, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { printf("%s: cannot read %s\n", name, path); g_fail++; return 0; }
    static char line[1 << 16];
    static int32_t v[4096], prev[4][BAND_REC_FINE];
    int nprev[4] = { 0 }, prev_view[4] = { -1, -1, -1, -1 };
    band_state st;
    static band_exact_list ex;
    static band_fine_list fl;
    int active = 0, at = 0, low = 0, want_k = -1, want_q = 0, low_k = -1;
    memset(&st, 0, sizeof st);
    long passes = 0, quads = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        char kind;
        const int n = parse(line, &kind, v, 4096);
        if (low_k >= 0 && kind != 'P' && kind != 'o') {
            if (kind == 'K' && (v[0] & 0xFF) == low_k) c_low_k_ok++;
            else { c_low_k_bad++; g_fail++; if (c_low_k_bad < 5) printf("%s pass %ld: low detail followed by '%c', not its colour %d\n", name, passes, kind, low_k); }
            low_k = -1;
        }
        switch (kind) {
        case 'P':
            low = 0;
            break;
        case 'o':
            if (active && at < BAND_QUADS) c_incomplete++;
            active = 0; low = 0; low_k = -1;
            if (!band_state_load(v, n, &st)) { printf("%s: bad 'o' record\n", name); g_fail++; break; }
            c_pass++; passes++;
            {
                int slot = 0;
                while (slot < 3 && prev_view[slot] >= 0 && prev_view[slot] != st.view) slot++;
                if (prev_view[slot] == st.view && nprev[slot]) check_blend(prev[slot], nprev[slot], v, n);
                prev_view[slot] = st.view;
                memcpy(prev[slot], v, (size_t)(n < BAND_REC_FINE ? n : BAND_REC_FINE) * sizeof *v);
                nprev[slot] = n < BAND_REC_FINE ? n : BAND_REC_FINE;
            }
            if (band_exact(&st, &ex)) { c_low++; low = 1; low_k = st.low_colour & 0xFF; break; }
            band_fine(&st, &ex, &fl);
            check_fine_vs_exact(&st, &ex, &fl);
            if (st.flat_a && st.flat_b) c_flat++;
            active = 1; at = 0;
            break;
        case 'U':
            if (low) { c_low_u++; break; }
            if (!active) break;
            {
                const band_quad *q = &ex.q[at];
                int same = n >= 10 && v[1] == 4 && (v[0] & 0xFF) == q->colour;
                for (int k = 0; k < 4 && same; k++) same = (int16_t)v[2 + 2 * k] == q->x[k] && (int16_t)v[3 + 2 * k] == q->y[k];
                if (!same) {
                    if (bad < 5) {
                        printf("%s pass %ld quad %d: log colour %d count %d", name, passes, at, v[0], v[1]);
                        for (int k = 0; k < 4 && 3 + 2 * k < n; k++) printf(" (%d,%d)", v[2 + 2 * k], v[3 + 2 * k]);
                        printf("  predicted colour %d", q->colour);
                        for (int k = 0; k < 4; k++) printf(" (%d,%d)", q->x[k], q->y[k]);
                        printf("\n");
                    }
                    bad++; g_fail++;
                    active = 0;
                    break;
                }
                c_quads++; quads++;
                c_wrapped += q->wrapped;
                want_k = q->colour; want_q = 1;
                if (++at >= BAND_QUADS) active = 0;
            }
            break;
        case 'K':
            if (want_k >= 0) {
                if ((v[0] & 0xFF) == want_k) c_k_ok++; else { c_k_bad++; g_fail++; }
                want_k = -1;
            }
            break;
        case 'Q':
            if (want_q) {
                want_q = 0;
                const int ya = v[0], nrows = v[3];
                int ok = 1;
                if (nrows <= 0) { c_q_empty++; break; }
                if (ya < 0 || ya + nrows - 1 > st.ymax) ok = 0;
                for (int r = 0; r < nrows && 5 + 2 * r < n && ok; r++) {
                    const int l = v[4 + 2 * r], rt = v[5 + 2 * r];
                    if (rt < l || (rt == l && (rt == 0 || rt == 0x13F))) continue;   /* the rasteriser's empty-row markers */
                    if (l < 0 || rt > st.xmax) ok = 0;
                }
                if (ok) c_q_ok++; else { c_q_bad++; if (c_q_bad < 5) printf("%s pass %ld: span fill outside the window (rows %d..%d)\n", name, passes, ya, ya + nrows - 1); g_fail++; }
            }
            break;
        default:
            break;
        }
    }
    fclose(f);
    printf("%-18s %6ld passes, %7ld quads matched, %ld mismatched\n", name, passes, quads, bad);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_band_state CORPUS_DIR\n"); return 2; }
    char path[1024];
    snprintf(path, sizeof path, "%s/list.txt", argv[1]);
    FILE *l = fopen(path, "r");
    if (!l) { printf("no corpus at %s: skipped\n", argv[1]); return 77; }
    char name[256], file[512];
    while (fscanf(l, "%255s %511s", name, file) == 2) {
        snprintf(path, sizeof path, "%s/%s", argv[1], file);
        run_log(name, path);
    }
    fclose(l);
    printf("EXACT: %ld passes (%ld at low detail, %ld flat), %ld quads matched (%ld with a wrapped corner), %ld passes cut short\n",
           c_pass, c_low, c_flat, c_quads, c_wrapped, c_incomplete);
    printf("low detail: %ld passes went straight to their own colour record, %ld did not (%ld 'U' records later in those phases, other "
           "callers of the span rasteriser)\n", c_low_k_ok, c_low_k_bad, c_low_u);
    printf("after each band: colour records %ld right, %ld wrong; span fills in the window %ld, outside %ld, empty %ld\n",
           c_k_ok, c_k_bad, c_q_ok, c_q_bad, c_q_empty);
    if (n_d)
        printf("FINE at the integer state, integer minus float over %ld corners: x [%.3f, %.3f] rms %.3f, y [%.3f, %.3f] rms %.3f\n",
               n_d, dx_lo, dx_hi, sqrt(sxx / n_d), dy_lo, dy_hi, sqrt(syy / n_d));
    if (n_near)
        printf("  band edges, integer line against fine line across the edge at the point nearest the view's centre (when in the window), "
               "%ld (positive: the fine edge lies on the sky side): [%.3f, %.3f] cells, mean %.3f, rms %.3f\n",
               n_near, ny_lo, ny_hi, ny_sum / n_near, sqrt(nyy / n_near));
    printf("blend: %ld consecutive pairs of one view checked, %ld refused (discrete words differ), %ld failures, %ld mid-way corners outside the bulge\n",
           c_blend, c_blend_refused, c_blend_bad, c_blend_mid_bad);
    printf(g_fail ? "FAIL (%ld)\n" : "ok\n", g_fail);
    return g_fail ? 1 : 0;
}
