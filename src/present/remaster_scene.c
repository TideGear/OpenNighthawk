/* remaster_scene.c - from a model request to camera-space primitives (see remaster_scene.h). */
#include "remaster_scene.h"

#include "scene_mesh.h"

#include <stdio.h>
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
    p->face = c->face;
    p->model = x->model;
    if (c->kind == SCENE_DOTS) {                              /* the items are vertices */
        for (unsigned k = 0; k < c->count && p->n < REMASTER_PRIM_VERTICES; k++) {
            if (!x->geometry->present[c->item[k]]) return 1;  /* a vertex the game did not make: skip the dot set */
            put_vertex(p, x->geometry, c->item[k]);
        }
    } else {                                                  /* the items are edges: a chain of vertices */
        unsigned cur = 0;
        for (unsigned k = 0; k < c->count; k++) {
            const scene_edge *e = &x->mesh->edge[c->item[k]];
            if (!x->geometry->present[e->a] || !x->geometry->present[e->b]) return 1;
            if (k == 0) { put_vertex(p, x->geometry, e->a); put_vertex(p, x->geometry, e->b); cur = e->b; }
            else if (e->a == cur) { if (p->n < REMASTER_PRIM_VERTICES) put_vertex(p, x->geometry, e->b); cur = e->b; }
            else if (e->b == cur) { if (p->n < REMASTER_PRIM_VERTICES) put_vertex(p, x->geometry, e->a); cur = e->a; }
            else return 1;                                    /* not a chain: leave it out */
        }
        if (c->kind == SCENE_POLYGON && p->n > 2 && x->geometry->camera[cur][0] == p->v[0].x &&
            x->geometry->camera[cur][1] == p->v[0].y && x->geometry->camera[cur][2] == p->v[0].z)
            p->n--;                                           /* a closed chain repeats its first vertex */
    }
    x->f->n++;
    x->added++;
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
