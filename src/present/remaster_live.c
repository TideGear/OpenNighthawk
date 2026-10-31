/* remaster_live.c - Remastered's view of a running game through the machine's execution hooks (see remaster_live.h). */
#include "remaster_live.h"

#include "remaster_raster.h"
#include "remaster_scene.h"
#include "scene_mesh.h"
#include "band_state.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* the watched entries, as {segment relative to the load segment, offset}: k = 0 the model request, 1 to 5 projection
 * returns, 6 the per-frame draw routine */
static const struct { uint16_t seg, ip; } WATCH[7] = {
    { 0x120A, 0x00CF },
    { 0x120A, 0x08D5 }, { 0x120A, 0x0901 }, { 0x120A, 0x0937 }, { 0x120A, 0x096A }, { 0x120A, 0x0970 },
    { 0x0000, 0x1450 },
};

struct remaster_live {
    remaster_scene *scene;
    remaster_frame frame;
    int pending;
    uint16_t model, bank;
    int16_t position[3];
    unsigned frames, want_frame;
    int w, h;
    char *out_path;
    int written;
    unsigned requests, added, refused;
    int32_t band_rec[BAND_REC_BASE];  /* the sky and ground band pass's starting state (band_state.h), for the frame in progress */
    int have_band;
    int origin[2], window[4];       /* the game's screen origin [8602],[8604] and clip window [85FA],[85FC],[85FE],[8600] */
};

remaster_live *remaster_live_new(int w, int h, unsigned frame, const char *out_path)
{
    remaster_live *r = (remaster_live *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->scene = remaster_scene_new();
    r->w = w; r->h = h; r->want_frame = frame;
    if (out_path) {
        r->out_path = (char *)malloc(strlen(out_path) + 1);
        if (r->out_path) strcpy(r->out_path, out_path);
    }
    if (!r->scene) { remaster_live_free(r); return NULL; }
    return r;
}

void remaster_live_free(remaster_live *r)
{
    if (!r) return;
    remaster_scene_free(r->scene);
    remaster_frame_free(&r->frame);
    free(r->out_path);
    free(r);
}

unsigned remaster_live_arm(remaster_live *r, machine_t *m, uint16_t load_seg)
{
    (void)r;
    const unsigned base = m->exec_hook_n;
    for (unsigned k = 0; k < 7 && m->exec_hook_n < EXEC_HOOKS_MAX; k++) {
        m->exec_hook[m->exec_hook_n].cs = (uint16_t)(load_seg + WATCH[k].seg);
        m->exec_hook[m->exec_hook_n].ip = WATCH[k].ip;
        m->exec_hook_n++;
    }
    return base;
}

static uint16_t peek16(const machine_t *m, uint16_t seg, uint16_t off)
{
    const uint32_t at = ((uint32_t)seg << 4) + off;
    return (uint16_t)(m->mem[at & 0xFFFFF] | m->mem[(at + 1) & 0xFFFFF] << 8);
}

static uint32_t dac_colour(const machine_t *m, unsigned index);

/* A colour word as the original's fill reads it (model_fill, VGAME 1377:0B09 with model_fill_begin 1377:01E0; the code is
 * documented in src/matched/matched.c). The high byte is the style: FF one colour (the low byte; a low byte of A0h or
 * more is darkened by the object's [48C4], never below its band base, low byte & F0); below FB a colour pair dithered
 * across columns and swapped on odd rows (here the two palette entries blended); FE AND, FD OR and FC stipple (here
 * drawn as the low byte's colour); FB clears without drawing (returns 0: nothing to paint). */
static int word_colour(const machine_t *m, unsigned word, unsigned darken, uint32_t *out)
{
    const unsigned ah = word >> 8 & 255, al = word & 255;
    if (ah == 0xFB) return 0;
    if (ah == 0xFF) {
        unsigned pixel = al;
        if (al >= 0xA0) {
            const unsigned base = al & 0xF0;
            pixel = al >= darken + base ? al - darken : base;      /* al - darken, not below the band base */
        }
        *out = dac_colour(m, pixel);
        return 1;
    }
    if (ah > 0xFB) { *out = dac_colour(m, al); return 1; }
    const uint32_t a = dac_colour(m, ah), b = dac_colour(m, al);
    *out = 0xFF000000u | ((((a >> 16 & 255) + (b >> 16 & 255)) / 2) << 16) | ((((a >> 8 & 255) + (b >> 8 & 255)) / 2) << 8) |
           (((a & 255) + (b & 255)) / 2);
    return 1;
}

/* the game's palette entry as 0xAARRGGBB; the DAC holds six bits a channel */
static uint32_t dac_colour(const machine_t *m, unsigned index)
{
    const uint8_t *d = &m->dac[(index & 255) * 3];
    const unsigned r = d[0] << 2 | d[0] >> 4, g = d[1] << 2 | d[1] >> 4, b = d[2] << 2 | d[2] >> 4;
    return 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* F117_REMASTER_DEBUG: how a polygon's colour word, index and shade relate to the pixel the original drew at the
 * polygon's centre (the machine's last scanned-out picture, 320 x 200). A research aid for the colour mapping. */
static void debug_colours(const remaster_live *r, const machine_t *m)
{
    enum { MAXK = 400 };
    static struct { unsigned word, index, shade, pixel, n; } k[MAXK];
    unsigned nk = 0;
    for (size_t i = 0; i < r->frame.n; i++) {
        const remaster_prim *p = &r->frame.prim[i];
        if (p->kind != SCENE_POLYGON || p->n < 3) continue;
        double x = 0, y = 0, z = 0;
        for (int v = 0; v < p->n; v++) { x += p->v[v].x; y += p->v[v].y; z += p->v[v].z; }
        x /= p->n; y /= p->n; z /= p->n;
        if (z < 0x10000) continue;
        const int sx = (int)(160.0 + 256.0 * x / z), sy = (int)(100.0 + 256.0 * y / z);
        if (sx < 0 || sx >= 320 || sy < 0 || sy >= 200) continue;
        const unsigned pixel = m->scan_pixels[sy * 320 + sx];
        unsigned j = 0;
        while (j < nk && !(k[j].word == p->colour_word && k[j].index == p->colour_index && k[j].shade == p->shade && k[j].pixel == pixel)) j++;
        if (j == nk && nk < MAXK) { k[nk].word = p->colour_word; k[nk].index = p->colour_index; k[nk].shade = p->shade; k[nk].pixel = pixel; k[nk].n = 0; nk++; }
        if (j < nk) k[j].n++;
    }
    for (unsigned j = 0; j < nk; j++)
        fprintf(stderr, "[colour] word %04X index %3u shade %3u -> pixel %3u  x%u\n", k[j].word, k[j].index, k[j].shade, k[j].pixel, k[j].n);
}

static void write_frame(remaster_live *r, const machine_t *m)
{
    remaster_target t;
    t.w = r->w; t.h = r->h;
    t.color = (uint32_t *)malloc((size_t)r->w * r->h * 4);
    t.inv_z = (float *)malloc((size_t)r->w * r->h * 4);
    if (!t.color || !t.inv_z) { free(t.color); free(t.inv_z); return; }
    t.cx = r->origin[0] * r->h / 240.0; t.cy = r->origin[1] * r->h / 200.0;   /* the game's origin, in its 320 x 200 pixels */
    t.sx = 256.0 * r->h / 240.0; t.sy = 256.0 * r->h / 200.0;
    t.near_z = 0x10000;
    remaster_clear(&t, 0xFF000000u);
    if (r->have_band) {                         /* the sky and ground bands, as far as anything: the game's own state model */
        band_state st;
        band_exact_list ex;
        band_fine_list fine;
        const int loaded = band_state_load(r->band_rec, BAND_REC_BASE, &st);
        const int rx = loaded ? band_exact(&st, &ex) : -1, rf = loaded && !rx ? band_fine(&st, &ex, &fine) : -1;
        if (getenv("F117_REMASTER_DEBUG"))
            fprintf(stderr, "[bands] loaded %d detail %d flat %d/%d height %d exact %d fine %d n %d\n", loaded, loaded ? st.detail : -9, loaded ? st.flat_a : 0, loaded ? st.flat_b : 0, loaded ? st.height : 0, rx, rf, loaded && !rx ? ex.n : -1);
        if (loaded && !rx && !rf) {
            const double ux = r->h / 240.0, uy = r->h / 200.0, far_z = 10000000.0;
            for (int q = 0; q < fine.n; q++) {
                if (getenv("F117_REMASTER_DEBUG") && (q == 0 || q == 15 || q == 16 || q == 31))
                    fprintf(stderr, "[band %d] colour %u valid %u x %.1f %.1f %.1f %.1f y %.1f %.1f %.1f %.1f\n", q, fine.q[q].colour, fine.q[q].valid,
                            fine.q[q].x[0], fine.q[q].x[1], fine.q[q].x[2], fine.q[q].x[3], fine.q[q].y[0], fine.q[q].y[1], fine.q[q].y[2], fine.q[q].y[3]);
                if (!fine.q[q].valid) continue;
                remaster_v3 v[4];
                for (int k = 0; k < 4; k++) {   /* a screen point as a camera-space point at a very large depth */
                    v[k].x = (int32_t)((fine.q[q].x[k] * ux - t.cx) * far_z / t.sx);
                    v[k].y = (int32_t)((fine.q[q].y[k] * uy - t.cy) * far_z / t.sy);
                    v[k].z = (int32_t)far_z;
                }
                remaster_polygon(&t, v, 4, dac_colour(m, fine.q[q].colour));
            }
            for (long i = 0; i < (long)t.w * t.h; i++) t.inv_z[i] = 0.0f;   /* the bands are behind everything: forget their depth */
        }
    }
    for (size_t i = 0; i < r->frame.n; i++) {
        const remaster_prim *p = &r->frame.prim[i];
        if (p->kind != SCENE_POLYGON || p->n < 3) continue;     /* lines and dots later */
        uint32_t colour;
        if (!word_colour(m, p->colour_word, p->darken, &colour)) continue;
        remaster_polygon(&t, p->v, p->n, colour);
    }
    if (getenv("F117_REMASTER_DEBUG")) {
        debug_colours(r, m);
        fprintf(stderr, "[view] origin %d,%d window x %d..%d y %d..%d\n", r->origin[0], r->origin[1], r->window[0], r->window[2], r->window[1], r->window[3]);
    }
    {                                           /* nothing outside the game's clip window (its 320 x 200 pixels) */
        const double ux = r->h / 240.0, uy = r->h / 200.0;
        const int x0 = (int)(r->window[0] * ux), x1 = (int)((r->window[2] + 1) * ux);
        const int y0 = (int)(r->window[1] * uy), y1 = (int)((r->window[3] + 1) * uy);
        for (int y = 0; y < r->h; y++)
            for (int x = 0; x < r->w; x++)
                if (x < x0 || x >= x1 || y < y0 || y >= y1) t.color[(long)y * r->w + x] = 0xFF000000u;
    }
    FILE *f = fopen(r->out_path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", r->w, r->h);
        for (long i = 0; i < (long)r->w * r->h; i++) {
            const uint32_t c = t.color[i];
            const unsigned char px[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
            fwrite(px, 1, 3, f);
        }
        fclose(f);
        r->written = 1;
    }
    free(t.color);
    free(t.inv_z);
    if (r->written && strlen(r->out_path) < 900) {                  /* the original's last picture, 320 x 200, beside it */
        char path[1024];
        snprintf(path, sizeof path, "%s.orig.ppm", r->out_path);
        FILE *o = fopen(path, "wb");
        if (o) {
            fprintf(o, "P6\n320 200\n255\n");
            for (int i = 0; i < 64000; i++) {
                const uint8_t *d = &m->scan_dac[m->scan_pixels[i] * 3];
                const unsigned char px[3] = { (unsigned char)(d[0] << 2 | d[0] >> 4), (unsigned char)(d[1] << 2 | d[1] >> 4), (unsigned char)(d[2] << 2 | d[2] >> 4) };
                fwrite(px, 1, 3, o);
            }
            fclose(o);
        }
    }
}

int remaster_live_exec(remaster_live *r, machine_t *m, unsigned k)
{
    if (k == 0) {                                           /* the model request: return IP, CS, then the model */
        const uint16_t ss = m->cpu.seg[S_SS], sp = m->cpu.r[R_SP];
        r->pending = 1;
        r->requests++;
        r->model = peek16(m, ss, (uint16_t)(sp + 4));
        r->bank = peek16(m, m->cpu.seg[S_DS], 0x8590);
        r->origin[0] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x8602);
        r->origin[1] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x8604);
        r->window[0] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x85FA);
        r->window[1] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x85FC);
        r->window[2] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x85FE);
        r->window[3] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x8600);
        for (int i = 0; i < 3; i++) r->position[i] = (int16_t)peek16(m, ss, (uint16_t)(sp + 6 + 2 * i));
    } else if (k <= 5) {                                    /* a projection returned: the pose is prepared */
        if (r->pending) {
            r->pending = 0;
            const int n = remaster_scene_add(r->scene, m->mem, 0x100000, m->cpu.seg[S_DS], r->bank, r->model, r->position, &r->frame);
            if (n < 0) r->refused++; else r->added++;
        }
    } else if (k == 6) {                                    /* the per-frame draw routine: the last frame is complete */
        if (r->out_path && !r->written && r->frames == r->want_frame && r->frame.n) write_frame(r, m);
        {                                       /* this frame's band pass starts here: its argument is the view block */
            const uint16_t ds = m->cpu.seg[S_DS], view = peek16(m, m->cpu.seg[S_SS], (uint16_t)(m->cpu.r[R_SP] + 2));
            int n = 0;
            r->band_rec[n++] = BAND_REC_VERSION;
            r->band_rec[n++] = view;
            r->band_rec[n++] = peek16(m, ds, (uint16_t)(view + 4));
            for (int i = 0; i < BAND_NWORDS; i++)
                r->band_rec[n++] = band_rec_byte[i] ? m->mem[(((uint32_t)ds << 4) + band_rec_off[i]) & 0xFFFFF] : peek16(m, ds, band_rec_off[i]);
            r->have_band = 1;
        }
        r->frames++;
        remaster_frame_clear(&r->frame);
        r->pending = 0;
    }
    return 0;
}

size_t remaster_live_prims(const remaster_live *r) { return r->frame.n; }
unsigned remaster_live_frames(const remaster_live *r) { return r->frames; }
