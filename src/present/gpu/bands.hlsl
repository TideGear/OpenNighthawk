// GPU compositor stage 2 (docs/gpu-compositor.md): the clear and the shaded (sky and ground band) triangles, reproducing remaster_raster.c's
// fill_triangle_shaded exactly: the same double-precision arithmetic in the same order (compile with -Gis, every double `precise`), a row
// at a time by one thread (the CPU steps its weights from pixel to pixel, so a pixel's value depends on the ones before it in its row),
// triangles in the trace's order, one dispatch (compute pass) each so a later triangle sees an earlier one's depth.
StructuredBuffer<uint> tri : register(t0, space0);
RWStructuredBuffer<uint> colour : register(u0, space1);
RWStructuredBuffer<uint> index_ : register(u1, space1);
RWStructuredBuffer<uint> invz : register(u2, space1);      // float bits
RWStructuredBuffer<uint> owner : register(u3, space1);

cbuffer params : register(b0, space2)
{
    uint mode;          // 0 clear, 1 one triangle's rows, 2 zero the depth
    uint tri_at;        // first word of the triangle in tri
    uint w;
    uint h;
    int y0;
    int y1;
    int x0;
    int x1;
    uint paint_index;
    uint pad0;
    uint pad1;
    uint pad2;
};

precise double rd(uint k)
{
    return asdouble(tri[tri_at + 2 * k], tri[tri_at + 2 * k + 1]);
}

// floor and ceil of a double that fits an int (the CPU's (int)floor(v) and (int)ceil(v))
int ifloor(precise double v) { int t = (int)v; if ((double)t > v) t--; return t; }
int iceil(precise double v) { int t = (int)v; if ((double)t < v) t++; return t; }

static const uint bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (mode == 0) {
        const uint i = id.x;
        if (i >= w * h) return;
        colour[i] = 0xFF000000u; index_[i] = 0xFFFFu; invz[i] = 0u; owner[i] = 0u;
        return;
    }
    if (mode == 2) {
        const uint i = id.x;
        if (i >= w * h) return;
        invz[i] = 0u;
        return;
    }
    const int y = y0 + (int)id.x;
    if (y > y1) return;
    // doubles in tri: 0 a.x, 1 a.y, 2 b.x, 3 b.y, 4 c.x, 5 c.y, 6..9 a1 b1 a2 b2, 10 c.iz, 11 diz1, 12 diz2, 13..15 dc1, 16..18 dc2, 19..21 cc
    precise double ax = rd(0), ay = rd(1), bx = rd(2), by = rd(3), cx = rd(4), cy = rd(5);
    precise double py = (double)y + 0.5;
    // row_span
    precise double lo = 0.0, hi = 0.0;
    bool any = false;
    precise double px[3] = { ax, bx, cx }, pyv[3] = { ay, by, cy };
    for (int e = 0; e < 3; e++) {
        const int n = (e + 1) % 3;
        precise double pxx = px[e], pyy = pyv[e], qxx = px[n], qyy = pyv[n];
        if (!((pyy <= py && py <= qyy) || (qyy <= py && py <= pyy))) continue;
        if (pyy == qyy) {
            precise double mn = min(pxx, qxx), mx = max(pxx, qxx);
            if (!any) { lo = mn; hi = mx; any = true; } else { lo = min(lo, mn); hi = max(hi, mx); }
            continue;
        }
        precise double xx = pxx + (py - pyy) * (qxx - pxx) / (qyy - pyy);
        if (!any) { lo = xx; hi = xx; any = true; } else { lo = min(lo, xx); hi = max(hi, xx); }
    }
    if (!any) return;
    int xs = ifloor(lo - 0.5) - 1, xe = iceil(hi - 0.5) + 1;
    if (xs < x0) xs = x0;
    if (xe > x1) xe = x1;
    precise double a1 = rd(6), b1 = rd(7), a2 = rd(8), b2 = rd(9);
    precise double ciz = rd(10), diz1 = rd(11), diz2 = rd(12);
    precise double dc1[3] = { rd(13), rd(14), rd(15) }, dc2[3] = { rd(16), rd(17), rd(18) }, cc[3] = { rd(19), rd(20), rd(21) };
    precise double l1 = a1 * ((double)xs + 0.5 - cx) + b1 * (py - cy);
    precise double l2 = a2 * ((double)xs + 0.5 - cx) + b2 * (py - cy);
    for (int x = xs; x <= xe; x++) {
        precise double l3 = 1.0 - l1 - l2;
        if (!(l1 < 0.0 || l2 < 0.0 || l3 < 0.0)) {
            precise float iz = (float)(ciz + l1 * diz1 + l2 * diz2);
            const uint at = (uint)y * w + (uint)x;
            precise float d = asfloat(invz[at]);
            if (!(iz < d * 0.9999f)) {
                invz[at] = asuint(iz);
                precise double noise = ((double)bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5;
                uint o = 0xFF000000u;
                for (int k = 0; k < 3; k++) {
                    precise double v = cc[k] + l1 * dc1[k] + l2 * dc2[k] + noise + 0.5;
                    if (v < 0.0) v = 0.0;
                    if (v > 255.0) v = 255.0;
                    o |= (uint)v << (16 - 8 * k);
                }
                colour[at] = o;
                index_[at] = paint_index;
            }
        }
        l1 += a1;
        l2 += a2;
    }
}
