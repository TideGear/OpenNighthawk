// The hardware-rasterised composite (gpu_compose.c, F117_HW_RASTER; docs/gpu-compositor.md "hardware path"): the sea's logic fills as blend MIN/MAX on bit planes, then a resolve pass.
// Targets: 0 colour (bytes B,G,R,A of the CPU's 0xAARRGGBB), 1 bits 0..3 of the palette index, 2 bits 4..7, 3 flags (r valid, g touched by a logic fill, b OR 0x80 count x 0.2 or 1.0 for a stroke, a drawn after the bands).
struct VIn { float2 p : TEXCOORD0; float d : TEXCOORD1; float4 c : TEXCOORD2; uint idx : TEXCOORD3; uint own : TEXCOORD4; uint dith : TEXCOORD5; };
struct VOut { float4 pos : SV_Position; float4 c : TEXCOORD0; nointerpolation uint idx : TEXCOORD1; nointerpolation uint own : TEXCOORD2; nointerpolation uint dith : TEXCOORD3; };
VOut vs(VIn i) { VOut o; o.pos = float4(i.p.x, i.p.y, i.d, 1.0); o.c = i.c; o.idx = i.idx; o.own = i.own; o.dith = i.dith; return o; }
struct POut { float4 col : SV_Target0; float4 a : SV_Target1; float4 b : SV_Target2; float4 f : SV_Target3; };
static const uint bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
POut ps(VOut i)
{
    POut o;
    float3 v = i.c.xyz * 255.0;
    const uint kind = i.own & 3u;                 // 0 polygon or line, 1 logic fill (AND/OR), 2 stipple
    if (i.dith != 0) {
        const uint2 q = uint2(i.pos.xy);
        const float noise = ((float)bayer[(q.y & 3) * 4 + (q.x & 3)] + 0.5) / 16.0 - 0.5;
        v = clamp(floor(v + noise + 0.5), 0.0, 255.0);
    }
    o.col = float4(v.z / 255.0, v.y / 255.0, v.x / 255.0, kind == 2 ? 0.5 : 1.0);
    const uint x = i.idx & 255u;
    o.a = float4((float)(x & 1u), (float)((x >> 1) & 1u), (float)((x >> 2) & 1u), (float)((x >> 3) & 1u));
    o.b = float4((float)((x >> 4) & 1u), (float)((x >> 5) & 1u), (float)((x >> 6) & 1u), (float)((x >> 7) & 1u));
    if (kind == 0) o.f = float4(i.idx == 0xFFFFu ? 0.0 : 1.0, 0.0, (i.own & 8u) ? 1.0 : 0.0, (i.own & 16u) ? 0.0 : 1.0);   // own bit 3: a stroke, bit 4: a band or the clear
    else if (kind == 1) o.f = float4(0.0, 1.0, (i.own & 4u) ? 0.2 : 0.0, 0.0);
    else o.f = float4(0.0, 0.0, 0.0, 0.0);
    return o;
}

// ---- resolve: a full-screen triangle
Texture2D<float4> tcol : register(t0, space2);
SamplerState scol : register(s0, space2);
Texture2D<float4> ta : register(t1, space2);
SamplerState sa : register(s1, space2);
Texture2D<float4> tb : register(t2, space2);
SamplerState sb : register(s2, space2);
Texture2D<float4> tf : register(t3, space2);
SamplerState sf : register(s3, space2);
Texture2D<float4> tpal : register(t4, space2);
SamplerState sp : register(s4, space2);
struct RV { float4 pos : SV_Position; };
RV rvs(uint id : SV_VertexID) { RV o; float2 uv = float2((id << 1) & 2, id & 2); o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }

#define F117_BAND_HLSL 1
#include "../band_palette.h"

uint pack(float4 c) { return (uint)(c.z * 255.0 + 0.5) | ((uint)(c.y * 255.0 + 0.5) << 8) | ((uint)(c.x * 255.0 + 0.5) << 16) | 0xFF000000u; }
float4 unpack(uint u) { return float4(((u >> 0) & 255u), ((u >> 8) & 255u), ((u >> 16) & 255u), 255.0) / 255.0; }
uint pal(uint i) { const float4 t = tpal.Load(int3(i, 0, 0)); return pack(float4(t.z, t.y, t.x, 1.0)); }
float4 rps(RV i) : SV_Target0
{
    const int2 q = int2(i.pos.xy);
    float4 c = tcol.Load(int3(q, 0));
    const float4 f = tf.Load(int3(q, 0));
    if (f.g < 0.5) return c;                      // not touched by a logic fill: the colour as drawn
    const float4 a = ta.Load(int3(q, 0)), b = tb.Load(int3(q, 0));
    const uint ni = (uint)(a.x + 0.5) | ((uint)(a.y + 0.5) << 1) | ((uint)(a.z + 0.5) << 2) | ((uint)(a.w + 0.5) << 3)
                  | ((uint)(b.x + 0.5) << 4) | ((uint)(b.y + 0.5) << 5) | ((uint)(b.z + 0.5) << 6) | ((uint)(b.w + 0.5) << 7);
    if (f.r < 0.5) return c;                      // no index there: nothing to look up
    if (f.b > 0.1 && f.b < 0.3 && ni >= 0xF0u) {                // the sea's smoothing (band_palette_sea): the pixel was a band index 0x70..0x7F before the OR 0x80
        const uint old = ni - 0x80u;
        const uint prev = old > 0x70u ? old - 1u : old, next = old < 0x7Fu ? old + 1u : old;
        const uint colour = pack(float4(c.z, c.y, c.x, 1.0));
        const uint r = band_palette_sea(colour, old, pal(prev), pal(old), pal(next), pal(prev | 0x80u), pal(ni), pal(next | 0x80u));
        return unpack(r);
    }
    return tpal.Load(int3(ni, 0, 0));
}
