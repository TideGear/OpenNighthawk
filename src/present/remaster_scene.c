/* remaster_scene.c - from a model request to camera-space primitives (see remaster_scene.h). */
#include "remaster_scene.h"

#include "scene_mesh.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_SIZE 1024

typedef struct {
    uint64_t bank;                  /* the bank's signature */
    uint16_t model, depth;
    scene_mesh *mesh;               /* NULL: a slot never used, or a model that would not decode (failed != 0) */
    int failed;
} cache_entry;

struct remaster_scene {
    cache_entry cache[CACHE_SIZE];
    unsigned next;                  /* the next slot to take when none matches */
    scene_pose pose;
    scene_geometry geometry;
    uint32_t requests;
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

static const scene_mesh *lookup(remaster_scene *s, const uint8_t *bank_page, uint64_t sig, uint16_t model, uint16_t depth)
{
    for (unsigned i = 0; i < CACHE_SIZE; i++) {
        const cache_entry *e = &s->cache[i];
        if ((e->mesh || e->failed) && e->bank == sig && e->model == model && e->depth == depth) return e->failed ? NULL : e->mesh;
    }
    cache_entry *e = &s->cache[s->next++ % CACHE_SIZE];
    scene_mesh_free(e->mesh);
    scene_error err = SCENE_OK;
    e->mesh = scene_mesh_decode(bank_page, SCENE_SEGMENT, model, depth, &err);
    e->bank = sig; e->model = model; e->depth = depth;
    e->failed = e->mesh == NULL;
    return e->mesh;
}

typedef struct {
    const scene_mesh *mesh;
    const scene_geometry *geometry;
    remaster_frame *f;
    uint16_t model;
    int added;
    int ok;
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

/* A face's command lists its edges in any order: the original's edge spans (130D:004A) take each edge alone and fill by
 * row, so no chain is needed. Order the edge set's vertices round the face: walk the edges end to end when they make one
 * closed loop, else sort the vertices by angle about their centre in the face's plane. */
static int order_vertices(const scene_mesh *mesh, const scene_geometry *g, const scene_material *c, unsigned *out, unsigned cap)
{
    unsigned a[32], b[32], n = c->count;
    for (unsigned k = 0; k < n; k++) { a[k] = mesh->edge[c->item[k]].a; b[k] = mesh->edge[c->item[k]].b; }
    int used[32] = { 0 };
    unsigned m = 0, cur = b[0];
    out[m++] = a[0]; used[0] = 1;
    for (unsigned step = 1; step < n; step++) {
        int found = -1;
        for (unsigned k = 0; k < n; k++) if (!used[k] && (a[k] == cur || b[k] == cur)) { found = (int)k; break; }
        if (found < 0) break;
        out[m++] = cur;
        used[found] = 1;
        cur = a[found] == cur ? b[found] : a[found];
    }
    unsigned all_used = 1;
    for (unsigned k = 0; k < n; k++) if (!used[k]) all_used = 0;
    if (all_used && cur == out[0] && m >= 3) return (int)m;           /* one closed loop */
    m = 0;                                                            /* otherwise the vertex set, by angle */
    for (unsigned k = 0; k < n; k++) for (int e = 0; e < 2; e++) {
        const unsigned v = e ? b[k] : a[k];
        int seen = 0;
        for (unsigned j = 0; j < m; j++) if (out[j] == v) seen = 1;
        if (!seen && m < cap) out[m++] = v;
    }
    if (m < 3) return (int)m;
    double cx = 0, cy = 0, cz = 0;
    for (unsigned j = 0; j < m; j++) { cx += g->camera[out[j]][0]; cy += g->camera[out[j]][1]; cz += g->camera[out[j]][2]; }
    cx /= m; cy /= m; cz /= m;
    double nx = 0, ny = 0, nz = 0;                                    /* the face's normal from the first non-degenerate pair */
    const double ux = g->camera[out[0]][0] - cx, uy = g->camera[out[0]][1] - cy, uz = g->camera[out[0]][2] - cz;
    for (unsigned j = 1; j < m && nx == 0 && ny == 0 && nz == 0; j++) {
        const double vx = g->camera[out[j]][0] - cx, vy = g->camera[out[j]][1] - cy, vz = g->camera[out[j]][2] - cz;
        nx = uy * vz - uz * vy; ny = uz * vx - ux * vz; nz = ux * vy - uy * vx;
    }
    const double ul = sqrt(ux * ux + uy * uy + uz * uz);
    if (ul == 0 || (nx == 0 && ny == 0 && nz == 0)) return (int)m;
    const double wx = ny * uz - nz * uy, wy = nz * ux - nx * uz, wz = nx * uy - ny * ux;   /* in-plane, perpendicular to u */
    double ang[32];
    for (unsigned j = 0; j < m; j++) {
        const double vx = g->camera[out[j]][0] - cx, vy = g->camera[out[j]][1] - cy, vz = g->camera[out[j]][2] - cz;
        ang[j] = atan2(vx * wx + vy * wy + vz * wz, vx * ux + vy * uy + vz * uz);
    }
    for (unsigned i = 1; i < m; i++) {                                /* insertion sort by angle */
        const double ka = ang[i]; const unsigned kv = out[i]; unsigned j = i;
        while (j > 0 && ang[j - 1] > ka) { ang[j] = ang[j - 1]; out[j] = out[j - 1]; j--; }
        ang[j] = ka; out[j] = kv;
    }
    return (int)m;
}

static remaster_prim *new_prim(emit_ctx *x, const scene_material *c)
{
    if (!grow(x->f)) { x->ok = 0; return NULL; }
    remaster_prim *p = &x->f->prim[x->f->n];
    memset(p, 0, sizeof *p);
    p->kind = c->kind;
    p->colour_index = c->colour_index;
    p->colour_word = c->colour_word;
    p->shade = c->shade;
    p->face = c->face;
    p->model = x->model;
    return p;
}

static int emit(void *user, const scene_material *c)
{
    emit_ctx *x = (emit_ctx *)user;
    if (c->kind == SCENE_DOTS) {                              /* the items are vertices */
        remaster_prim *p = new_prim(x, c);
        if (!p) return 0;
        for (unsigned k = 0; k < c->count && p->n < REMASTER_PRIM_VERTICES; k++) {
            if (!x->geometry->present[c->item[k]]) return 1;  /* a vertex the game did not make: skip the dot set */
            put_vertex(p, x->geometry, c->item[k]);
        }
        x->f->n++; x->added++;
    } else if (c->kind == SCENE_LINES) {                      /* each edge is its own segment */
        for (unsigned k = 0; k < c->count; k++) {
            const scene_edge *e = &x->mesh->edge[c->item[k]];
            if (!x->geometry->present[e->a] || !x->geometry->present[e->b]) continue;
            remaster_prim *p = new_prim(x, c);
            if (!p) return 0;
            put_vertex(p, x->geometry, e->a);
            put_vertex(p, x->geometry, e->b);
            x->f->n++; x->added++;
        }
    } else {                                                  /* a polygon: the edge set's vertices round the face */
        unsigned ids[64];
        for (unsigned k = 0; k < c->count; k++) {
            const scene_edge *e = &x->mesh->edge[c->item[k]];
            if (!x->geometry->present[e->a] || !x->geometry->present[e->b]) return 1;
        }
        const int m = order_vertices(x->mesh, x->geometry, c, ids, 32);
        if (m < 3) return 1;
        remaster_prim *p = new_prim(x, c);
        if (!p) return 0;
        for (int k = 0; k < m && p->n < REMASTER_PRIM_VERTICES; k++) put_vertex(p, x->geometry, ids[k]);
        x->f->n++; x->added++;
    }
    return 1;
}

int remaster_scene_add(remaster_scene *s, const uint8_t *mem, size_t mem_size, uint16_t ds, uint16_t bank_segment,
                       uint16_t model, const int16_t position[3], remaster_frame *f)
{
    const size_t ds_at = (size_t)ds << 4, bank_at = (size_t)bank_segment << 4;
    if (ds_at + SCENE_SEGMENT > mem_size || bank_at + SCENE_SEGMENT > mem_size) return -1;
    const uint8_t *dsp = mem + ds_at, *bank = mem + bank_at;
    int16_t camera[9];
    for (int k = 0; k < 9; k++) camera[k] = (int16_t)(dsp[0x7B28 + 2 * k] | dsp[0x7B28 + 2 * k + 1] << 8);
    const uint16_t depth = scene_model_depth(position, camera);
    const scene_mesh *mesh = lookup(s, bank, bank_signature(bank), model, depth);
    if (!mesh) return -1;
    if (!scene_pose_read(&s->pose, mesh, dsp, SCENE_SEGMENT)) return -1;
    if (!scene_mesh_eval(mesh, &s->pose, &s->geometry)) return -1;
    const size_t frame_start = f->n;
    emit_ctx ctx = { mesh, &s->geometry, f, model, 0, 1 };
    if (!scene_mesh_materials(mesh, &s->pose, &s->geometry, emit, &ctx)) {
        f->n = frame_start;
        return -1;
    }
    /* the fill's colour rule: a word FFxx with xx >= A0 is darkened by DS:48C4, but not below its band (xx & F0) */
    const uint8_t darken = dsp[0x48C4];
    for (size_t k = frame_start; k < f->n; k++) {
        remaster_prim *p = &f->prim[k];
        uint8_t al = (uint8_t)p->colour_word;
        if ((p->colour_word >> 8) == 0xFF && al >= 0xA0) {
            const uint8_t base = al & 0xF0;
            al = (uint8_t)(al - darken);
            if (!(al > base)) al = base;                  /* unsigned, as the original's JA: a borrow wraps and stays */
        }
        p->pixel = al;
        p->style = (uint8_t)(p->colour_word >> 8);
        p->pixel2 = p->style;
    }
    if (getenv("F117_RM_MESH") && (ctx.added == 0 || getenv("F117_RM_MESH")[0] == '2'))
        fprintf(stderr, "mesh model %u depth %u parts %u vertices %u planes %u edges %u faces %u raw_planes %d added %d\n", model, depth, mesh->nparts,
                mesh->nvertices, mesh->nplanes, mesh->nedges, mesh->nfaces, (int16_t)mesh->raw_planes, ctx.added);
    if (getenv("F117_RM_MESH") && ctx.added == 0) {
        fprintf(stderr, "  stream @%04X:", mesh->face[0]);
        for (unsigned k = 0; k < 28; k++) fprintf(stderr, " %02X", mesh->bank[(mesh->face[0] + k) & 0xFFFF]);
        fprintf(stderr, "  settings 0..7:");
        for (int k = 0; k < 8; k++) fprintf(stderr, " %d", s->pose.setting[k]);
        fprintf(stderr, "\n");
    }
    const uint32_t request = ++s->requests;
    int32_t zkey = 0;
    for (size_t k = frame_start; k < f->n; k++)
        for (int j = 0; j < f->prim[k].n; j++) if (f->prim[k].v[j].z > zkey) zkey = f->prim[k].v[j].z;
    for (size_t k = frame_start; k < f->n; k++) { f->prim[k].request = request; f->prim[k].zkey = zkey; }
    return ctx.added;
}
