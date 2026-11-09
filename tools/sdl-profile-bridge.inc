/* Review draft: include only in SDL 3.4.8's Direct3D 12 backend, after its types.
 * Borrowed pointers: valid only while the owner device/command buffer lives.
 * Caller must verify SDL_GetGPUDeviceDriver(device) == "direct3d12".
 * No rendering, synchronization, ownership transfer, or game-state changes. */
__declspec(dllexport) void *F117_GPUProfileDevice(SDL_GPUDevice *device)
{
    return device ? ((D3D12Renderer *)device->driverData)->device : NULL;
}
__declspec(dllexport) void *F117_GPUProfileQueue(SDL_GPUDevice *device)
{
    return device ? ((D3D12Renderer *)device->driverData)->commandQueue : NULL;
}
__declspec(dllexport) void *F117_GPUProfileCommandList(SDL_GPUCommandBuffer *command_buffer)
{
    return command_buffer ? ((D3D12CommandBuffer *)command_buffer)->graphicsCommandList : NULL;
}