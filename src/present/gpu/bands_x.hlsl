// GPU compositor stage 2, the fast form (docs/gpu-compositor.md): the clear and the shaded band triangles for the whole picture in ONE dispatch, one thread
// per pixel in single precision. Each thread walks the triangles in the trace's order with the CPU's own depth test (a later triangle wins a tie within
// a hundredth of a percent), so painter order is kept; the weights are computed directly rather than stepped along the row, so a pixel on a triangle's
// edge, or a colour on a rounding boundary, can differ from the double-precision CPU result. gpu_stage reports how many pixels and by how much.
StructuredBuffer<uint> tri : register(t0, space0);
RWStructuredBuffer<uint> colour : register(u0, space1);
RWStructuredBuffer<uint> index_ : register(u1, space1);
RWStructuredBuffer<uint> invz : register(u2, space1);     // zero after the bands (the CPU zeroes the depth after them)
RWStructuredBuffer<uint> owner : register(u3, space1);    // zero: the bands have no owner

cbuffer params : register(b0, space2)
{
    uint w;
    uint h;
    uint ntri;
    uint exact;                                           // 1: integer coverage and planes (exact.h), 0: the float weights
};

static const uint bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

// Exact form (exact.h; docs/gpu-compositor.md "Step 2 design"): per triangle 64 words: the 24 above, then 24 to 35 the three vertices snapped to 1/256 px
// (int64: ax ay bx by cx cy), 36 the area's sign (1 positive), 37 and 38 the plane origin pixel (ox, oy), and from 40 the four planes (inverse depth, red,
// green, blue) as base, step in x, step in y, int64 each, six words a plane. Coverage is three int64 edge values; each plane is evaluated in wrapping uint64.
int64_t i64(uint lo, uint hi) { return (int64_t)(((uint64_t)hi << 32) | (uint64_t)lo); }
uint64_t u64(uint lo, uint hi) { return ((uint64_t)hi << 32) | (uint64_t)lo; }

// Per group cull (exact): a group is 64 pixels of one row. Each thread tests one triangle of a batch of 64 against that whole segment: an edge's value
// is linear along the row, so its best value over the segment is at one end, and if that is on the wrong side of zero the triangle covers no pixel of the
// segment (the same int64 edge test as below, at the end pixel). Only the survivors, normally one to three of the 64 or so band triangles (in the wide view
// every box is the whole picture, so the box test culls nothing), are visited per pixel, in the trace's order: painter order and the depth tie rule are unchanged.
groupshared uint g_mask[2];

bool edge_may_pass(int64_t ax, int64_t ay, int64_t bx, int64_t by, int64_t py, int64_t p0, int64_t p1, bool pos)
{
    const int64_t dy = by - ay;
    const int64_t px = pos ? (dy < 0 ? p1 : p0) : (dy > 0 ? p1 : p0);
    const int64_t q = (bx - ax) * (py - ay) - dy * (px - ax);
    return pos ? q >= 0 : q <= 0;
}

bool may_cover_segment(uint t, int xs, int xe, uint y, int64_t p0, int64_t p1)
{
    const uint b = t * 64;
    const int tx0 = asint(tri[b + 19]), tx1 = asint(tri[b + 20]), ty0 = asint(tri[b + 21]), ty1 = asint(tri[b + 22]);
    if (xe < tx0 || xs > tx1 || (int)y < ty0 || (int)y > ty1) return false;
    if (exact == 0) return true;
    const int64_t py = (int64_t)y * 256 + 128;
    const int64_t xa = i64(tri[b + 24], tri[b + 25]), ya = i64(tri[b + 26], tri[b + 27]), xb = i64(tri[b + 28], tri[b + 29]);
    const int64_t yb = i64(tri[b + 30], tri[b + 31]), xc = i64(tri[b + 32], tri[b + 33]), yc = i64(tri[b + 34], tri[b + 35]);
    const bool pos = tri[b + 36] != 0;
    return edge_may_pass(xa, ya, xb, yb, py, p0, p1, pos) && edge_may_pass(xb, yb, xc, yc, py, p0, p1, pos) && edge_may_pass(xc, yc, xa, ya, py, p0, p1, pos);
}

[numthreads(64, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 gt : SV_GroupThreadID, uint3 id : SV_DispatchThreadID)
{
    const uint x = id.x, y = id.y;
    const bool live = x < w && y < h;
    const uint lane = gt.x;
    const int xs = (int)(gid.x * 64u), xe = (int)min(gid.x * 64u + 63u, w - 1u);
    const int64_t seg0 = (int64_t)xs * 256 + 128, seg1 = (int64_t)xe * 256 + 128;
    float d = 0.0;
    uint col = 0xFF000000u, idx = 0xFFFFu;
    const float fx = (float)x + 0.5, fy = (float)y + 0.5;
    for (uint batch = 0; batch < ntri; batch += 64) {
        if (lane == 0) { g_mask[0] = 0u; g_mask[1] = 0u; }
        GroupMemoryBarrierWithGroupSync();
        const uint tc = batch + lane;
        if (tc < ntri && may_cover_segment(tc, xs, xe, y, seg0, seg1)) InterlockedOr(g_mask[lane >> 5], 1u << (lane & 31));
        GroupMemoryBarrierWithGroupSync();
        uint m0 = g_mask[0], m1 = g_mask[1];
        while ((m0 | m1) != 0u) {
            uint bit;
            if (m0 != 0u) { bit = firstbitlow(m0); m0 &= m0 - 1u; } else { bit = 32u + firstbitlow(m1); m1 &= m1 - 1u; }
            if (!live) continue;
            const uint b = (batch + bit) * 64;
        const int x0 = asint(tri[b + 19]), x1 = asint(tri[b + 20]), y0 = asint(tri[b + 21]), y1 = asint(tri[b + 22]);
        if ((int)x < x0 || (int)x > x1 || (int)y < y0 || (int)y > y1) continue;
        if (exact != 0) {
            const int64_t px = (int64_t)x * 256 + 128, py = (int64_t)y * 256 + 128;
            const int64_t xa = i64(tri[b + 24], tri[b + 25]), ya = i64(tri[b + 26], tri[b + 27]), xb = i64(tri[b + 28], tri[b + 29]);
            const int64_t yb = i64(tri[b + 30], tri[b + 31]), xc = i64(tri[b + 32], tri[b + 33]), yc = i64(tri[b + 34], tri[b + 35]);
            const int64_t q0 = (xb - xa) * (py - ya) - (yb - ya) * (px - xa);
            const int64_t q1 = (xc - xb) * (py - yb) - (yc - yb) * (px - xb);
            const int64_t q2 = (xa - xc) * (py - yc) - (ya - yc) * (px - xc);
            const bool inside = tri[b + 36] != 0 ? (q0 >= 0 && q1 >= 0 && q2 >= 0) : (q0 <= 0 && q1 <= 0 && q2 <= 0);
            if (!inside) continue;
            const uint64_t ddx = (uint64_t)((int64_t)x - (int64_t)asint(tri[b + 37])), ddy = (uint64_t)((int64_t)y - (int64_t)asint(tri[b + 38]));
            const uint64_t pz = u64(tri[b + 40], tri[b + 41]) + u64(tri[b + 42], tri[b + 43]) * ddx + u64(tri[b + 44], tri[b + 45]) * ddy;
            const float eiz = (float)((double)(int64_t)pz * asdouble(0u, 0x3CB00000u));      // 2^-52
            if (eiz < d * 0.9999) continue;
            d = eiz;
            uint eo = 0xFF000000u;
            for (int k = 0; k < 3; k++) {
                const uint c0 = 46 + 6 * k;
                const uint64_t pc = u64(tri[b + c0], tri[b + c0 + 1]) + u64(tri[b + c0 + 2], tri[b + c0 + 3]) * ddx + u64(tri[b + c0 + 4], tri[b + c0 + 5]) * ddy;
                const int64_t vs = (int64_t)pc + ((int64_t)(2 * (int)bayer[(y & 3) * 4 + (x & 3)] - 15) << 27) + 2147483648;
                int64_t vi = vs < 0 ? 0 : (vs >> 32);
                if (vi > 255) vi = 255;
                eo |= (uint)vi << (16 - 8 * k);
            }
            col = eo;
            idx = tri[b + 18];
            continue;
        }
        const float cx = asfloat(tri[b + 4]), cy = asfloat(tri[b + 5]);
        const float l1 = asfloat(tri[b + 0]) * (fx - cx) + asfloat(tri[b + 1]) * (fy - cy);
        const float l2 = asfloat(tri[b + 2]) * (fx - cx) + asfloat(tri[b + 3]) * (fy - cy);
        const float l3 = 1.0 - l1 - l2;
        if (l1 < 0.0 || l2 < 0.0 || l3 < 0.0) continue;
        const float iz = asfloat(tri[b + 6]) + l1 * asfloat(tri[b + 7]) + l2 * asfloat(tri[b + 8]);
        if (iz < d * 0.9999) continue;
        d = iz;
        const float noise = ((float)bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5;
        uint o = 0xFF000000u;
        for (int k = 0; k < 3; k++) {
            float v = asfloat(tri[b + 15 + k]) + l1 * asfloat(tri[b + 9 + k]) + l2 * asfloat(tri[b + 12 + k]) + noise + 0.5;
            v = clamp(v, 0.0, 255.0);
            o |= (uint)v << (16 - 8 * k);
        }
        col = o;
        idx = tri[b + 18];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (!live) return;
    colour[y * w + x] = col;
    index_[y * w + x] = idx;
    invz[y * w + x] = 0u;
    owner[y * w + x] = 0u;
}
