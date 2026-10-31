/* test_remaster_scene.c - from a model request to camera-space primitives, on the synthetic bank and data segment of
 * test_scene_mesh.c placed inside a fake guest memory. */
#include "remaster_scene.h"

#include "scene_mesh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static uint8_t bank[SCENE_SEGMENT], ds[SCENE_SEGMENT];
static uint8_t mem[0x100000];
static void put(uint8_t *p, unsigned at, uint32_t v, unsigned n)
{ for (unsigned k = 0; k < n; k++) p[(uint16_t)(at + k)] = (uint8_t)(v >> (8 * k)); }
static void w(unsigned *at, int v) { put(bank, *at, (uint16_t)v, 2); *at += 2; }
static void b(unsigned *at, unsigned v) { bank[(uint16_t)(*at)++] = (uint8_t)v; }
static void fixture(int shared, int ordered)
{
    memset(bank, 0, sizeof bank); memset(ds, 0, sizeof ds);
    put(bank, 0, 1, 2); put(bank, 2, 0x100, 2);
    unsigned at = 0x100; w(&at, 5); w(&at, 0x200); w(&at, 32769); w(&at, 0x300);
    at = 0x300; w(&at, 65535); w(&at, 0); w(&at, 0); w(&at, 0); w(&at, 0); b(&at, 255);
    at = 0x200; w(&at, 65535);
    if (shared) {
        w(&at, 0); w(&at, -2); w(&at, 0); b(&at, 5); b(&at, 7);
        w(&at, 1); b(&at, 5); b(&at, 7);
        b(&at, 0x41); b(&at, 1); b(&at, 0); b(&at, 0x21); b(&at, 2); b(&at, 7); b(&at, 255);
        const int32_t xyz[2][3] = { {-10,20,30}, {40,-50,60} };
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 3; j++) put(ds, 0x4EE6 + 16u * (i ? 7 : 5) + 4u * j, (uint32_t)xyz[i][j], 4);
            put(ds, 0x4EE6 + 16u * (i ? 7 : 5) + 12, 0xBAD0BAD0, 4);
        }
    } else {
        w(&at, 2); b(&at, 3); b(&at, 1); w(&at, 10); w(&at, 20); w(&at, 30);
        w(&at, 2); w(&at, ordered ? -1 : 1); w(&at, 0x8001); w(&at, 0x8100); b(&at, 0x21); b(&at, 255);
        w(&at, 1); w(&at, 0); w(&at, 0); w(&at, 0); w(&at, 0);
        b(&at, 2); b(&at, 0); b(&at, 1);
        if (ordered) { b(&at, 0); b(&at, 255); b(&at, 255); }
        w(&at, 1); w(&at, 2); w(&at, 3); w(&at, 0x8001); w(&at, -2); w(&at, 4); w(&at, 8);
        w(&at, 1); b(&at, 0); b(&at, 1); w(&at, 0x500);
        static const uint8_t material[] = {1,0,2,0x41,3,0,0x21,4,1,0x81,255,1,0,255,255};
        memcpy(bank + 0x500, material, sizeof material);
    }
    put(ds, 0x7D50, 0x1000, 2); put(ds, 0x7D4E, 0x9000, 2); put(ds, 0x9002, 65535, 2);
    put(ds, 0x7D80, 0x9100, 2); ds[0x7D8A] = 37;
    for (unsigned k = 0; k < 256; k++) put(ds, 0x9100 + 2 * k, 0xA000 + k, 2);
    for (int k = 0; k < 3; k++) {
        put(ds, 0x1000 + 8u * k, 32767, 2); put(ds, 0x1024 + 8u * k, (unsigned)k + 1, 2);
        put(ds, 0x7CDE + 4u * k, 100u * (k + 1), 4); put(ds, 0x7CEA + 4u * k, (uint32_t)(-10 * (k + 1)), 4);
        put(ds, 0x7CD2 + 4u * k, (uint32_t)(-5 - k), 4);
    }
    put(ds, 0x7C72, (uint16_t)-2, 2); put(ds, 0x7CA2, 16384, 2);
}

int main(void)
{
    fixture(0, 0);
    ds[0x7D8E] = 21;                                         /* orthographic: every vertex is projected, as in test_scene_mesh */
    memcpy(mem + 0x20000, bank, sizeof bank);                /* the bank at segment 2000h */
    memcpy(mem + 0x40000, ds, sizeof ds);                    /* the data segment at 4000h */
    const int16_t position[3] = { 0, 0, 12 };
    remaster_scene *s = remaster_scene_new();
    remaster_frame f = { 0 };
    CHECK(s);
    /* what the pieces give directly */
    scene_error err;
    scene_mesh *m = scene_mesh_decode(bank, sizeof bank, 0, scene_model_depth(position, (const int16_t[9]){ 0 }), &err);
    CHECK(m);
    scene_pose pose;
    scene_geometry g;
    CHECK(scene_pose_read(&pose, m, ds, sizeof ds) && scene_mesh_eval(m, &pose, &g));
    /* the glue gives the same: the fixture's three material commands (a polygon, lines, dots). The fixture's "polygon"
     * is one edge, two vertices: a face needs three (the edge set's vertices, in any order), so it gives no primitive;
     * the lines command gives one segment per edge. */
    const int n = remaster_scene_add(s, mem, sizeof mem, 0x4000, 0x2000, 0, position, &f);
    CHECK(n == 2 && f.n == 2);
    CHECK(f.prim[0].kind == SCENE_LINES && f.prim[1].kind == SCENE_DOTS);
    CHECK(f.prim[1].n == 1);                                 /* the fixture's dots command names one vertex, number 1 */
    CHECK(f.prim[1].v[0].x == g.camera[1][0] && f.prim[1].v[0].y == g.camera[1][1] && f.prim[1].v[0].z == g.camera[1][2]);
    CHECK(f.prim[0].n == 2);                                  /* a segment: two vertices */
    CHECK(f.prim[0].v[0].x == g.camera[m->edge[0].a][0] && f.prim[0].v[1].x == g.camera[m->edge[0].b][0]);
    CHECK(f.prim[0].model == 0 && f.prim[0].colour_word == pose.palette[f.prim[0].colour_index]);
    /* asking again is served from the cache and gives the same */
    remaster_frame_clear(&f);
    CHECK(remaster_scene_add(s, mem, sizeof mem, 0x4000, 0x2000, 0, position, &f) == 2 && f.n == 2);
    /* a frame collects several requests */
    CHECK(remaster_scene_add(s, mem, sizeof mem, 0x4000, 0x2000, 0, position, &f) == 2 && f.n == 4);
    /* what cannot be read is refused, not guessed at */
    CHECK(remaster_scene_add(s, mem, sizeof mem, 0xF800, 0x2000, 0, position, &f) == -1);       /* a data segment past the memory */
    CHECK(remaster_scene_add(s, mem, sizeof mem, 0x4000, 0x2000, 9, position, &f) == -1);       /* a model the bank does not have */
    CHECK(f.n == 4);
    /* A malformed runtime-only conditional path must not turn emitted commands into success. */
    remaster_scene_free(s);
    s = remaster_scene_new();                                /* isolate this case from the prior cached mesh */
    CHECK(s);
    bank[0x509] = 0x82;                                   /* setting[2] mismatches its expected -1 */
    bank[0x50B] = 0x81;                                   /* linear validation treats this as a nested conditional */
    bank[0x50D] = 1; bank[0x50E] = 0; bank[0x50F] = 255; bank[0x510] = 255;
    memcpy(mem + 0x20000, bank, sizeof bank);
    const int malformed_result = remaster_scene_add(s, mem, sizeof mem, 0x4000, 0x2000, 0, position, &f);
    CHECK(malformed_result == -1);
    CHECK(f.n == 4);                                        /* preserve the already collected requests */
    scene_mesh_free(m);
    remaster_frame_free(&f);
    remaster_scene_free(s);
    puts("remaster scene passes");
    return 0;
}
