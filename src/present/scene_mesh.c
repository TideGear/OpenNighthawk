#include "scene_mesh.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *data; uint16_t at; unsigned used; int bad; } reader;
static uint8_t byte(reader *r)
{
    if (r->used >= SCENE_SEGMENT) { r->bad = 1; return 0; }
    r->used++;
    return r->data[r->at++];
}
static uint16_t word(reader *r) { const uint16_t lo = byte(r); return (uint16_t)(lo | (uint16_t)byte(r) << 8); }
static uint16_t peek(const uint8_t *p, unsigned at) { return (uint16_t)(p[(uint16_t)at] | (uint16_t)p[(uint16_t)(at + 1)] << 8); }
static int16_t s16(uint16_t x) { return (int16_t)(x < 32768 ? (int)x : (int)x - 65536); }
static int32_t s32(uint32_t x) { return x <= INT32_MAX ? (int32_t)x : -1 - (int32_t)(UINT32_MAX - x); }
static int32_t dword(const uint8_t *p, unsigned at) { return s32(peek(p, at) | (uint32_t)peek(p, at + 2) << 16); }
static unsigned magnitude(uint16_t x) { const int n = s16(x); return (unsigned)(n < 0 ? -n : n); }

const char *scene_error_name(scene_error e)
{
    static const char *const names[] = { "ok", "invalid input", "invalid model format", "unproven model format", "out of memory" };
    return (unsigned)e < sizeof names / sizeof names[0] ? names[e] : "unknown error";
}
void scene_mesh_free(scene_mesh *m)
{
    if (!m) return;
    free(m->edge); free(m->condition); free(m->indices); free(m);
}
uint16_t scene_model_depth(const int16_t position[3], const int16_t camera[9])
{
    uint32_t z = 0;
    for (int k = 0; k < 3; k++) z += (uint32_t)((int32_t)position[k] * camera[3 * k + 2]);
    return (uint16_t)magnitude((uint16_t)(z >> 16));
}
static int valid_vertex(const scene_mesh *m, unsigned v)
{
    if (!((unsigned)m->raw_vertices & 0x8000)) return v < m->nvertices;
    for (unsigned k = 0; k < m->nvertices; k++) if (m->vertex[k].shared_index == v) return 1;
    return 0;
}
/* Validate all linear commands without choosing frame-dependent settings.
 * Runtime branching below reads the retained bytecode itself. */
static int validate_commands(const scene_mesh *m, uint16_t at)
{
    reader r = { m->bank, at, 0, 0 };
    while (!r.bad) {
        const uint8_t op = byte(&r);
        if (op == 255) return !r.bad;
        if (op & 128) { byte(&r); continue; }
        const unsigned n = op & 31;
        if (!n) return 0;
        if (op & 96) byte(&r);
        for (unsigned k = 0; k < n; k++) {
            const unsigned item = byte(&r);
            if ((op & 32) ? !valid_vertex(m, item) : item >= m->nedges) return 0;
        }
        if (!(op & 96)) byte(&r);
    }
    return 0;
}

scene_mesh *scene_mesh_decode(const uint8_t *bank, size_t size, uint16_t model, uint16_t depth, scene_error *error)
{
    scene_error e = SCENE_INPUT;
    scene_mesh *m = NULL;
    if (!bank || size != SCENE_SEGMENT || depth > 32768 || model >= peek(bank, 0)) goto fail;
    m = (scene_mesh *)calloc(1, sizeof *m);
    if (!m) { e = SCENE_MEMORY; goto fail; }
    memcpy(m->bank, bank, SCENE_SEGMENT);
    m->bank_key = UINT64_C(14695981039346656037);
    for (unsigned k = 0; k < SCENE_SEGMENT; k++) m->bank_key = (m->bank_key ^ bank[k]) * UINT64_C(1099511628211);
    m->model = model;
    reader r = { m->bank, peek(bank, 2u + 2u * model), 0, 0 };
    for (;;) {
        const uint16_t range = word(&r), offset = word(&r);
        if (r.bad) { e = SCENE_FORMAT; goto fail; }
        if (depth <= range) { m->lod = offset; break; }
    }
    r.at = m->lod; r.used = 0;
    m->own_range = word(&r); m->raw_parts = word(&r); m->prefix[0] = r.at;
    m->nparts = s16(m->raw_parts) > 0 ? m->raw_parts : 0;
    if (m->nparts > SCENE_PARTS) { e = SCENE_UNSUPPORTED; goto fail; }
    for (unsigned k = 1; k < m->nparts; k++) { m->part[k].angle = byte(&r); m->part[k].axis = byte(&r); }
    for (unsigned k = 1; k < m->nparts; k++) for (int j = 0; j < 3; j++) m->part[k].offset[j] = s16(word(&r));
    m->raw_vertices = word(&r); m->nvertices = magnitude(m->raw_vertices); m->prefix[1] = r.at;
    m->raw_planes = word(&r); m->nplanes = magnitude(m->raw_planes);
    if (m->nvertices > SCENE_VERTICES || m->nplanes > SCENE_PLANES) { e = SCENE_UNSUPPORTED; goto fail; }
    /* Both variable lists are bounded by the original segment budget. */
    m->condition = (scene_condition *)calloc(SCENE_SEGMENT / 4, sizeof *m->condition);
    m->indices = (uint8_t *)malloc(SCENE_SEGMENT);
    if (!m->condition || !m->indices) { e = SCENE_MEMORY; goto fail; }
    unsigned part = 0, condition_first = 0;
    for (unsigned k = 0; k < m->nplanes;) {
        const uint16_t start = r.at, a = word(&r);
        if (r.bad) { e = SCENE_FORMAT; goto fail; }
        if ((a & 0xFF00) == 0x8000) { part = a & 255; continue; }
        if (a == 0x8100) {
            if (m->nconditions >= SCENE_SEGMENT / 4) { e = SCENE_FORMAT; goto fail; }
            scene_condition *c = &m->condition[m->nconditions++];
            c->setting = byte(&r);
            const uint8_t raw = byte(&r); c->expected = (int8_t)(raw < 128 ? (int)raw : (int)raw - 256);
            continue;
        }
        if (part >= (m->nparts ? m->nparts : 1)) { e = SCENE_UNSUPPORTED; goto fail; }
        scene_plane *p = &m->plane[k++];
        p->offset = start; p->part = (uint8_t)part;
        p->normal[0] = s16(a); p->normal[1] = s16(word(&r)); p->normal[2] = s16(word(&r));
        const uint32_t low = word(&r); p->d = s32(low | (uint32_t)word(&r) << 16);
        p->condition_first = condition_first; p->condition_count = m->nconditions - condition_first;
        condition_first = m->nconditions;
    }
    for (unsigned k = 0; k < m->nplanes; k++) {
        scene_plane *p = &m->plane[k]; p->vertex_first = m->nindices; p->vertex_count = byte(&r);
        if (m->nindices + p->vertex_count > SCENE_SEGMENT) { e = SCENE_FORMAT; goto fail; }
        for (unsigned j = 0; j < p->vertex_count; j++) m->indices[m->nindices++] = byte(&r);
    }
    if (s16(m->raw_planes) < 0) {
        for (unsigned k = 0; k < 2 * m->nplanes + 1; k++) m->order[k] = byte(&r);
        if (m->order[0] >= m->nplanes) { e = SCENE_FORMAT; goto fail; }
        for (unsigned k = 1; k < 2 * m->nplanes + 1; k++)
            if (m->order[k] != 255 && m->order[k] >= m->nplanes) { e = SCENE_UNSUPPORTED; goto fail; }
    }
    m->prefix[2] = m->pointers[0] = r.at; part = 0;
    for (unsigned k = 0; k < m->nvertices;) {
        const uint16_t start = r.at;
        if (s16(m->raw_vertices) < 0) {
            scene_vertex *v = &m->vertex[k++]; v->offset = start; v->shared = 1; v->shared_index = byte(&r);
        } else {
            const uint16_t x = word(&r);
            if (r.bad) { e = SCENE_FORMAT; goto fail; }
            if (x >= 0x8000 && x < 0x8010) { part = x & 15; continue; }
            if (part >= (m->nparts ? m->nparts : 1)) { e = SCENE_UNSUPPORTED; goto fail; }
            scene_vertex *v = &m->vertex[k++]; v->offset = start; v->part = (uint8_t)part;
            v->xyz[0] = s16(x); v->xyz[1] = s16(word(&r)); v->xyz[2] = s16(word(&r));
        }
    }
    m->pointers[1] = r.at; m->nedges = word(&r);
    if (m->nedges) {
        m->edge = (scene_edge *)calloc(m->nedges, sizeof *m->edge);
        if (!m->edge) { e = SCENE_MEMORY; goto fail; }
    }
    for (unsigned k = 0; k < m->nedges; k++) { m->edge[k].a = byte(&r); m->edge[k].b = byte(&r); }
    m->pointers[2] = r.at; m->nfaces = m->nplanes ? m->nplanes : 1;
    if (m->nplanes) for (unsigned k = 0; k < m->nfaces; k++) m->face[k] = word(&r);
    else m->face[0] = r.at;
    if (r.bad) { e = SCENE_FORMAT; goto fail; }
    for (unsigned k = 0; k < m->nedges; k++)
        if (!valid_vertex(m, m->edge[k].a) || !valid_vertex(m, m->edge[k].b)) { e = SCENE_FORMAT; goto fail; }
    for (unsigned k = 0; k < m->nindices; k++) if (!valid_vertex(m, m->indices[k])) { e = SCENE_FORMAT; goto fail; }
    for (unsigned k = 0; k < m->nfaces; k++) if (!validate_commands(m, m->face[k])) { e = SCENE_FORMAT; goto fail; }
    if (error) *error = SCENE_OK;
    return m;
fail:
    scene_mesh_free(m);
    if (error) *error = e;
    return NULL;
}

int scene_pose_read(scene_pose *p, const scene_mesh *m, const uint8_t *ds, size_t size)
{
    if (!p || !m || !ds || size != SCENE_SEGMENT) return 0;
    memset(p, 0, sizeof *p); p->nparts = m->nparts ? m->nparts : 1;
    p->ortho = ds[0x7D8E]; p->base_shade = ds[0x7D8A];
    const unsigned base = peek(ds, 0x7D50), settings = peek(ds, 0x7D4E), palette = peek(ds, 0x7D80);
    for (unsigned k = 0; k < p->nparts; k++) {
        scene_part_pose *part = &p->part[k];
        for (int j = 0; j < 9; j++) part->matrix[j] = s16(peek(ds, base + 36 * k + 2 * j));
        for (int j = 0; j < 3; j++) {
            part->translation[j] = dword(ds, (m->nparts ? 0x7CDE + 12 * k : 0x7CD2) + 4 * j);
            part->eye[j] = s16(peek(ds, 0x7C6C + 6 * k + 2 * j));
            part->light[j] = s16(peek(ds, 0x7C9C + 6 * k + 2 * j));
        }
    }
    for (unsigned k = 0; k < 128; k++) p->setting[k] = s16(peek(ds, settings + 2 * k));
    for (unsigned k = 0; k < 256; k++) p->palette[k] = peek(ds, palette + 2 * k);
    for (unsigned k = 0; k < m->nvertices; k++) if (m->vertex[k].shared) {
        const unsigned id = m->vertex[k].shared_index; p->shared_present[id] = 1;
        for (int j = 0; j < 3; j++) p->shared_camera[id][j] = dword(ds, 0x4EE6 + 16 * id + 4 * j);
    }
    return 1;
}

int scene_mesh_eval(const scene_mesh *m, const scene_pose *p, scene_geometry *g)
{
    if (!m || !p || !g || !p->nparts || p->nparts > SCENE_PARTS) return 0;
    memset(g, 0, sizeof *g);
    for (unsigned k = 0; k < m->nvertices; k++) {
        const scene_vertex *v = &m->vertex[k];
        if (v->part >= p->nparts || (v->shared && !p->shared_present[v->shared_index])) return 0;
        const scene_part_pose *part = &p->part[v->part];
        const unsigned id = v->shared ? v->shared_index : k; g->present[id] = 1;
        for (int j = 0; j < 3; j++) {
            uint32_t value = (uint32_t)part->translation[j];
            if (v->shared) value += (uint32_t)p->shared_camera[id][j];
            else for (int row = 0; row < 3; row++) value += (uint32_t)((int32_t)v->xyz[row] * part->matrix[3 * row + j]);
            g->camera[id][j] = s32(value);
        }
        if (!m->nplanes || v->shared) g->projected[id] = 1;
    }
    for (unsigned k = 0; k < m->nplanes; k++) {
        const scene_plane *plane = &m->plane[k];
        if (plane->part >= p->nparts) return 0;
        const scene_part_pose *part = &p->part[plane->part];
        uint32_t facing = (uint32_t)plane->d, light = 0; unsigned mask = 255;
        for (unsigned j = 0; j < plane->condition_count; j++) {
            const scene_condition *c = &m->condition[plane->condition_first + j];
            if (p->setting[c->setting & 31] != c->expected) mask = 127;
        }
        for (int j = 0; j < 3; j++) {
            facing += (uint32_t)((int32_t)plane->normal[j] * part->eye[j]);
            light += (uint32_t)((int32_t)plane->normal[j] * part->light[j]);
        }
        g->visibility[k] = (uint8_t)((p->ortho || s32(facing) >= 0 ? 255 : 0) & mask);
        g->shade[k] = (uint8_t)((light * 4u) >> 16);
        if (g->visibility[k] > 127) for (unsigned j = 0; j < plane->vertex_count; j++)
            g->projected[m->indices[plane->vertex_first + j]] = 1;
    }
    return 1;
}

int scene_mesh_order(const scene_mesh *m, const scene_geometry *g, uint16_t *faces, unsigned capacity, unsigned *count)
{
    uint16_t result[SCENE_PLANES]; unsigned n = 0;
    if (count) *count = 0;
    if (!m || !g || !faces || !count) return 0;
    if (s16(m->raw_planes) >= 0) {
        for (unsigned k = 0; k < m->nfaces; k++) if (!m->nplanes || g->visibility[k] > 127) result[n++] = (uint16_t)k;
    } else {
        struct { uint16_t node; uint8_t step; } stack[SCENE_PLANES];
        uint8_t active[SCENE_PLANES] = {0}; unsigned depth = 1;
        stack[0].node = m->order[0]; stack[0].step = 0; active[stack[0].node] = 1;
        while (depth) {
            const unsigned node = stack[depth - 1].node, step = stack[depth - 1].step++;
            if (step == 3) { active[node] = 0; depth--; continue; }
            const int front = g->visibility[node] == 0;
            if (step == 1) {
                if (g->visibility[node] > 127) { if (n >= SCENE_PLANES) return 0; result[n++] = (uint16_t)node; }
                continue;
            }
            const unsigned side = step == 0 ? (unsigned)!front : (unsigned)front;
            const unsigned child = m->order[1 + 2 * node + side];
            if (child == 255) continue;
            if (child >= m->nplanes || active[child] || depth >= SCENE_PLANES) return 0;
            active[child] = 1; stack[depth].node = (uint16_t)child; stack[depth++].step = 0;
        }
    }
    if (n > capacity) return 0;
    memcpy(faces, result, n * sizeof *faces); *count = n; return 1;
}

int scene_mesh_materials(const scene_mesh *m, const scene_pose *p, const scene_geometry *g, scene_material_fn emit, void *user)
{
    uint16_t faces[SCENE_PLANES]; unsigned count;
    if (!p || !emit || !scene_mesh_order(m, g, faces, SCENE_PLANES, &count)) return 0;
    for (unsigned face = 0; face < count; face++) {
        reader r = { m->bank, m->face[faces[face]], 0, 0 };
        while (!r.bad) {
            scene_material command = {0}; command.offset = r.at; command.face = faces[face];
            const uint8_t op = byte(&r);
            if (op == 255) break;
            if (op & 128) {
                const uint8_t raw = byte(&r);
                const int expected = raw < 128 ? raw : (int)raw - 256;
                if (p->setting[op & 127] != expected) {
                    const uint8_t skip = byte(&r);
                    if (skip == 255) break;
                    for (unsigned k = 0; k < (unsigned)(skip & 31) + 1; k++) byte(&r);
                }
                continue;
            }
            command.count = op & 31;
            if (!command.count) return 0;
            command.kind = op & 32 ? SCENE_DOTS : op & 64 ? SCENE_LINES : SCENE_POLYGON;
            if (command.kind != SCENE_POLYGON) command.colour_index = byte(&r);
            for (unsigned k = 0; k < command.count; k++) {
                command.item[k] = byte(&r);
                if (command.kind == SCENE_DOTS ? !valid_vertex(m, command.item[k]) : command.item[k] >= m->nedges) return 0;
            }
            if (command.kind == SCENE_POLYGON) command.colour_index = byte(&r);
            if (r.bad) return 0;
            if (command.kind == SCENE_POLYGON && command.colour_index == 255) continue;
            command.colour_word = p->palette[command.colour_index];
            command.shade = m->nplanes ? g->shade[command.face] : p->base_shade;
            if (!emit(user, &command)) return 0;
        }
        if (r.bad) return 0;
    }
    return 1;
}
