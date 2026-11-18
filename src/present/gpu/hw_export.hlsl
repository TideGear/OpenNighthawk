// Export of the hardware-rasterised picture into the compositor's pixel buffers (gpu_compose.c): the colour as 0xAARRGGBB, the depth buffer's "drawn" and the stroke depth as 1.0f or 0.
Texture2D<float4> tcol : register(t0, space0);
Texture2D<float4> tf : register(t1, space0);
RWStructuredBuffer<uint> colour : register(u0, space1);
RWStructuredBuffer<uint> invz : register(u1, space1);
RWStructuredBuffer<uint> strokez : register(u2, space1);
cbuffer P : register(b0, space2) { uint tw; uint h; uint pad0; uint pad1; };
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= tw || id.y >= h) return;
    const float4 c = tcol.Load(int3(id.xy, 0));
    const float4 f = tf.Load(int3(id.xy, 0));
    const uint i = id.y * tw + id.x;
    colour[i] = (uint)(c.x * 255.0 + 0.5) | ((uint)(c.y * 255.0 + 0.5) << 8) | ((uint)(c.z * 255.0 + 0.5) << 16) | 0xFF000000u;
    invz[i] = f.a > 0.5 ? asuint(1.0f) : 0u;
    strokez[i] = (f.b > 0.9 && f.g < 0.5) ? asuint(1.0f) : 0u;
}
