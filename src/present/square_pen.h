/* R37: convex hull of the endpoint squares, shared by scene compositors. */
#ifndef F117_SQUARE_PEN_H
#define F117_SQUARE_PEN_H
#include <stdlib.h>
typedef struct { double x, y; } square_pen_point;
static inline int square_pen_enabled(void)
{
    const char *s = getenv("F117_SQUARE_PEN");
    return !s || atoi(s) != 0;
}
static inline double square_pen_cross(square_pen_point a, square_pen_point b, square_pen_point c)
{
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
static inline int square_pen_hull(double ax, double ay, double bx, double by, double half, square_pen_point *out)
{
    square_pen_point p[8];
    for (int i=0;i<8;i++) {
        p[i].x=(i<4?ax:bx)+((i&1)?half:-half);
        p[i].y=(i<4?ay:by)+((i&2)?half:-half);
    }
    for (int i=1;i<8;i++) {
        square_pen_point v=p[i]; int j=i;
        while (j && (p[j-1].x>v.x || (p[j-1].x==v.x && p[j-1].y>v.y))) { p[j]=p[j-1]; j--; }
        p[j]=v;
    }
    int n=0;
    for (int i=0;i<8;i++) {
        while (n>=2 && square_pen_cross(out[n-2],out[n-1],p[i])<=0) n--;
        out[n++]=p[i];
    }
    const int lower=n;
    for (int i=6;i>=0;i--) {
        while (n>lower && square_pen_cross(out[n-2],out[n-1],p[i])<=0) n--;
        out[n++]=p[i];
    }
    return n-1;
}
#endif
