/* model_sheet.c - draw every model of a model bank into a contact sheet (Remastered stage R3, a first look at
 * depth-correct drawing; docs/remastered-design.md).
 *
 * For each model the decoder (src/present/scene_mesh.c) gives vertices (part-local, moved by their part's offset),
 * and planes that list the vertices of each face. Each face is filled as a triangle fan into a depth buffer, shaded
 * by its plane normal against a fixed light, so a nearer face always covers a farther one whatever the order they
 * come in: the property the original's whole-object sort lacks. No part articulation, no original materials.
 *
 *   model_sheet FILE.3DX OUT.ppm [yaw_deg pitch_deg [level]]
 */
#include "scene_mesh.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_W 160
#define TILE_H 120

typedef struct { float x, y, z; } v3;

static void draw_tri(uint8_t *rgb, float *zbuf, int stride, v3 a, v3 b, v3 c, const uint8_t col[3])
{
    const float minx = fminf(a.x, fminf(b.x, c.x)), maxx = fmaxf(a.x, fmaxf(b.x, c.x));
    const float miny = fminf(a.y, fminf(b.y, c.y)), maxy = fmaxf(a.y, fmaxf(b.y, c.y));
    const float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabsf(den) < 1e-6f) return;
    for (int y = (int)floorf(fmaxf(miny, 0)); y <= (int)ceilf(fminf(maxy, TILE_H - 1)); y++)
        for (int x = (int)floorf(fmaxf(minx, 0)); x <= (int)ceilf(fminf(maxx, TILE_W - 1)); x++) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float l1 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
            const float l2 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
            const float l3 = 1.0f - l1 - l2;
            if (l1 < -0.001f || l2 < -0.001f || l3 < -0.001f) continue;
            const float z = l1 * a.z + l2 * b.z + l3 * c.z;
            if (z >= zbuf[y * stride + x]) continue;                 /* nearer wins */
            zbuf[y * stride + x] = z;
            memcpy(rgb + ((size_t)y * stride + x) * 3, col, 3);
        }
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: model_sheet FILE.3DX OUT.ppm [yaw pitch [level]]\n"); return 2; }
    const float yaw = argc > 3 ? (float)atof(argv[3]) * 3.14159265f / 180 : 0.6f;
    const float pitch = argc > 4 ? (float)atof(argv[4]) * 3.14159265f / 180 : -0.35f;
    const unsigned level = argc > 5 ? (unsigned)atoi(argv[5]) : 0;
    static uint8_t bank[SCENE_SEGMENT];
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    memset(bank, 0, sizeof bank);
    fread(bank, 1, sizeof bank, f);
    fclose(f);
    const unsigned models = (bank[0] | bank[1] << 8) / 2;
    const unsigned cols = 7, rows = (models + cols - 1) / cols;
    const int W = (int)cols * TILE_W, H = (int)rows * TILE_H;
    uint8_t *sheet = (uint8_t *)calloc((size_t)W * H, 3);
    if (!sheet) return 1;
    const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    for (unsigned model = 0; model < models; model++) {
        scene_error err;
        scene_mesh *m = scene_mesh_decode(bank, sizeof bank, (uint16_t)model, (uint16_t)level, &err);
        static uint8_t tile[TILE_W * TILE_H * 3];
        static float z[TILE_W * TILE_H];
        for (int i = 0; i < TILE_W * TILE_H; i++) { z[i] = 1e30f; tile[i * 3] = 18; tile[i * 3 + 1] = 24; tile[i * 3 + 2] = 32; }
        if (m && m->nvertices) {
            static v3 p[SCENE_VERTICES];
            static uint8_t ok[SCENE_VERTICES];
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (unsigned k = 0; k < m->nvertices; k++) {
                const scene_vertex *v = &m->vertex[k];
                ok[k] = !v->shared;
                if (!ok[k]) continue;
                float q[3] = { v->xyz[0], v->xyz[1], v->xyz[2] };
                if (v->part && v->part < m->nparts) for (int j = 0; j < 3; j++) q[j] += m->part[v->part].offset[j];
                for (int j = 0; j < 3; j++) { if (q[j] < lo[j]) lo[j] = q[j]; if (q[j] > hi[j]) hi[j] = q[j]; }
                p[k] = (v3){ q[0], q[1], q[2] };
            }
            const float mid[3] = { (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2 };
            float r = 1;
            for (int j = 0; j < 3; j++) if (hi[j] - lo[j] > r) r = hi[j] - lo[j];
            const float scale = 0.46f * TILE_H / (r * 0.6f);
            static v3 s[SCENE_VERTICES];
            for (unsigned k = 0; k < m->nvertices; k++) {
                if (!ok[k]) continue;
                const float x = p[k].x - mid[0], yv = p[k].y - mid[1], zv = p[k].z - mid[2];
                const float x1 = x * cy + zv * sy, z1 = -x * sy + zv * cy;          /* yaw about the vertical */
                const float y2 = yv * cp - z1 * sp, z2 = yv * sp + z1 * cp;          /* pitch */
                s[k] = (v3){ TILE_W / 2 + x1 * scale, TILE_H / 2 - y2 * scale, z2 };
            }
            for (unsigned k = 0; k < m->nplanes; k++) {
                const scene_plane *pl = &m->plane[k];
                if (pl->vertex_count < 3) continue;
                /* the plane's normal, turned the same way as the vertices, against a light from the upper left */
                float n[3] = { pl->normal[0], pl->normal[1], pl->normal[2] };
                const float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                float shade = 0.55f;
                if (nl > 0) {
                    for (int j = 0; j < 3; j++) n[j] /= nl;
                    const float x1 = n[0] * cy + n[2] * sy, z1 = -n[0] * sy + n[2] * cy;
                    const float y2 = n[1] * cp - z1 * sp, z2 = n[1] * sp + z1 * cp;
                    shade = 0.35f + 0.65f * fmaxf(0.0f, (-0.4f * x1 + 0.7f * y2 - 0.5f * z2));
                }
                const uint8_t col[3] = { (uint8_t)(90 + 150 * shade), (uint8_t)(110 + 130 * shade), (uint8_t)(130 + 110 * shade) };
                int okf = 1;
                for (unsigned j = 0; j < pl->vertex_count; j++) okf &= ok[m->indices[pl->vertex_first + j]];
                if (!okf) continue;
                for (unsigned j = 1; j + 1 < pl->vertex_count; j++)
                    draw_tri(tile, z, TILE_W, s[m->indices[pl->vertex_first]], s[m->indices[pl->vertex_first + j]],
                             s[m->indices[pl->vertex_first + j + 1]], col);
            }
        }
        if (m) scene_mesh_free(m);
        const unsigned tx = (model % cols) * TILE_W, ty = (model / cols) * TILE_H;
        for (int y = 0; y < TILE_H; y++) memcpy(sheet + (((size_t)(ty + y)) * W + tx) * 3, tile + (size_t)y * TILE_W * 3, TILE_W * 3);
    }
    FILE *o = fopen(argv[2], "wb");
    if (!o) return 1;
    fprintf(o, "P6\n%d %d\n255\n", W, H);
    fwrite(sheet, 3, (size_t)W * H, o);
    fclose(o);
    printf("%u models drawn into %dx%d\n", models, W, H);
    return 0;
}
