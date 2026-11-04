// GPU compositor stage 1 probe (docs/gpu-compositor.md): one thread per pixel writes a value the CPU can predict,
// read from a read-only storage buffer, so the harness checks the device, the shader format, the bindings and readback.
StructuredBuffer<uint> src : register(t0, space0);
RWStructuredBuffer<uint> dst : register(u0, space1);
cbuffer params : register(b0, space2) { uint width; uint height; uint pad0; uint pad1; };

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    const uint i = id.y * width + id.x;
    dst[i] = src[i] * 2654435761u + id.x * 31u + id.y;
}
