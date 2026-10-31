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

typedef struct {
    int w, h;
    uint32_t *color;            /* w * h */
    float *inv_z;               /* w * h: 1/z of the nearest thing drawn, 0 where nothing is */
    double cx, cy;              /* the view's centre in pixels */
    double sx, sy;              /* pixels per unit of x / z and y / z: 256 for the game's own 320 x 200 pixels */
    int32_t near_z;             /* nearer than this is clipped (the game's own smallest z is 0x10000) */
    uint32_t *gen;              /* optional, w * h: which draw group last wrote each pixel's depth */
    uint32_t cur_gen;           /* with gen set, depth is compared only against the same group: groups paint in order (the
                                   original's painter order across models), depth orders the faces within one */
} remaster_target;

/* Clear colour and depth. */
void remaster_clear(remaster_target *t, uint32_t color);

/* Fill a convex polygon of n vertices (n >= 3) in one colour; returns the pixels written. */
long remaster_polygon(remaster_target *t, const remaster_v3 *v, int n, uint32_t color);

/* A line between two camera-space points, width pixels wide, drawn a hair nearer than a surface at the same depth;
 * returns the pixels written. */
long remaster_line(remaster_target *t, remaster_v3 a, remaster_v3 b, double width, uint32_t color);

#endif
