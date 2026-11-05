// GPU compositor stage 5 (docs/gpu-compositor.md): the wide Remastered picture from the composite and the game's own picture, one thread per output pixel, for the
// default (decal) lines, where the whole-line layer equals the composite's colour and remaster_live.c's replace step reduces to a choice per pixel.
//   centre (the game's 3D window): the composite's colour where the pixel's origin tag is a scene or outline tag and the composite has drawn something there (or the
//   scene is complete, or it is an outline or span tag); otherwise the game's own palette colour (the six-bit channels expanded to eight);
//   margins: the composite's colour.
// Exactly the CPU's integer logic: the inputs are the CPU's own buffers, so the output is expected to be identical.
StructuredBuffer<uint> tagflags : register(t0, space0);   // 256: bit 0 scene tag, bit 1 outline tag, bit 2 span tag
StructuredBuffer<uint> src : register(t1, space0);        // the game's picture, palette indices, packed four to a word
StructuredBuffer<uint> tag : register(t2, space0);        // origin tags, packed four to a word
StructuredBuffer<uint> dac : register(t3, space0);        // 768 six-bit palette bytes, packed four to a word
RWStructuredBuffer<uint> colour : register(u0, space1);   // the composite's colour (tw * h)
RWStructuredBuffer<uint> invz : register(u1, space1);     // its depth
RWStructuredBuffer<uint> strokez : register(u2, space1);  // its stroke depth (nonzero where a stroke was drawn)
RWStructuredBuffer<uint> outpix : register(u3, space1);   // the wide picture (tw * h)

cbuffer params : register(b0, space2)
{
    uint w;
    uint h;
    uint tw;
    uint xoff;
    int x0;
    int x1;
    int y0;
    int y1;
    uint complete;
    uint pad0;
    uint pad1;
    uint pad2;
};

uint byte_of(StructuredBuffer<uint> b, uint i) { return (b[i >> 2] >> ((i & 3u) * 8u)) & 255u; }

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint x = id.x, y = id.y;
    if (x >= tw || y >= h) return;
    const uint ti = y * tw + x;
    const uint c = colour[ti];
    if (x < xoff || x >= xoff + w) { outpix[ti] = c; return; }
    const uint gx = x - xoff;
    const uint i = y * w + gx;
    bool replace = false;
    if ((int)y >= max(y0, 0) && (int)y < min(y1, (int)h) && (int)gx >= max(x0, 0) && (int)gx < min(x1, (int)w)) {
        const uint fl = tagflags[byte_of(tag, i)];
        const bool scene = (fl & 1u) != 0u, outline = (fl & 2u) != 0u, span = (fl & 4u) != 0u;
        replace = (scene || outline) && (complete != 0u || outline || asfloat(invz[ti]) > 0.0 || asfloat(strokez[ti]) > 0.0 || span);
    }
    if (replace) { outpix[ti] = c; return; }
    const uint oc = byte_of(src, i);
    const uint cr = byte_of(dac, oc * 3u), cg = byte_of(dac, oc * 3u + 1u), cb = byte_of(dac, oc * 3u + 2u);
    outpix[ti] = 0xFF000000u | ((cr << 2 | cr >> 4) << 16) | ((cg << 2 | cg >> 4) << 8) | (cb << 2 | cb >> 4);
}
