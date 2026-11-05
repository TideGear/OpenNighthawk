// GPU compositor, piece 3 (docs/gpu-compositor.md): the finished wide picture (0xAARRGGBB words in a buffer) into an RGBA storage texture that the app's SDL renderer wraps and draws with
// no readback. One thread per pixel.
StructuredBuffer<uint> pic : register(t0, space0);
RWTexture2D<float4> target : register(u0, space1);

cbuffer params : register(b0, space2)
{
    uint w;
    uint h;
    uint pad0;
    uint pad1;
};

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= w || id.y >= h) return;
    const uint c = pic[id.y * w + id.x];
    target[int2(id.x, id.y)] = float4((float)((c >> 16) & 255u), (float)((c >> 8) & 255u), (float)(c & 255u), (float)(c >> 24)) / 255.0;
}
