// GPU compositor stage 4 (docs/gpu-compositor.md): the lines and dots ("strokes") of a composite, after the polygons. remaster_raster.c draws each stroke as two
// triangles under the decal rules: a stroke is hidden where the polygon pass has something clearly nearer than it (decal_hidden, decal_stroke_hidden), and
// strokes compete with one another per pixel (the stroke_z / stroke_owner / aux state). With line_order set the polygon pass's depth and owner are read-only, so
// this pass only reads them; it writes colour and palette index. Two kernels, chosen by `mode`:
//   0: one thread per stroke: a point (a dot or a zero-length line) whose own surface is nearer than it is dropped (point_behind_own_surface);
//   1: one thread per pixel: the strokes binned into the pixel's 16 x 16 tile, in the trace's order, each as two triangles, carrying the pixel's colour, index and
//      stroke state in registers. Single precision (the polygon pass's weights are evaluated in double on the CPU at each tile's first pixel).
StructuredBuffer<uint> tri : register(t0, space0);        // 12 words a triangle: a1 b1 a2 b2 ciz diz1 diz2, stroke
StructuredBuffer<uint> ents : register(t1, space0);       // 3 words per (triangle, tile): triangle, L1, L2 at the tile's first pixel centre
StructuredBuffer<uint> tileoff : register(t2, space0);
StructuredBuffer<uint> strokes : register(t3, space0);    // 16 words a stroke
RWStructuredBuffer<uint> colour : register(u0, space1);
RWStructuredBuffer<uint> index_ : register(u1, space1);
RWStructuredBuffer<uint> invz : register(u2, space1);     // the polygon pass's depth: read only here
RWStructuredBuffer<uint> owner : register(u3, space1);    // the polygon pass's owner: read only here
RWStructuredBuffer<uint> hidden : register(u4, space1);   // per stroke: 1 when a point is behind its own surface
RWStructuredBuffer<uint> strokez : register(u5, space1);  // per pixel: the last stroke's depth (nonzero where a stroke was drawn), for the replace step

cbuffer params : register(b0, space2)
{
    uint mode;
    uint w;
    uint h;
    uint tilesx;
    uint nstrokes;
    uint pad0;
    uint pad1;
    uint pad2;
};

// stroke words: 0 colour, 1 paint index, 2 paint owner, 3 line_point, 4 radius, 5..8 line_screen (x0 y0 x1 y1), 9 depth_dx, 10 depth_dy, 11 sa.x, 12 sa.y, 13 sa.iz

bool decal_hidden(uint at, float iz, uint po, bool line_point, bool self_hidden)
{
    const uint ow = owner[at];
    const bool same = (ow >> 12) == (po >> 12);
    const bool on_ground = ow != 0 && (ow & 0x800u) != 0 && !same;
    if (same && line_point) return self_hidden;
    return !on_ground && iz < asfloat(invz[at]) * (same ? 0.99 : 0.97);
}

bool stroke_hidden(float px, float py, float iz, uint po, bool line_point, bool self_hidden, float4 ls, int radius)
{
    const float dx = ls.z - ls.x, dy = ls.w - ls.y;
    const float ll = dx * dx + dy * dy;
    float u = ll > 0.0 ? ((px - ls.x) * dx + (py - ls.y) * dy) / ll : 0.0;
    u = clamp(u, 0.0, 1.0);
    const float cx = ls.x + u * dx, cy = ls.y + u * dy;
    if (cx >= 0.0 && cy >= 0.0 && cx < (float)w && cy < (float)h) {
        const uint i = (uint)(int)cy * w + (uint)(int)cx;
        if (decal_hidden(i, iz, po, line_point, self_hidden)) return true;
        if ((owner[i] >> 12) == (po >> 12)) return false;
    }
    if (ll <= 0.0) return false;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            const float qx = cx + (float)x, qy = cy + (float)y;
            if (qx < 0.0 || qy < 0.0 || qx >= (float)w || qy >= (float)h) continue;
            const uint i = (uint)(int)qy * w + (uint)(int)qx;
            const uint ow = owner[i];
            if (ow != 0 && (ow >> 12) == (po >> 12) && decal_hidden(i, iz, po, line_point, self_hidden)) return true;
        }
    return false;
}

[numthreads(16, 16, 1)]
void main(uint3 gid : SV_GroupID, uint3 id : SV_DispatchThreadID)
{
    if (mode == 0) {
        const uint s = id.y * 4096u + id.x;
        if (s >= nstrokes) return;
        const uint b = s * 16;
        hidden[s] = 0u;
        if (strokes[b + 3] == 0u) return;
        const float cx = asfloat(strokes[b + 11]), cy = asfloat(strokes[b + 12]), iz = asfloat(strokes[b + 13]);
        if (cx < 0.0 || cy < 0.0 || cx >= (float)w || cy >= (float)h) return;
        const int x = (int)cx, y = (int)cy;
        const uint i = (uint)y * w + (uint)x;
        const uint own = owner[i];
        const uint po = strokes[b + 2];
        if (own == 0 || (own & 0x800u) != 0 || (own >> 12) != (po >> 12)) return;
        float surface = asfloat(invz[i]);
        for (int axis = 0; axis < 2; ++axis) {
            float slope = 0.0; int samples = 0;
            for (int sign = -1; sign <= 1; sign += 2) {
                const int nx = x + (axis == 0 ? sign : 0), ny = y + (axis == 1 ? sign : 0);
                if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h) continue;
                const uint ni = (uint)ny * w + (uint)nx;
                if (owner[ni] == own) { slope += (asfloat(invz[ni]) - asfloat(invz[i])) * (float)sign; ++samples; }
            }
            if (samples != 0) surface += slope / (float)samples * (axis == 0 ? cx - (float)x - 0.5 : cy - (float)y - 0.5);
        }
        if (iz < surface * 0.99) hidden[s] = 1u;
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
        const uint ti = ents[3 * j];
        const uint b = ti * 12;
        const float l1 = asfloat(ents[3 * j + 1]) + asfloat(tri[b + 0]) * dx + asfloat(tri[b + 1]) * dy;
        const float l2 = asfloat(ents[3 * j + 2]) + asfloat(tri[b + 2]) * dx + asfloat(tri[b + 3]) * dy;
        const float l3 = 1.0 - l1 - l2;
        if (l1 < 0.0 || l2 < 0.0 || l3 < 0.0) continue;
        const float iz = asfloat(tri[b + 4]) + l1 * asfloat(tri[b + 5]) + l2 * asfloat(tri[b + 6]);
        const uint si = tri[b + 7];
        if (hidden[si] != 0u) continue;
        const uint sb = si * 16;
        const uint po = strokes[sb + 2];
        const bool line_point = strokes[sb + 3] != 0u;
        const uint object = po >> 12;
        const float4 ls = float4(asfloat(strokes[sb + 5]), asfloat(strokes[sb + 6]), asfloat(strokes[sb + 7]), asfloat(strokes[sb + 8]));
        const float ddx = asfloat(strokes[sb + 9]), ddy = asfloat(strokes[sb + 10]);
        // decal rules against the polygon pass
        if (decal_hidden(at, iz, po, line_point, false)) continue;
        if (owner[at] == 0xFFFFF7FEu) continue;   // a far dot: the original paints it over the lines already there (remaster_raster.c)
        const bool same_here = (owner[at] >> 12) == object;
        if (!same_here && stroke_hidden(px, py, iz, po, line_point, false, ls, (int)strokes[sb + 4])) continue;
        // stroke competition
        const float old_z = s_z;
        const uint old = old_z > 0.0 ? s_owner : 0u;
        const bool old_point = (old & 0x100000u) != 0u;
        const float point_z0 = line_point ? iz : old_z;
        float edge_z = line_point ? old_z : iz;
        const float point_z = point_z0;
        if (old_z > 0.0 && old_point != line_point) {
            if (line_point) edge_z += s_ax * (ls.x - px) + s_ay * (ls.y - py);
            else edge_z += ddx * (s_ax - px) + ddy * (s_ay - py);
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
        s_ax = line_point ? ls.x : ddx;
        s_ay = line_point ? ls.y : ddy;
        s_owner = object | (line_point ? 0x100000u : 0u);
        col = strokes[sb + 0];
        idx = strokes[sb + 1];
    }
    colour[at] = col; index_[at] = idx; strokez[at] = asuint(s_z);
}
