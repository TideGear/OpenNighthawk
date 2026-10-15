/* Complete original model geometry, independent of guest execution and
 * visible draw-list storage. Immutable decoded meshes own their inputs;
 * prepared poses and evaluation results belong to a presentation frame.
 * This module does not change simulation, submit pixels or identify world
 * instances. Original part preparation and render styles remain upstream. */
#ifndef F117R_SCENE_MESH_H
#define F117R_SCENE_MESH_H

#include <stddef.h>
#include <stdint.h>

#define SCENE_SEGMENT 65536
#define SCENE_VERTICES 256
#define SCENE_PLANES 256
#define SCENE_PARTS 16

typedef enum { SCENE_OK, SCENE_INPUT, SCENE_FORMAT, SCENE_UNSUPPORTED, SCENE_MEMORY } scene_error;
const char *scene_error_name(scene_error error);

typedef struct { uint8_t angle, axis; int16_t offset[3]; } scene_part;
typedef struct { uint16_t offset; uint8_t part, shared, shared_index; int16_t xyz[3]; } scene_vertex;
typedef struct { uint8_t setting; int8_t expected; } scene_condition;
typedef struct {
    uint16_t offset;
    uint8_t part;
    int16_t normal[3];
    int32_t d;
    unsigned condition_first, condition_count, vertex_first, vertex_count;
} scene_plane;
typedef struct { uint8_t a, b; } scene_edge;

typedef struct {
    uint64_t bank_key;
    uint16_t model, lod, own_range, raw_parts, raw_vertices, raw_planes;
    uint16_t prefix[3], pointers[3];
    unsigned nparts, nvertices, nplanes, nedges, nfaces, nconditions, nindices;
    scene_part part[SCENE_PARTS];
    scene_vertex vertex[SCENE_VERTICES];
    scene_plane plane[SCENE_PLANES];
    scene_edge *edge;
    scene_condition *condition;
    uint8_t *indices;
    uint8_t order[2 * SCENE_PLANES + 1];
    uint16_t face[SCENE_PLANES];
    /* Original material bytecode: retained for conditional paths that do
     * not follow the linear decoding order. No borrowed guest pointers. */
    uint8_t bank[SCENE_SEGMENT];
} scene_mesh;

uint16_t scene_model_depth(const int16_t position[3], const int16_t camera[9]);
scene_mesh *scene_mesh_decode(const uint8_t *bank, size_t size, uint16_t model, uint16_t depth, scene_error *error);
void scene_mesh_free(scene_mesh *mesh);

typedef struct {
    int16_t matrix[9], eye[3], light[3];
    int32_t translation[3];
} scene_part_pose;
typedef struct {
    unsigned nparts;
    uint8_t ortho, base_shade;
    scene_part_pose part[SCENE_PARTS];
    int16_t setting[128];
    uint16_t palette[256];
    int32_t shared_camera[SCENE_VERTICES][3];
    uint8_t shared_present[SCENE_VERTICES];
} scene_pose;
typedef struct {
    int32_t camera[SCENE_VERTICES][3];
    uint8_t present[SCENE_VERTICES], projected[SCENE_VERTICES];
    uint8_t visibility[SCENE_PLANES], shade[SCENE_PLANES];
} scene_geometry;

/* Load a compact pose from an original DS snapshot AFTER part preparation.
 * The input is private captured memory, never a pointer to live devices. */
int scene_pose_read(scene_pose *pose, const scene_mesh *mesh, const uint8_t *prepared, size_t size);
int scene_mesh_eval(const scene_mesh *mesh, const scene_pose *pose, scene_geometry *geometry);
/* Original painter order, including the nonzero-versus-visible distinction
 * in conditional plane states. No output is committed on failure. */
int scene_mesh_order(const scene_mesh *mesh, const scene_geometry *geometry, uint16_t *faces, unsigned capacity, unsigned *count);

enum { SCENE_POLYGON, SCENE_LINES, SCENE_DOTS };
typedef struct {
    uint16_t offset, face, colour_word;
    uint8_t kind, colour_index, shade, count, item[31];
} scene_material;
typedef int (*scene_material_fn)(void *user, const scene_material *command);
/* Execute original setting branches and FF polygon suppression. The
 * callback receives semantic commands, not raster styles or pixels. It
 * can receive a prefix before invalid data fails: collect before painting. */
int scene_mesh_materials(const scene_mesh *mesh, const scene_pose *pose, const scene_geometry *geometry,
                         scene_material_fn emit, void *user);

#endif
