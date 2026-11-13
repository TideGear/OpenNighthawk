// GPU compositor stage 4 (docs/gpu-compositor.md): the lines and dots ("strokes") of a composite, after the polygons. remaster_raster.c draws each stroke as two
// triangles under the decal rules: a stroke is hidden where the polygon pass has something clearly nearer than it (decal_hidden, decal_stroke_hidden), and
// strokes compete with one another per pixel (the stroke_z / stroke_owner / aux state). With line_order set the polygon pass's depth and owner are read-only, so
// this pass only reads them; it writes colour and palette index. Two kernels, chosen by `mode`:
//   0: one thread per stroke: a point (a dot or a zero-length line) whose own surface is nearer than it is dropped (point_behind_own_surface);
//   1: one thread per pixel: the strokes binned into the pixel's 16 x 16 tile, in the trace's order, each as two triangles, carrying the pixel's colour, index and
//      stroke state in registers. Single precision (the polygon pass's weights are evaluated in double on the CPU at each tile's first pixel).
StructuredBuffer<uint> tri : register(t0, space0);        // 12 words a triangle: a1 b1 a2 b2 ciz diz1 diz2, stroke
StructuredBuffer<uint> ents : register(t1, space0);       // 9 words per (triangle, tile): triangle, L1, L2 at the tile's first pixel centre, then the three exact edge values
StructuredBuffer<uint> tileoff : register(t2, space0);
StructuredBuffer<uint> strokes : register(t3, space0);    // 16 words a stroke
RWStructuredBuffer<uint> colour : register(u0, space1);
RWStructuredBuffer<uint> index_ : register(u1, space1);
RWStructuredBuffer<uint> invz : register(u2, space1);     // the polygon pass's depth: read only here
RWStructuredBuffer<uint> owner : register(u3, space1);    // the polygon pass's owner: read only here
RWStructuredBuffer<uint> hidden : register(u4, space1);   // per stroke: 1 when a point is behind its own surface
RWStructuredBuffer<uint> strokez : register(u5, space1);  // per pixel: the last stroke's depth (nonzero where a stroke was drawn), for the replace step


// Exact coverage (docs/gpu-compositor.md, "Step 2 design"): the CPU snaps each triangle's vertices to 1/256 pixel and sends, per triangle, the three edge
// equations' per-pixel steps as int64 (words 12 to 23) with the sign of the area (word 24), and, per (triangle, tile), the three edge values at the tile's first
// pixel centre as int64 (entry words 3 to 8). A pixel is covered when all three edge values, origin plus steps times the pixel's offset in the tile, have
// the area's sign or are zero: integers only, the decision remaster_raster.c's itri_covers makes.
int64_t i64(uint lo, uint hi) { return (int64_t)(((uint64_t)hi << 32) | (uint64_t)lo); }
uint64_t u64(uint lo, uint hi) { return ((uint64_t)hi << 32) | (uint64_t)lo; }

cbuffer params : register(b0, space2)
{
    uint mode;
    uint w;
    uint h;
    uint tilesx;
    uint nstrokes;
    uint exact;                                           // 1: integer coverage, 0: the float weights test
    uint pad1;
    uint pad2;
};

// stroke words (24 a stroke): 0 colour, 1 paint index, 2 paint owner, 3 line_point, 4 radius, 5..8 line_screen (x0 y0 x1 y1), 9 depth_dx, 10 depth_dy, 11 sa.x, 12 sa.y, 13 sa.iz,
// 16..23 the segment's ends x0 y0 x1 y1 as doubles (the decal rule's nearest point is found in double precision, as the CPU does)

bool decal_hidden(uint at, float iz, uint po, bool line_point, bool self_hidden)
{
    const uint ow = owner[at];
    const bool same = (ow >> 12) == (po >> 12);
    const bool on_ground = ow != 0 && (ow & 0x800u) != 0 && !same;
    if (same && line_point) return self_hidden;
    return !on_ground && iz < asfloat(invz[at]) * (same ? 0.99 : 0.97);
}


int64_t review_i64(uint b) {return i64(strokes[b],strokes[b+1]);}
uint64_t review_u64(uint b) {return u64(strokes[b],strokes[b+1]);}
uint face_base(int j) {return pad1+4u+262144u+(uint)j*26u;}
int face_head(uint ow) {return asint(strokes[pad1+4u+((ow*2654435761u)&262143u)]);}
bool face_contains(uint ow,double x,double y) {
 bool found=false;precise double px=x*256.0,py=y*256.0;
 for(int j=face_head(ow);j>=0;j=asint(strokes[face_base(j)+1])) {
  uint b=face_base(j);if(strokes[b]!=ow)continue;found=true;
  int64_t ax=review_i64(b+2),ay=review_i64(b+4),bx=review_i64(b+6),by=review_i64(b+8),cx=review_i64(b+10),cy=review_i64(b+12);
  precise double e0=(double)(bx-ax)*(py-(double)ay)-(double)(by-ay)*(px-(double)ax);
  precise double e1=(double)(cx-bx)*(py-(double)by)-(double)(cy-by)*(px-(double)bx);
  precise double e2=(double)(ax-cx)*(py-(double)cy)-(double)(ay-cy)*(px-(double)cx);
  if(review_i64(b+14)>0?(e0>=0&&e1>=0&&e2>=0):(e0<=0&&e1<=0&&e2<=0))return true;
 }
 return !found;
}
double2 face_gradient(uint ow,int x,int y,float depth) {
 double best=1e100;double2 result=double2(0,0);
 int64_t px=(int64_t)x*256+128,py=(int64_t)y*256+128;
 for(int j=face_head(ow);j>=0;j=asint(strokes[face_base(j)+1])) {
  uint b=face_base(j);if(strokes[b]!=ow)continue;
  int64_t ax=review_i64(b+2),ay=review_i64(b+4),bx=review_i64(b+6),by=review_i64(b+8),cx=review_i64(b+10),cy=review_i64(b+12);
  int64_t e0=(bx-ax)*(py-ay)-(by-ay)*(px-ax),e1=(cx-bx)*(py-by)-(cy-by)*(px-bx),e2=(ax-cx)*(py-cy)-(ay-cy)*(px-cx);
  if(!(review_i64(b+14)>0?(e0>=0&&e1>=0&&e2>=0):(e0<=0&&e1<=0&&e2<=0)))continue;
  uint64_t zq=review_u64(b+20)+review_u64(b+22)*(uint64_t)((int64_t)x-review_i64(b+16))+review_u64(b+24)*(uint64_t)((int64_t)y-review_i64(b+18));
  float z=(float)((double)(int64_t)zq*asdouble(0u,0x3CB00000u));
  double error=abs((double)z-(double)depth);if(error>=best)continue;best=error;
  result=double2((double)review_i64(b+22),(double)review_i64(b+24))*asdouble(0u,0x3CB00000u);
 }
 return result;
}
float sample_depth(double px,double py,float iz,double sx,double sy,double4 ls,double ddx,double ddy,uint po) {
 if(pad1==0u)return iz;
 int x=(int)sx,y=(int)sy;uint at=(uint)y*w+(uint)x;
 precise double dx=ls.z-ls.x,dy=ls.w-ls.y,ll=dx*dx+dy*dy;
 precise double u=ll>0.0?((px-ls.x)*dx+(py-ls.y)*dy)/ll:0.0;
 if(u<0.0)u=0.0;if(u>1.0)u=1.0;
 precise double cx=ls.x+u*dx,cy=ls.y+u*dy;
 if((owner[at]>>12)==(po>>12) && !face_contains(owner[at],cx,cy))return iz<asfloat(invz[at])*0.95?iz:1e20;
 double2 g=face_gradient(owner[at],x,y,asfloat(invz[at]));
 precise double correction=g.x*(cx-(double)x-0.5)+g.y*(cy-(double)y-0.5);
 float center_z=iz+(float)(ddx*(cx-px)+ddy*(cy-py));
 return center_z-(float)correction;
}
bool stroke_hidden(double px, double py, float iz, uint po, bool line_point, bool self_hidden, double4 ls, int radius, double ddx, double ddy)
{
    // the same double arithmetic, in the same order, as decal_stroke_hidden in remaster_raster.c
    const double dx = ls.z - ls.x, dy = ls.w - ls.y;
    const double ll = dx * dx + dy * dy;
    double u = ll > 0.0 ? ((px - ls.x) * dx + (py - ls.y) * dy) / ll : 0.0;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    const double cx = ls.x + u * dx, cy = ls.y + u * dy;
    if (cx >= 0.0 && cy >= 0.0 && cx < (double)w && cy < (double)h) {
        const uint i = (uint)(int)cy * w + (uint)(int)cx;
        if (decal_hidden(i, sample_depth(px,py,iz,cx,cy,ls,ddx,ddy,po), po, line_point, self_hidden)) return true;
        if ((owner[i] >> 12) == (po >> 12)) return false;
    }
    if (ll <= 0.0) return false;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            const double qx = cx + (double)x, qy = cy + (double)y;
            if (qx < 0.0 || qy < 0.0 || qx >= (double)w || qy >= (double)h) continue;
            const uint i = (uint)(int)qy * w + (uint)(int)qx;
            const uint ow = owner[i];
            if (ow != 0 && (ow >> 12) == (po >> 12) && decal_hidden(i, sample_depth(px,py,iz,qx,qy,ls,ddx,ddy,po), po, line_point, self_hidden)) return true;
        }
    return false;
}

[numthreads(16, 16, 1)]
void main(uint3 gid : SV_GroupID, uint3 id : SV_DispatchThreadID)
{
    if (mode == 0) {
        const uint s = id.y * 4096u + id.x;
        if (s >= nstrokes) return;
        const uint b = s * 24;
        hidden[s] = 0u;
        if (strokes[b + 3] == 0u) return;
        // point_behind_own_surface in remaster_raster.c works in doubles (centre, slope, surface) with a float iz: the same here, from the double end (words 16, 17).
        // In floats the two differed for far points, whose inverse depths are about 3e-8 (7 November: a light drawn on the CPU and not on the GPU).
        const double cx = asdouble(strokes[b + 16], strokes[b + 17]), cy = asdouble(strokes[b + 18], strokes[b + 19]);
        const float iz = asfloat(strokes[b + 13]);
        if (cx < 0.0 || cy < 0.0 || cx >= (double)w || cy >= (double)h) return;
        const int x = (int)cx, y = (int)cy;
        const uint i = (uint)y * w + (uint)x;
        const uint own = owner[i];
        const uint po = strokes[b + 2];
        if (own == 0 || (own & 0x800u) != 0 || (own >> 12) != (po >> 12)) return;
        double surface = (double)asfloat(invz[i]);
        for (int axis = 0; axis < 2; ++axis) {
            double slope = 0.0; int samples = 0;
            for (int sign = -1; sign <= 1; sign += 2) {
                const int nx = x + (axis == 0 ? sign : 0), ny = y + (axis == 1 ? sign : 0);
                if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h) continue;
                const uint ni = (uint)ny * w + (uint)nx;
                /* a float difference, as the CPU's (float - float) * int */
                if (owner[ni] == own) { slope += (double)((asfloat(invz[ni]) - asfloat(invz[i])) * (float)sign); ++samples; }
            }
            if (samples != 0) surface += slope / (double)samples * (axis == 0 ? cx - (double)x - 0.5 : cy - (double)y - 0.5);
        }
        if ((double)iz < surface * 0.99) hidden[s] = 1u;
        return;
    }
    const uint x = id.x, y = id.y;
    if (x >= w || y >= h) return;
    const uint tile = gid.y * tilesx + gid.x;
    const float dx = (float)(x - gid.x * 16), dy = (float)(y - gid.y * 16);
    const uint at = y * w + x;
    uint col = colour[at], idx = index_[at];
    float s_z = 0.0, s_ax = 0.0, s_ay = 0.0;
    uint s_owner = 0u;
    const float px = (float)x + 0.5, py = (float)y + 0.5;
    const uint s0 = tileoff[tile], e0 = tileoff[tile + 1];
    for (uint j = s0; j < e0; j++) {
        const uint ti = ents[9 * j];
        const uint b = ti * 40;
        const float l1 = asfloat(ents[9 * j + 1]) + asfloat(tri[b + 0]) * dx + asfloat(tri[b + 1]) * dy;
        const float l2 = asfloat(ents[9 * j + 2]) + asfloat(tri[b + 2]) * dx + asfloat(tri[b + 3]) * dy;
        const float l3 = 1.0 - l1 - l2;
        bool inside;
        if (exact != 0) {
            const int64_t ddx = (int64_t)(x - gid.x * 16), ddy = (int64_t)(y - gid.y * 16);
            const int64_t q0 = i64(ents[9 * j + 3], ents[9 * j + 4]) + i64(tri[b + 12], tri[b + 13]) * ddx + i64(tri[b + 14], tri[b + 15]) * ddy;
            const int64_t q1 = i64(ents[9 * j + 5], ents[9 * j + 6]) + i64(tri[b + 16], tri[b + 17]) * ddx + i64(tri[b + 18], tri[b + 19]) * ddy;
            const int64_t q2 = i64(ents[9 * j + 7], ents[9 * j + 8]) + i64(tri[b + 20], tri[b + 21]) * ddx + i64(tri[b + 22], tri[b + 23]) * ddy;
            inside = tri[b + 24] != 0 ? (q0 >= 0 && q1 >= 0 && q2 >= 0) : (q0 <= 0 && q1 <= 0 && q2 <= 0);
        } else {
            inside = !(l1 < 0.0 || l2 < 0.0 || l3 < 0.0);
        }
        if (!inside) continue;
        float iz;
        if (exact != 0) {
            const uint64_t ddx = (uint64_t)((int64_t)x - (int64_t)asint(tri[b + 25])), ddy = (uint64_t)((int64_t)y - (int64_t)asint(tri[b + 26]));
            const uint64_t pz = u64(tri[b + 27], tri[b + 28]) + u64(tri[b + 29], tri[b + 30]) * ddx + u64(tri[b + 31], tri[b + 32]) * ddy;
            iz = (float)((double)(int64_t)pz * asdouble(0u, 0x3CB00000u));      // 2^-52
        } else {
            iz = asfloat(tri[b + 4]) + l1 * asfloat(tri[b + 5]) + l2 * asfloat(tri[b + 6]);
        }
        const uint si = tri[b + 7];
        if (hidden[si] != 0u) continue;
        const uint sb = si * 24;
        const uint po = strokes[sb + 2];
        const bool line_point = strokes[sb + 3] != 0u;
        const uint object = po >> 12;
        const double4 ls = double4(asdouble(strokes[sb + 16], strokes[sb + 17]), asdouble(strokes[sb + 18], strokes[sb + 19]),
                                   asdouble(strokes[sb + 20], strokes[sb + 21]), asdouble(strokes[sb + 22], strokes[sb + 23]));
        const float ddx = asfloat(strokes[sb + 9]), ddy = asfloat(strokes[sb + 10]);
        const double ddxd = asdouble(strokes[sb + 5], strokes[sb + 6]), ddyd = asdouble(strokes[sb + 7], strokes[sb + 8]);
        // decal rules against the polygon pass
        if (owner[at] == 0xFFFFF7FEu) continue;   // a far dot: the original paints it over the lines already there (remaster_raster.c)
        const bool same_here = (owner[at] >> 12) == object;
        // F117_DECAL_SIMPLE (default on; F117_DECAL_SIMPLE=0 restores the exact rule): pad2 != 0 skips the neighbourhood walk (stroke_hidden), which is where the time went
        // (strokes 11.9 to 3.0 ms on the late flight at 4K); the face-cache depth stays. About 216 of 8.3 million pixels differ from the exact rule (docs/gpu-compositor.md).
        if (decal_hidden(at, same_here && !line_point ? sample_depth(px,py,iz,px,py,ls,ddxd,ddyd,po) : iz, po, line_point, false)) continue;
        if (!same_here && pad2 == 0u && stroke_hidden(px, py, iz, po, line_point, false, ls, (int)strokes[sb + 4], ddxd, ddyd)) continue;
        // stroke competition
        const float old_z = s_z;
        const uint old = old_z > 0.0 ? s_owner : 0u;
        const bool old_point = (old & 0x100000u) != 0u;
        const float point_z0 = line_point ? iz : old_z;
        float edge_z = line_point ? old_z : iz;
        const float point_z = point_z0;
        if (old_z > 0.0 && old_point != line_point) {
            if (line_point) edge_z += (float)((double)s_ax * (ls.x - px) + (double)s_ay * (ls.y - py));    // in double, then to float, as the CPU
            else edge_z += (float)(ddxd * ((double)s_ax - (double)px) + ddyd * ((double)s_ay - (double)py));    // in double, then to float, as the CPU
        }
        const bool point_tie = old_z > 0.0 && object != 0u && object == (old & 0xFFFFFu) && old_point != line_point &&
            ((po & 0x800u) != 0u || (point_z >= edge_z * 0.99 && edge_z >= point_z * 0.99));
        const bool pair = object != 0u && (old & 0xFFFFFu) != 0u && old_point != line_point;
        if (pair) {
            const float new_z = line_point ? point_z : edge_z;
            const float prev_z = line_point ? edge_z : point_z;
            if (point_tie ? !line_point : new_z < prev_z * 0.9999) continue;
        }
        s_z = iz;
        s_ax = line_point ? (float)ls.x : ddx;
        s_ay = line_point ? (float)ls.y : ddy;
        s_owner = object | (line_point ? 0x100000u : 0u);
        col = strokes[sb + 0];
        idx = strokes[sb + 1];
    }
    colour[at] = col; index_[at] = idx; strokez[at] = asuint(s_z);
}
