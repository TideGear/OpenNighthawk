// GPU compositor stage 3 (docs/gpu-compositor.md): the solid polygons and the AND / OR / stipple polygons of a composite, in single precision, one thread per
// pixel. The CPU has clipped each polygon to the near plane, projected it, split it into triangles and binned the triangles into 16 x 16 tiles in the trace's
// order, with each triangle's two barycentric weights evaluated in double at the tile's first pixel (so a huge ground polygon whose vertices lie far off the
// screen does not lose its precision); a thread walks its tile's list in order carrying the pixel's state (colour, palette index, depth, owner) in
// registers, with remaster_raster.c's depth, owner and fill rules. Painter order and the later-wins tie are therefore the CPU's.
StructuredBuffer<uint> tri : register(t0, space0);       // 32 words a triangle (see the exact coverage note below)
StructuredBuffer<uint> ents : register(t1, space0);      // 9 words per (triangle, tile): triangle, L1 and L2 at the tile's first pixel centre, then the three exact edge values
StructuredBuffer<uint> tileoff : register(t2, space0);   // tiles + 1 offsets into ents, in entries
StructuredBuffer<uint> pal : register(t3, space0);       // 256 colours
RWStructuredBuffer<uint> colour : register(u0, space1);
RWStructuredBuffer<uint> index_ : register(u1, space1);
RWStructuredBuffer<uint> invz : register(u2, space1);
RWStructuredBuffer<uint> owner : register(u3, space1);

cbuffer params : register(b0, space2)
{
    uint w;
    uint h;
    uint tilesx;
    uint exact;                                           // 1: integer coverage, 0: the float weights test
};


// Exact coverage (docs/gpu-compositor.md, "Step 2 design"): the CPU snaps each triangle's vertices to 1/256 pixel and sends, per triangle, the three edge
// equations' per-pixel steps as int64 (words 12 to 23) with the sign of the area (word 24), and, per (triangle, tile), the three edge values at the tile's first
// pixel centre as int64 (entry words 3 to 8). A pixel is covered when all three edge values, origin plus steps times the pixel's offset in the tile, have
// the area's sign or are zero: integers only, the decision remaster_raster.c's itri_covers makes.
int64_t i64(uint lo, uint hi) { return (int64_t)(((uint64_t)hi << 32) | (uint64_t)lo); }

// triangle words: 0 a1, 1 b1, 2 a2, 3 b2, 4 c.iz, 5 diz1, 6 diz2, 7 colour (a solid fill) or value (AND, OR, stipple), 8 paint index, 9 paint owner, 10 kind (0 solid, 1 AND, 2 OR, 3 stipple)

[numthreads(16, 16, 1)]
void main(uint3 gid : SV_GroupID, uint3 id : SV_DispatchThreadID)
{
    const uint x = id.x, y = id.y;
    if (x >= w || y >= h) return;
    const uint tile = gid.y * tilesx + gid.x;
    const float dx = (float)(x - gid.x * 16), dy = (float)(y - gid.y * 16);
    const uint at = y * w + x;
    uint col = colour[at], idx = index_[at], own = owner[at];
    float d = asfloat(invz[at]);
    const uint s = tileoff[tile], e = tileoff[tile + 1];
    for (uint j = s; j < e; j++) {
        const uint ti = ents[9 * j];
        const uint b = ti * 32;
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
        const float iz = asfloat(tri[b + 4]) + l1 * asfloat(tri[b + 5]) + l2 * asfloat(tri[b + 6]);
        const uint kind = tri[b + 10];
        if (kind == 0) {
            const uint po = tri[b + 9];
            const bool same = (own >> 12) == (po >> 12);
            const bool on_ground = own != 0 && (own & 0x800u) != 0 && !same;
            const bool me_ground = (po & 0x800u) != 0;
            float k = 0.9999;
            if (on_ground) k = me_ground ? 0.97 : 0.0;
            else if (me_ground && own != 0 && !same) k = 1.03;
            if (iz < d * k) continue;
            d = iz; own = po; col = tri[b + 7]; idx = tri[b + 8];
        } else {
            if (iz < d * 0.9999) continue;
            const uint value = tri[b + 7];
            if (kind == 3) {
                const uint p = pal[value & 255u];
                col = 0xFF000000u | ((((col >> 16 & 255u) + (p >> 16 & 255u)) / 2u) << 16) | ((((col >> 8 & 255u) + (p >> 8 & 255u)) / 2u) << 8) | (((col & 255u) + (p & 255u)) / 2u);
                idx = 0xFFFFu;
                if (d <= 0.0) d = 1e-15;
            } else if (idx != 0xFFFFu) {
                const uint ni = (kind == 1 ? (idx & value) : (idx | value)) & 255u;
                idx = ni;
                col = pal[ni];
                if (d <= 0.0) d = 1e-15;
            }
        }
    }
    colour[at] = col; index_[at] = idx; invz[at] = asuint(d); owner[at] = own;
}
