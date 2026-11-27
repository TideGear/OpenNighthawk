// GPU step A (F117_GPU_HUD, with F117_STEPA): the HUD pass's fine strokes, drawn over the wide picture after the replace step instead of into the CPU's fine
// picture. One thread group layer per stroke (z), covering its bounding box; the coverage rule is drawlist_stroke's upright square pen (owner-approved R37),
// in float rather than double (17 Nov: hardware raster with a pixel tolerance; the CPU path stays the checker).
struct Stroke { float ax, ay, bx, by, half; int cl, ct, cr, cb; uint col; int x0, y0, bw, bh, pad0, pad1; };
StructuredBuffer<Stroke> st : register(t0, space0);
StructuredBuffer<uint> dac : register(t1, space0);        // 768 six-bit palette bytes, packed four to a word
StructuredBuffer<uint> tag : register(t2, space0);        // the CPU picture's origin tags (diagtag.h), packed four to a word
RWStructuredBuffer<uint> outpix : register(u0, space1);   // the wide picture (tw * h)

cbuffer params : register(b0, space2)
{
    uint w;
    uint h;
    uint tw;
    uint xoff;
    uint cover0, cover1, cover2, cover3;   // bit t: a pixel tagged t is art drawn over the HUD (blits, sprites): no stroke there
};

uint byte_of(StructuredBuffer<uint> b, uint i) { return (b[i >> 2] >> ((i & 3u) * 8u)) & 255u; }

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const Stroke s = st[id.z];
    if ((int)id.x >= s.bw || (int)id.y >= s.bh) return;
    const int x = s.x0 + (int)id.x, y = s.y0 + (int)id.y;
    if (x < 0 || y < 0 || x >= (int)w || y >= (int)h) return;
    const float yc = y + 0.5, dx = s.bx - s.ax, dy = s.by - s.ay;
    float ta = 0.0, tb = 1.0;
    if (abs(dy) < 1e-12) {
        if (yc < s.ay - s.half || yc >= s.ay + s.half) return;
    } else {
        ta = (yc - s.half - s.ay) / dy;
        tb = (yc + s.half - s.ay) / dy;
        if (ta > tb) { const float t = ta; ta = tb; tb = t; }
        ta = max(ta, 0.0); tb = min(tb, 1.0);
        if (ta > tb) return;
    }
    const float lo = max((float)s.cl, s.ax + dx * ta - s.half), hi = min((float)s.cr, s.ax + dx * tb + s.half);
    if (lo >= hi) return;
    const int left = (int)ceil(lo - 0.5), right = (int)ceil(hi - 0.5);
    if (x < left || x >= right) return;
    const uint t = byte_of(tag, (uint)y * w + (uint)x);
    const uint cw = t < 32u ? cover0 : t < 64u ? cover1 : t < 96u ? cover2 : cover3;
    if (t < 128u && ((cw >> (t & 31u)) & 1u) != 0u) return;
    const uint oc = s.col & 255u;
    const uint cr = byte_of(dac, oc * 3u), cg = byte_of(dac, oc * 3u + 1u), cb = byte_of(dac, oc * 3u + 2u);
    outpix[(uint)y * tw + (uint)x + xoff] = 0xFF000000u | ((cr << 2 | cr >> 4) << 16) | ((cg << 2 | cg >> 4) << 8) | (cb << 2 | cb >> 4);
}
