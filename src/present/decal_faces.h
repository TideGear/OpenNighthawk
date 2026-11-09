/* Exact face geometry for decal visibility. Words are shared with strokes_x.hlsl:
 * four header words, 262144 face-list heads, then 26 words per triangle.
 * Scratch belongs to one compositor/CPU part, is reused across frames, and is
 * never shared while being written. Allocation failure disables the new rule. */
#ifndef F117_DECAL_FACES_H
#define F117_DECAL_FACES_H
#include "exact.h"
#include <stdlib.h>
#include <string.h>
#define DF_HEADS 262144u
#define DF_STRIDE 26u
#define DF_START (4u + DF_HEADS)
typedef struct { uint32_t *words; size_t count, cap; int failed; } decal_face_cache;
static inline int decal_faces_enabled(void) {
    const char *e = getenv("F117_DECAL_FACE");
    const char *x = getenv("F117_RASTER_EXACT");
    return e && e[0] == '1' && !(x && x[0] == '0');
}
static inline int df_reserve(decal_face_cache *c, size_t count) {
    if (c->failed) return 0;
    if (c->words && count <= c->cap) return 1;
    size_t cap = c->cap ? c->cap : 1024;
    while (cap < count) cap *= 2;
    if (cap > (SIZE_MAX / 4 - DF_START) / DF_STRIDE) { c->failed = 1; return 0; }
    void *p = realloc(c->words, (DF_START + cap * DF_STRIDE) * 4);
    if (!p) { c->failed = 1; return 0; }
    c->words = (uint32_t *)p; c->cap = cap; return 1;
}
static inline void df_reset(decal_face_cache *c) {
    c->failed = 0; c->count = 0;
    if (!df_reserve(c, 1)) return;
    c->words[0] = 0x52433843u; c->words[1] = 0;
    c->words[2] = DF_HEADS; c->words[3] = DF_STRIDE;
    memset(c->words + 4, 255, DF_HEADS * 4);
}
static inline int64_t df_i64(const uint32_t *p) { int64_t v; memcpy(&v, p, 8); return v; }
static inline uint64_t df_u64(const uint32_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline void df_add(decal_face_cache *c, uint32_t owner, const ex_tri *tri, const ex_planes *plane) {
    if (c->failed || !c->words) return;
    uint32_t w[DF_STRIDE] = {0}; w[0] = owner;
    memcpy(w + 2, tri->q, 48); memcpy(w + 14, &tri->area, 8);
    memcpy(w + 16, &plane->ox, 8); memcpy(w + 18, &plane->oy, 8);
    memcpy(w + 20, &plane->base[0], 8); memcpy(w + 22, &plane->px[0], 8); memcpy(w + 24, &plane->py[0], 8);
    const uint32_t h = (owner * 2654435761u) & (DF_HEADS - 1);
    /* Tiled CPU row stages can encounter the same face more than once. */
    for (int32_t j = (int32_t)c->words[4 + h]; j >= 0;) {
        const uint32_t *r = c->words + DF_START + (size_t)j * DF_STRIDE;
        if (r[0] == owner && !memcmp(r + 2, w + 2, (DF_STRIDE - 2) * 4)) return;
        j = (int32_t)r[1];
    }
    if (c->count >= INT32_MAX || !df_reserve(c, c->count + 1)) { c->failed = 1; return; }
    w[1] = c->words[4 + h];
    memcpy(c->words + DF_START + c->count * DF_STRIDE, w, sizeof w);
    c->words[4 + h] = (uint32_t)c->count++; c->words[1] = (uint32_t)c->count;
}
static inline int df_contains(const decal_face_cache *c, uint32_t owner, double x, double y) {
    int found = 0; const double px = x * 256.0, py = y * 256.0;
    const uint32_t h = (owner * 2654435761u) & (DF_HEADS - 1);
    for (int32_t j = (int32_t)c->words[4 + h]; j >= 0;) {
        const uint32_t *r = c->words + DF_START + (size_t)j * DF_STRIDE; j = (int32_t)r[1];
        if (r[0] != owner) continue; found = 1;
        int64_t q[6]; memcpy(q, r + 2, sizeof q);
        const double e0 = (double)(q[2]-q[0])*(py-q[1])-(double)(q[3]-q[1])*(px-q[0]);
        const double e1 = (double)(q[4]-q[2])*(py-q[3])-(double)(q[5]-q[3])*(px-q[2]);
        const double e2 = (double)(q[0]-q[4])*(py-q[5])-(double)(q[1]-q[5])*(px-q[4]);
        if (df_i64(r + 14) > 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0)) return 1;
    }
    return !found;
}
static inline void df_gradient(const decal_face_cache *c, uint32_t owner, int x, int y, float depth, double *gx, double *gy) {
    double best = 1e100; *gx = *gy = 0;
    const uint32_t h = (owner * 2654435761u) & (DF_HEADS - 1);
    for (int32_t j = (int32_t)c->words[4 + h]; j >= 0;) {
        const uint32_t *r = c->words + DF_START + (size_t)j * DF_STRIDE; j = (int32_t)r[1];
        if (r[0] != owner) continue;
        ex_tri tri; memcpy(tri.q, r + 2, 48); tri.area = df_i64(r + 14);
        if (!ex_covers(&tri, x, y)) continue;
        const uint64_t zq = df_u64(r+20) + df_u64(r+22)*(uint64_t)((int64_t)x-df_i64(r+16)) + df_u64(r+24)*(uint64_t)((int64_t)y-df_i64(r+18));
        const float z = (float)((double)(int64_t)zq * EX_INV_2_52);
        const double error = fabs((double)z - depth); if (error >= best) continue; best = error;
        *gx = (double)df_i64(r+22)*EX_INV_2_52; *gy = (double)df_i64(r+24)*EX_INV_2_52;
    }
}
#endif


