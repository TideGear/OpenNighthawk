// Optional Direct3D 12 GPU timestamp queries (F117_GPU_TIMESTAMPS).
// All borrowed D3D12 objects must outlive the profiler. Call read/reset only
// after the application's existing final SDL fence has completed.
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>

struct TimestampDraft {
    ID3D12QueryHeap *heap = nullptr;
    ID3D12Resource *readback = nullptr;
    UINT64 frequency = 0;
    UINT count = 0, resolved = 0;
    char labels[64][64] = {};
    ~TimestampDraft() {
        if (readback) readback->Release();
        if (heap) heap->Release();
    }
};

extern "C" void *f117_timestamp_create(void *device, void *queue)
{
    if (!device || !queue) return nullptr;
    auto *p = new (std::nothrow) TimestampDraft;
    if (!p) return nullptr;
    auto *d = static_cast<ID3D12Device *>(device);
    auto *q = static_cast<ID3D12CommandQueue *>(queue);
    D3D12_QUERY_HEAP_DESC query = {};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = 64;
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    properties.CreationNodeMask = properties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 64 * sizeof(UINT64);
    buffer.Height = 1;
    buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(q->GetTimestampFrequency(&p->frequency)) || !p->frequency ||
        FAILED(d->CreateQueryHeap(&query, IID_PPV_ARGS(&p->heap))) ||
        FAILED(d->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
            &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&p->readback)))) {
        delete p;
        return nullptr;
    }
    return p;
}

extern "C" int f117_timestamp_mark(void *profiler, void *command_list,
                                     const char *label)
{
    auto *p = static_cast<TimestampDraft *>(profiler);
    if (!p || !command_list || p->count >= 64) return 0;
    auto *list = static_cast<ID3D12GraphicsCommandList *>(command_list);
    strncpy_s(p->labels[p->count], label ? label : "unnamed", _TRUNCATE);
    list->EndQuery(p->heap, D3D12_QUERY_TYPE_TIMESTAMP, p->count++);
    return 1;
}

// Resolve this command list's new queries before submission. Begin/finish
// lists resolve disjoint ranges of the same frame. No unresolved query read.
extern "C" int f117_timestamp_resolve(void *profiler, void *command_list)
{
    auto *p = static_cast<TimestampDraft *>(profiler);
    if (!p || !command_list) return 0;
    if (p->count > p->resolved) {
        auto *list = static_cast<ID3D12GraphicsCommandList *>(command_list);
        list->ResolveQueryData(p->heap, D3D12_QUERY_TYPE_TIMESTAMP, p->resolved,
            p->count - p->resolved, p->readback, p->resolved * sizeof(UINT64));
        p->resolved = p->count;
    }
    return 1;
}

extern "C" int f117_timestamp_read_after_fence(void *profiler, FILE *out)
{
    auto *p = static_cast<TimestampDraft *>(profiler);
    if (!p || !out || p->count != p->resolved || !p->count) return 0;
    D3D12_RANGE range = {0, p->count * sizeof(UINT64)};
    void *mapped = nullptr;
    if (FAILED(p->readback->Map(0, &range, &mapped))) return 0;
    const auto *ticks = static_cast<const UINT64 *>(mapped);
    bool valid = true;
    for (UINT i = 1; i < p->count; ++i) valid = valid && ticks[i] >= ticks[i - 1];
    if (!valid) std::fprintf(out, "invalid GPU timestamp order; sample discarded\n");
    for (UINT i = 1; i < p->count; ++i) {
        if (!valid) break;
        std::fprintf(out, "%s,%s,%.6f\n", p->labels[i - 1], p->labels[i],
            1000.0 * static_cast<double>(ticks[i] - ticks[i - 1]) / p->frequency);
    }
    D3D12_RANGE no_write = {0, 0};
    p->readback->Unmap(0, &no_write);
    p->count = p->resolved = 0;
    return valid ? 1 : 0;
}

extern "C" void f117_timestamp_destroy(void *profiler)
{
    delete static_cast<TimestampDraft *>(profiler);
}
