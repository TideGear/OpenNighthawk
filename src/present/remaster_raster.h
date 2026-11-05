/* remaster_raster.h - the Remastered presentation's triangle fill (docs/remastered-design.md, stage R3).
 *
 * Polygons arrive in camera space as the game makes them (three 32-bit coordinates; remaster_project.h) and are
 * drawn at the output's own resolution: vertices projected without the game's whole-pixel truncation, each pixel
 * centre tested against the triangle, depth kept per pixel as 1/z (it interpolates linearly across the screen), so
 * a nearer face covers a farther one whatever order they are drawn in. That is what the original's whole-object
 * sort cannot promise. Parts behind the near plane are clipped away.
 *
 * Scales: the game's pixels are not square (320 x 200 shown at 4:3), so a target has a scale for each axis; for an
 * output H high and W wide that shows the original's frame at 4:3 they are 256 H / 240 across and 256 H / 200 up. A
 * wider frame (16:9, say) keeps both scales and has more columns: the picture grows at its sides and nothing that
 * was drawn moves. */
#ifndef F117R_REMASTER_RASTER_H
#define F117R_REMASTER_RASTER_H

#include <stdint.h>

typedef struct { int32_t x, y, z; } remaster_v3;

/* One quarter of a world unit, expressed in the current terrain ring's
 * camera units. A fixed native near plane opens a larger ground gap at
 * every coarser ring, especially when the camera sits on the surface. */
static inline double remaster_ground_near_z(double inv_depth_scale)
{ return 64.0 * (inv_depth_scale > 0.0 ? inv_depth_scale : 1.0); }

typedef struct {
    int w, h;
    uint32_t *color;            /* w * h */
    float *inv_z;               /* w * h: 1/z of the nearest thing drawn, 0 where nothing is */
    double cx, cy;              /* the view's centre in pixels */
    double sx, sy;              /* pixels per unit of x / z and y / z: 256 for the game's own 320 x 200 pixels */
    int32_t near_z;             /* nearer than this is clipped (the game's own smallest z is 0x10000) */
    /* Optional, for the original's fills that combine with what is already drawn (AND, OR, stipple: remaster_polygon_logic):
     * the palette index each pixel stands for, REMASTER_NO_INDEX where it has none, and the palette to turn an index into a
     * colour. The solid fills write paint_index into index as they paint. */
    uint16_t *index;            /* w * h, or NULL */
    const uint32_t *palette;    /* 256 entries, 0xAARRGGBB */
    uint16_t paint_index;       /* what the next polygon, shaded polygon or line writes into index */
    /* Lines lie on the surface they mark, at its depth, and a constant depth bias cannot both keep them on top of a ground
     * polygon near the horizon and keep a far hangar's door lines hidden behind a nearer hangar. line_bias is the bias
     * (0: the default, 1.5). When line_strong (w * h) and line_tmp_z (w * h) are set, the scene's lines are drawn after its
     * polygons twice: with a huge bias into line_strong (every line whole), and with the default into color; the caller
     * picks per pixel, using the original's own picture to say where a line really is visible. */
    double line_bias;
    /* The rows a call may touch, [y_lo, y_hi): one worker's share when a picture is drawn by several at once (the rows are
     * independent). y_hi 0 means every row. */
    int y_lo, y_hi;
    /* The whole-line pass: a line passes the depth test where it is no farther than the surface under it by more than that
     * surface's own depth slope times line_slope (a ground polygon seen at a grazing angle changes depth a lot from one pixel to the
     * next, so its markings pass; a hangar's face is nearly flat, so a light behind it does not). line_ref_z is the polygons' depth
     * the slope is read from (the test's own buffer holds the lines drawn so far). 0: off. */
    double line_slope;
    const float *line_ref_z;
    uint32_t *line_strong;
    float *line_tmp_z;
    /* With line_order set, polygon depth stays read-only. Optional stroke buffers
     * resolve point/edge competition within an object at the point centre.
     * Other strokes retain painter order (flag over pole, ground markings). */
    int line_order;
    float *stroke_z;
    float *stroke_aux_x, *stroke_aux_y; /* edge depth gradient, or a point decoration's screen centre */
    uint32_t *stroke_owner;    /* object serial in bits 0..19, point marker in bit 20 */
    int line_point;
    int point_self_hidden;
    double line_depth_dx, line_depth_dy;
    /* Lines and dots as decals on the face they belong to. owner (w * h, or NULL) is the object and face of the polygon at each
     * pixel (paint_owner as it was painted, 0 for the sky and ground). With line_decal set a line or dot pixel is drawn over its
     * own face or another face of its own object unless that face is nearer by more than a hundredth (the original's
     * lines and dots keep one pixel's width at any distance, so they spill onto the faces beside theirs); over the ground
     * a hangar stands on; and over another object only unless it is nearer by more than three hundredths. */
    uint32_t *owner;
    uint32_t paint_owner;
    int line_decal;
    double inv_depth_scale;    /* convert native model inverse depth to common world units; 0 means 1 */
    double line_screen[4];     /* centre segment for conservative decal stroke coverage */
    int line_radius;
} remaster_target;

static inline int remaster_row_lo(const remaster_target *t) { return t->y_hi > 0 ? t->y_lo : 0; }
static inline int remaster_row_hi(const remaster_target *t) { return t->y_hi > 0 ? t->y_hi : t->h; }

#define REMASTER_NO_INDEX 0xFFFFu

enum { REMASTER_AND, REMASTER_OR, REMASTER_STIPPLE };

/* A polygon that combines with the pixels under it, as the original's fill styles FEh (AND), FDh (OR) and FCh (stipple) do with
 * the low byte of the colour word: the pixel's palette index is ANDed or ORed with `value`, and the colour becomes that palette
 * entry; a stipple blends the pixel half way towards palette[value]. It applies where nothing nearer is drawn, and leaves the
 * depth alone, so what is drawn later still covers it. A pixel with no index is left as it is (AND, OR). */
long remaster_polygon_logic(remaster_target *t, const remaster_v3 *v, int n, int op, unsigned value);

/* Clear colour and depth. */
void remaster_clear(remaster_target *t, uint32_t color);

/* Fill a convex polygon of n vertices (n >= 3) in one colour; returns the pixels written. */
long remaster_polygon(remaster_target *t, const remaster_v3 *v, int n, uint32_t color);

/* A convex polygon with a colour at each vertex (0xAARRGGBB), interpolated across it (the sky and ground gradients),
 * with a hair of ordered noise so an 8-bit result shows no steps of its own. Every vertex must be in front of the near
 * plane (a far-away polygon, as the bands are); otherwise nothing is drawn. Same depth test as remaster_polygon. */
long remaster_polygon_shaded(remaster_target *t, const remaster_v3 *v, const uint32_t *colors, int n);

/* A line from a to b, width_px wide on the screen (a point when a == b: a square of that side), with the same depth test
 * and near-plane clip as the polygons. Returns the pixels written. */
long remaster_line(remaster_target *t, remaster_v3 a, remaster_v3 b, double width_px, uint32_t color);

/* Tracing (docs/gpu-compositor.md): when remaster_trace_hook is set, each call above reports itself first, with its arguments and the
 * target's state, so a frame's drawing can be recorded and replayed (the GPU compositor's oracle). NULL: one pointer test per call. */
enum { REMASTER_T_CLEAR, REMASTER_T_POLYGON, REMASTER_T_SHADED, REMASTER_T_LOGIC, REMASTER_T_LINE };
extern void (*remaster_trace_hook)(int kind, const remaster_target *t, const remaster_v3 *v, int n, const uint32_t *colors,
                                   uint32_t color, double width, int op, unsigned value);

#endif
