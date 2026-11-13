// GPU compositor stage 3 (docs/gpu-compositor.md): the solid polygons and the AND / OR / stipple polygons of a composite, in single precision, one thread per
// pixel. The CPU has clipped each polygon to the near plane, projected it, split it into triangles and binned the triangles into 16 x 16 tiles in the trace's
// order, with each triangle's two barycentric weights evaluated in double at the tile's first pixel (so a huge ground polygon whose vertices lie far off the
// screen does not lose its precision); a thread walks its tile's list in order carrying the pixel's state (colour, palette index, depth, owner) in
// registers, with remaster_raster.c's depth, owner and fill rules. Painter order and the later-wins tie are therefore the CPU's.
StructuredBuffer<uint> tri : register(t0, space0);       // 12 words a triangle
StructuredBuffer<uint> ents : register(t1, space0);      // 3 words per (triangle, tile): triangle, L1 and L2 at the tile's first pixel centre
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
    uint pad;
};

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
        const uint ti = ents[3 * j];
        const uint b = ti * 12;
        const float l1 = asfloat(ents[3 * j + 1]) + asfloat(tri[b + 0]) * dx + asfloat(tri[b + 1]) * dy;
        const float l2 = asfloat(ents[3 * j + 2]) + asfloat(tri[b + 2]) * dx + asfloat(tri[b + 3]) * dy;
        const float l3 = 1.0 - l1 - l2;
        if (l1 < 0.0 || l2 < 0.0 || l3 < 0.0) continue;
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
            if (po == 0xFFFFF7FEu) k = 0.0;   // a far dot: over whatever is there (see remaster_raster.c)
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
