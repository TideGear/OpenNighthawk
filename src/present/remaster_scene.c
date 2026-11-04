/* remaster_scene.c - from a model request to camera-space primitives (see remaster_scene.h). */
#include "remaster_scene.h"

#include "scene_mesh.h"
#include "tls.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_SIZE 1024

unsigned remaster_scene_dropped[3];   /* primitives left out: a vertex the game did not make (dots), (edges), a chain that is not one */

typedef struct {
    uint64_t bank;                  /* the bank's signature */
    uint16_t model, depth;
    scene_mesh *mesh;               /* NULL: a slot never used, or a model that would not decode (failed != 0) */
    int failed;
} cache_entry;

struct remaster_scene {
    cache_entry cache[CACHE_SIZE];
    uint16_t index[4096];           /* hash of (bank, model, level) -> cache slot + 1: a lookup is two probes, not a scan of the cache */
    unsigned next;                  /* the next slot to take when none matches */
    const uint8_t *sig_page; uint8_t sig_samples[40]; uint64_t sig;   /* the last bank page's signature, reused while its samples are unchanged */
    scene_pose pose;
    scene_geometry geometry;
    uint32_t serial;                /* the requests added, numbering each object of a frame (remaster_prim.owner) */
};

void remaster_frame_clear(remaster_frame *f) { f->n = 0; }

void remaster_frame_free(remaster_frame *f)
{
    free(f->prim);
    memset(f, 0, sizeof *f);
}

remaster_scene *remaster_scene_new(void)
{
    return (remaster_scene *)calloc(1, sizeof(remaster_scene));
}

void remaster_scene_free(remaster_scene *s)
{
    if (!s) return;
    for (unsigned i = 0; i < CACHE_SIZE; i++) scene_mesh_free(s->cache[i].mesh);
    free(s);
}

/* a signature of the 64 KB bank page: the first kilobyte whole, then a word every 64 bytes */
static uint64_t bank_signature(const uint8_t *page)
{
    uint64_t h = UINT64_C(14695981039346656037);
    for (unsigned k = 0; k < 1024; k++) h = (h ^ page[k]) * UINT64_C(1099511628211);
    for (unsigned k = 1024; k < SCENE_SEGMENT; k += 64) h = (h ^ page[k] ^ ((uint64_t)page[k + 1] << 8)) * UINT64_C(1099511628211);
    return h;
}

/* The bank's signature, kept while forty sampled bytes (the first sixteen and one every 2.7 KB) and the page are unchanged: hashing two thousand bytes for every
 * model asked for was most of what a cached lookup cost. A new bank (a change of theatre) changes those bytes. */
static uint64_t bank_signature_cached(remaster_scene *s, const uint8_t *page)
{
    uint8_t samples[40];
    for (unsigned k = 0; k < 16; k++) samples[k] = page[k];
    for (unsigned k = 0; k < 24; k++) samples[16 + k] = page[16 + k * 2731u];
    if (s->sig_page == page && !memcmp(samples, s->sig_samples, sizeof samples)) return s->sig;
    s->sig_page = page; memcpy(s->sig_samples, samples, sizeof samples);
    s->sig = bank_signature(page);
    return s->sig;
}

/* The mesh for a model at a camera depth. The cache is keyed on the detail level the depth selects, not on the depth: every object at its own depth used to
 * decode its model again (a copy of the 64 KB bank and a hash of it each time). */
static const scene_mesh *lookup(remaster_scene *s, const uint8_t *bank_page, uint64_t sig, uint16_t model, uint16_t depth)
{
    uint16_t lod;
    if (!scene_mesh_lod(bank_page, model, depth, &lod)) return NULL;
    const unsigned h = (unsigned)((sig ^ (sig >> 29) ^ ((uint64_t)model * 0x9E3779B1u) ^ ((uint64_t)lod * 0x85EBCA6Bu)) & 4095u);
    for (unsigned probe = 0; probe < 2; probe++) {
        const unsigned slot = s->index[h ^ probe];
        if (!slot) continue;
        const cache_entry *e = &s->cache[slot - 1];
        if ((e->mesh || e->failed) && e->bank == sig && e->model == model && e->depth == lod) return e->failed ? NULL : e->mesh;
    }
    cache_entry *e = &s->cache[s->next % CACHE_SIZE];
    const unsigned slot = s->next++ % CACHE_SIZE;
    scene_mesh_free(e->mesh);
    scene_error err = SCENE_OK;
    e->mesh = scene_mesh_decode(bank_page, SCENE_SEGMENT, model, depth, &err);
    e->bank = sig; e->model = model; e->depth = lod;
    e->failed = e->mesh == NULL;
    unsigned put = h;
    if (s->index[h]) { const cache_entry *o = &s->cache[s->index[h] - 1]; if (o->mesh || o->failed) put = h ^ 1; }     /* the second probe when the first holds a live entry */
    s->index[put] = (uint16_t)(slot + 1);
    return e->mesh;
}

typedef struct {
    const scene_mesh *mesh;
    const scene_geometry *geometry;
    remaster_frame *f;
    uint16_t model;
    uint8_t darken;
    int added;
    int ok;
    uint32_t serial;
} emit_ctx;

static int grow(remaster_frame *f)
{
    if (f->n < f->cap) return 1;
    const size_t cap = f->cap ? f->cap * 2 : 256;
    remaster_prim *p = (remaster_prim *)realloc(f->prim, cap * sizeof *p);
    if (!p) return 0;
    f->prim = p;
    f->cap = cap;
    return 1;
}

static void put_vertex(remaster_prim *p, const scene_geometry *g, unsigned id)
{
    p->v[p->n].x = g->camera[id][0];
    p->v[p->n].y = g->camera[id][1];
    p->v[p->n].z = g->camera[id][2];
    p->n++;
}

/* When a face's edges do not run end to end as one chain, the original still fills it: its edge spans (130D:004A) take each
 * edge alone and fill by row. Take the edge set's vertices and order them round the face by angle about their centre in the
 * face's plane (a convex face, as the models' faces are). Returns the count, written to ids (cap entries). */
static unsigned order_by_angle(const scene_mesh *mesh, const scene_geometry *g, const scene_material *c, unsigned *ids, unsigned cap)
{
    unsigned m = 0;
    for (unsigned k = 0; k < c->count; k++)
        for (int side = 0; side < 2; side++) {
            const unsigned v = side ? mesh->edge[c->item[k]].b : mesh->edge[c->item[k]].a;
            int seen = 0;
            for (unsigned j = 0; j < m; j++) if (ids[j] == v) seen = 1;
            if (!seen && m < cap) ids[m++] = v;
        }
    if (m < 3) return m;
    double cx = 0, cy = 0, cz = 0;
    for (unsigned j = 0; j < m; j++) { cx += g->camera[ids[j]][0]; cy += g->camera[ids[j]][1]; cz += g->camera[ids[j]][2]; }
    cx /= m; cy /= m; cz /= m;
    const double ux = g->camera[ids[0]][0] - cx, uy = g->camera[ids[0]][1] - cy, uz = g->camera[ids[0]][2] - cz;
    double nx = 0, ny = 0, nz = 0;
    for (unsigned j = 1; j < m && nx == 0 && ny == 0 && nz == 0; j++) {
        const double vx = g->camera[ids[j]][0] - cx, vy = g->camera[ids[j]][1] - cy, vz = g->camera[ids[j]][2] - cz;
        nx = uy * vz - uz * vy; ny = uz * vx - ux * vz; nz = ux * vy - uy * vx;
    }
    if ((ux == 0 && uy == 0 && uz == 0) || (nx == 0 && ny == 0 && nz == 0)) return m;
    const double wx = ny * uz - nz * uy, wy = nz * ux - nx * uz, wz = nx * uy - ny * ux;   /* in the plane, perpendicular to u */
    double ang[32];
    for (unsigned j = 0; j < m; j++) {
        const double vx = g->camera[ids[j]][0] - cx, vy = g->camera[ids[j]][1] - cy, vz = g->camera[ids[j]][2] - cz;
        ang[j] = atan2(vx * wx + vy * wy + vz * wz, vx * ux + vy * uy + vz * uz);
    }
    for (unsigned i = 1; i < m; i++) {
        const double ka = ang[i]; const unsigned kv = ids[i]; unsigned j = i;
        while (j > 0 && ang[j - 1] > ka) { ang[j] = ang[j - 1]; ids[j] = ids[j - 1]; j--; }
        ang[j] = ka; ids[j] = kv;
    }
    return m;
}

static int emit(void *user, const scene_material *c)
{
    emit_ctx *x = (emit_ctx *)user;
    if (!grow(x->f)) { x->ok = 0; return 0; }
    remaster_prim *p = &x->f->prim[x->f->n];
    memset(p, 0, sizeof *p);
    p->kind = c->kind;
    p->colour_index = c->colour_index;
    p->colour_word = c->colour_word;
    p->shade = c->shade;
    p->darken = x->darken;
    p->face = c->face;
    p->model = x->model;
    p->owner = x->serial << 12 | (c->face & 0xFFFu);
    if (c->kind == SCENE_LINES) {                             /* the items are edges, each its own segment (not one chain) */
        const remaster_prim head = *p;
        for (unsigned k = 0; k < c->count; k++) {
            const scene_edge *e = &x->mesh->edge[c->item[k]];
            if (!x->geometry->present[e->a] || !x->geometry->present[e->b]) { remaster_scene_dropped[1]++; continue; }
            if (!grow(x->f)) { x->ok = 0; return 0; }
            remaster_prim *q = &x->f->prim[x->f->n];
            *q = head;
            q->n = 0;
            put_vertex(q, x->geometry, e->a);
            put_vertex(q, x->geometry, e->b);
            x->f->n++;
            x->added++;
        }
        return 1;
    }
    if (c->kind == SCENE_DOTS) {                              /* the items are vertices */
        for (unsigned k = 0; k < c->count && p->n < REMASTER_PRIM_VERTICES; k++) {
            if (!x->geometry->present[c->item[k]]) { remaster_scene_dropped[0]++; return 1; }  /* a vertex the game did not make: skip the dot set */
            put_vertex(p, x->geometry, c->item[k]);
        }
    } else {                                                  /* the items are edges: a chain of vertices */
        /* the edges as listed need not run end to end: chain them by the vertex they share */
        unsigned used = 0, cur = 0, taken = 0;
        for (unsigned k = 0; k < c->count; k++) {
            const scene_edge *e = &x->mesh->edge[c->item[k]];
            if (!x->geometry->present[e->a] || !x->geometry->present[e->b]) { remaster_scene_dropped[1]++; return 1; }
        }
        {
            const scene_edge *e = &x->mesh->edge[c->item[0]];
            put_vertex(p, x->geometry, e->a); put_vertex(p, x->geometry, e->b);
            cur = e->b; used = 1u; taken = 1;
        }
        for (int progress = 1; progress && taken < c->count;) {
            progress = 0;
            for (unsigned k = 1; k < c->count; k++) {
                if (used >> k & 1u) continue;
                const scene_edge *e = &x->mesh->edge[c->item[k]];
                if (e->a == cur) { if (p->n < REMASTER_PRIM_VERTICES) put_vertex(p, x->geometry, e->b); cur = e->b; }
                else if (e->b == cur) { if (p->n < REMASTER_PRIM_VERTICES) put_vertex(p, x->geometry, e->a); cur = e->a; }
                else continue;
                used |= 1u << k; taken++; progress = 1;
            }
        }
        if (taken < c->count) {
            if (getenv("F117_REMASTER_DEBUG") && remaster_scene_dropped[2] < 6) {
                fprintf(stderr, "[nochain] model %u kind %d count %u taken %u edges:", (unsigned)x->model, c->kind, c->count, taken);
                for (unsigned k = 0; k < c->count; k++) fprintf(stderr, " %u-%u", (unsigned)x->mesh->edge[c->item[k]].a, (unsigned)x->mesh->edge[c->item[k]].b);
                fprintf(stderr, "\n");
            }
            remaster_scene_dropped[2]++;
            if (c->kind == SCENE_POLYGON) {                   /* not one chain: fill it from its edge set all the same */
                unsigned ids[32];
                const unsigned m = order_by_angle(x->mesh, x->geometry, c, ids, 32);
                if (m < 3) return 1;
                p->n = 0;
                for (unsigned k = 0; k < m && p->n < REMASTER_PRIM_VERTICES; k++) put_vertex(p, x->geometry, ids[k]);
                x->f->n++;
                x->added++;
                return 1;
            }
        }   /* not one chain */
        if (c->kind == SCENE_POLYGON && p->n > 2 && x->geometry->camera[cur][0] == p->v[0].x &&
            x->geometry->camera[cur][1] == p->v[0].y && x->geometry->camera[cur][2] == p->v[0].z)
            p->n--;                                           /* a closed chain repeats its first vertex */
    }
    if (c->kind == SCENE_POLYGON && p->n < 3) return 1;       /* a face needs three vertices: two are no polygon */
    x->f->n++;
    x->added++;
    return 1;
}

const scene_geometry *remaster_scene_geometry(const remaster_scene *s) { return &s->geometry; }
int remaster_scene_has_shared(const remaster_scene *s)
{
    for (unsigned i = 0; i < SCENE_VERTICES; i++) if (s->pose.shared_present[i]) return 1;
    return 0;
}
void remaster_scene_origin(const remaster_scene *s, int32_t out[3]) { for (int j = 0; j < 3; j++) out[j] = s->pose.part[0].translation[j]; }

static F117_TLS unsigned g_lod_shift;
void remaster_scene_lod_shift(unsigned bits) { g_lod_shift = bits; }
int remaster_scene_add(remaster_scene *s, const uint8_t *mem, size_t mem_size, uint16_t ds, uint16_t bank_segment,
                       uint16_t model, const int16_t position[3], remaster_frame *f)
{
    const size_t ds_at = (size_t)ds << 4, bank_at = (size_t)bank_segment << 4;
    if (ds_at + SCENE_SEGMENT > mem_size || bank_at + SCENE_SEGMENT > mem_size) return -1;
    const uint8_t *dsp = mem + ds_at, *bank = mem + bank_at;
    int16_t camera[9];
    for (int k = 0; k < 9; k++) camera[k] = (int16_t)(dsp[0x7B28 + 2 * k] | dsp[0x7B28 + 2 * k + 1] << 8);
    uint32_t depth32 = scene_model_depth(position, camera);
    if (g_lod_shift) { depth32 <<= g_lod_shift > 16 ? 16 : g_lod_shift; if (depth32 > 32767) depth32 = 32767; g_lod_shift = 0; }
    const uint16_t depth = (uint16_t)depth32;
    const scene_mesh *mesh = lookup(s, bank, bank_signature_cached(s, bank), model, depth);
    if (!mesh) return -1;
    s->serial = (s->serial + 1) & 0xFFFFF;
    if (!scene_pose_read(&s->pose, mesh, dsp, SCENE_SEGMENT)) return -1;
    if (!scene_mesh_eval(mesh, &s->pose, &s->geometry)) return -1;
    const size_t frame_start = f->n;
    emit_ctx ctx = { mesh, &s->geometry, f, model, dsp[0x48C4], 0, 1, s->serial };
    if (!scene_mesh_materials(mesh, &s->pose, &s->geometry, emit, &ctx)) {
        f->n = frame_start;
        return -1;
    }
    {   /* a ground object (the runway, an apron): every polygon of it lies level. The world's up axis in camera space is that
         * axis's three matrix words (axis 1: measured, the runway and the jets' wings come out level with it). */
        static int axis = -1;
        if (axis < 0) { const char *e = getenv("F117_FLAT_AXIS"); axis = e ? atoi(e) : 1; }
        int all_level = 0, polygons = 0;
        const double ux = camera[3 * axis], uy = camera[3 * axis + 1], uz = camera[3 * axis + 2];
        const double ul = sqrt(ux * ux + uy * uy + uz * uz);
        for (size_t i = frame_start; i < f->n; i++) {
            remaster_prim *p = &f->prim[i];
            if (p->kind != SCENE_POLYGON || p->n < 3 || ul <= 0.0) continue;
            const double ax = (double)p->v[1].x - p->v[0].x, ay = (double)p->v[1].y - p->v[0].y, az = (double)p->v[1].z - p->v[0].z;
            const double bx = (double)p->v[2].x - p->v[0].x, by = (double)p->v[2].y - p->v[0].y, bz = (double)p->v[2].z - p->v[0].z;
            const double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
            const double nl = sqrt(nx * nx + ny * ny + nz * nz);
            polygons++;
            if (nl > 0.0 && fabs(nx * ux + ny * uy + nz * uz) > 0.9 * nl * ul) all_level++;
        }
        const uint8_t ground = polygons > 0 && all_level == polygons;
        for (size_t i = frame_start; i < f->n; i++) {
            f->prim[i].flat = ground;
            if (ground) f->prim[i].owner |= 0x800u;
        }
    }
    return ctx.added;
}
