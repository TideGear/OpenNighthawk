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
    uint pad;
};

static const uint bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

// per triangle, 24 words: 0..3 a1 b1 a2 b2, 4 cx, 5 cy, 6 ciz, 7 diz1, 8 diz2, 9..11 dc1, 12..14 dc2, 15..17 cc, 18 paint, 19..22 x0 x1 y0 y1
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint x = id.x, y = id.y;
    if (x >= w || y >= h) return;
    float d = 0.0;
    uint col = 0xFF000000u, idx = 0xFFFFu;
    const float fx = (float)x + 0.5, fy = (float)y + 0.5;
    for (uint t = 0; t < ntri; t++) {
        const uint b = t * 24;
        const int x0 = asint(tri[b + 19]), x1 = asint(tri[b + 20]), y0 = asint(tri[b + 21]), y1 = asint(tri[b + 22]);
        if ((int)x < x0 || (int)x > x1 || (int)y < y0 || (int)y > y1) continue;
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
    colour[y * w + x] = col;
    index_[y * w + x] = idx;
    invz[y * w + x] = 0u;
    owner[y * w + x] = 0u;
}
