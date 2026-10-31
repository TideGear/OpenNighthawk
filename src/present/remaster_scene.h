/* remaster_scene.h - from a model request to camera-space primitives (docs/remastered-design.md, stage R3).
 *
 * For one model request the game has prepared a pose in its data segment. Given the guest's memory, the data segment
 * and the request (model, bank segment, position words: observe_pose.h), this picks the detail level the game picks
 * (scene_model_depth, from the position and the camera matrix at DS:7B28), decodes the model's whole geometry from its
 * bank (kept in a cache), reads the pose where it lies (scene_pose_read), evaluates every vertex in camera space with the
 * game's own integer arithmetic (scene_mesh_eval), and runs the model's material commands (scene_mesh_materials) to
 * get the polygons, lines and dots with their colour word and shade. A polygon is a chain of edges; its vertices
 * come out in order. Nothing here changes the guest.
 *
 * The bank is recognised by a signature of its content (a sample of the 64 KB page), so a change of theatre starts a new
 * cache without the caller saying so. */
#ifndef F117R_REMASTER_SCENE_H
#define F117R_REMASTER_SCENE_H

#include "remaster_raster.h"

#include <stddef.h>
#include <stdint.h>

#define REMASTER_PRIM_VERTICES 16

typedef struct {
    int kind;                               /* SCENE_POLYGON, SCENE_LINES or SCENE_DOTS (scene_mesh.h) */
    int n;                                  /* vertices in v */
    remaster_v3 v[REMASTER_PRIM_VERTICES];  /* camera space, the game's units */
    uint8_t colour_index;
    uint16_t colour_word;                   /* the model's palette entry for the colour index */
    uint8_t shade;                          /* the face's shade (the plane's, or the model's base) */
    uint16_t face;
    uint16_t model;
    uint8_t pixel;                          /* the palette index the original's fill ends with (VGAME 1377:01E0) */
} remaster_prim;

typedef struct {
    remaster_prim *prim;
    size_t n, cap;
} remaster_frame;

void remaster_frame_clear(remaster_frame *f);
void remaster_frame_free(remaster_frame *f);

typedef struct remaster_scene remaster_scene;
remaster_scene *remaster_scene_new(void);
void remaster_scene_free(remaster_scene *s);

/* Add one request's primitives to f. mem is the guest's memory (mem_size bytes: a megabyte); ds and bank_segment are
 * segments in it. Returns the number added, or -1 when the request cannot be read (a data segment or bank outside the
 * memory, an undecodable model, a pose the decoder refuses). */
int remaster_scene_add(remaster_scene *s, const uint8_t *mem, size_t mem_size, uint16_t ds, uint16_t bank_segment,
                       uint16_t model, const int16_t position[3], remaster_frame *f);

#endif
