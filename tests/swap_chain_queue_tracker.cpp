// WARP and COM-observation mocks only. Never starts or attaches to the game.
#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/SwapChainQueueTracker.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using Tracker = PhoenixD3D12::SwapChainQueueTracker;

// Only ownership, type and reference-count behavior is mocked. Commands are
// never sent to this object; positive GPU execution uses actual WARP queues.
struct MockQueue final : ID3D12CommandQueue
{
    explicit MockQueue(IUnknown* owner, D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT)
        : Owner(owner) { Desc.Type = type; }
    ComPtr<IUnknown> Owner;
    D3D12_COMMAND_QUEUE_DESC Desc{};
    std::atomic<ULONG> References{1};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(ID3D12CommandQueue)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++References; }
    ULONG STDMETHODCALLTYPE Release() override { return --References; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid, void** out) override { return Owner->QueryInterface(iid, out); }
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Heap*, UINT, const D3D12_TILE_RANGE_FLAGS*, const UINT*, const UINT*, D3D12_TILE_MAPPING_FLAGS) override {}
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, D3D12_TILE_MAPPING_FLAGS) override {}
    void STDMETHODCALLTYPE ExecuteCommandLists(UINT, ID3D12CommandList* const*) override {}
    void STDMETHODCALLTYPE SetMarker(UINT, const void*, UINT) override {}
    void STDMETHODCALLTYPE BeginEvent(UINT, const void*, UINT) override {}
    void STDMETHODCALLTYPE EndEvent() override {}
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence*, UINT64) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence*, UINT64) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64*, UINT64*) override { return E_NOTIMPL; }
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override { return Desc; }
};

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_PRESENT)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    return barrier;
}

void ExpectQueue(Tracker& tracker, ID3D12Resource* buffer, ID3D12CommandQueue* expected)
{
    auto* queue = tracker.AcquireQueue(buffer);
    assert(queue == expected);
    if (queue) queue->Release();
}

int main()
{
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    ComPtr<IDXGIAdapter> adapter;
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    ComPtr<ID3D12Device> device;
    assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    ComPtr<ID3D12InfoQueue> info;
    device.As(&info);
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    ComPtr<ID3D12CommandQueue> queueA, queueB;
    assert(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queueA))));
    assert(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queueB))));
    ComPtr<ID3D12CommandAllocator> allocator;
    assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    std::array<ComPtr<ID3D12Resource>, 3> buffers;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 32; desc.Height = 32; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    for (auto& buffer : buffers)
        assert(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PRESENT, nullptr, IID_PPV_ARGS(&buffer))));
    std::vector<ComPtr<ID3D12GraphicsCommandList>> lists(Tracker::MaxLists + 1);
    for (auto& list : lists)
    {
        assert(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
        assert(SUCCEEDED(list->Close()));
    }
    ID3D12Resource* targets[] = {buffers[0].Get(), buffers[1].Get()};
    MockQueue mockA(device.Get()), mockB(device.Get()), wrongDevice(buffers[2].Get());
    MockQueue compute(device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
    Tracker tracker;
    auto fresh = [&] { assert(tracker.SetTarget(device.Get(), targets, 2)); };
    auto observe = [&](UINT list, UINT buffer) {
        auto barrier = Transition(buffers[buffer].Get());
        tracker.OnResourceBarriers(lists[list].Get(), 1, &barrier);
    };
    auto capture = [&](UINT list) {
        ID3D12CommandList* submitted[] = {lists[list].Get()};
        return tracker.CaptureSubmission(1, submitted);
    };

    // Actual WARP recording, submission and fence completion; two buffers
    // deliberately use different DIRECT queues on the same native device.
    fresh();
    ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(event);
    for (UINT i = 0; i < 2; ++i)
    {
        assert(SUCCEEDED(allocator->Reset()));
        assert(SUCCEEDED(lists[i]->Reset(allocator.Get(), nullptr)));
        tracker.OnReset(lists[i].Get());
        D3D12_RESOURCE_BARRIER barriers[] = {
            Transition(buffers[i].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
            Transition(buffers[i].Get())};
        lists[i]->ResourceBarrier(2, barriers);
        tracker.OnResourceBarriers(lists[i].Get(), 2, barriers);
        assert(SUCCEEDED(lists[i]->Close()));
        auto token = capture(i);
        ExpectQueue(tracker, buffers[i].Get(), nullptr);
        auto* queue = i ? queueB.Get() : queueA.Get();
        ID3D12CommandList* submitted[] = {lists[i].Get()};
        queue->ExecuteCommandLists(1, submitted);
        tracker.CompleteSubmission(queue, token);
        assert(SUCCEEDED(queue->Signal(fence.Get(), i + 1)));
        assert(SUCCEEDED(fence->SetEventOnCompletion(i + 1, event)));
        assert(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0);
        ExpectQueue(tracker, buffers[i].Get(), queue);
    }
    ExpectQueue(tracker, buffers[0].Get(), queueA.Get());
    ExpectQueue(tracker, buffers[1].Get(), queueB.Get());
    ExpectQueue(tracker, buffers[2].Get(), nullptr);
    CloseHandle(event);

    fresh(); observe(0, 2); tracker.CompleteSubmission(&mockA, capture(0));
    ExpectQueue(tracker, buffers[0].Get(), nullptr); // Unrelated texture/auxiliary queue.
    observe(0, 0); auto token = capture(0);
    tracker.CompleteSubmission(&wrongDevice, token);
    tracker.CompleteSubmission(&mockA, token); // Rejected token cannot be replayed.
    ExpectQueue(tracker, buffers[0].Get(), nullptr);
    observe(0, 0); tracker.CompleteSubmission(&compute, capture(0));
    ExpectQueue(tracker, buffers[0].Get(), nullptr);

    observe(0, 0); tracker.OnReset(lists[0].Get());
    assert(capture(0).Count == 0);
    observe(0, 0); token = capture(0); tracker.OnReset(lists[0].Get());
    tracker.CompleteSubmission(&mockA, token);
    ExpectQueue(tracker, buffers[0].Get(), nullptr);
    observe(0, 0); token = capture(0);
    tracker.CompleteSubmission(&mockA, token);
    tracker.CompleteSubmission(&mockB, token); // Successful token also single-use.
    tracker.CompleteSubmission(&mockB, capture(0)); // Closed-list replay cannot win.
    ExpectQueue(tracker, buffers[0].Get(), &mockA);
    assert(mockA.References == 2);
    auto* owned = tracker.AcquireQueue(buffers[0].Get());
    assert(owned == &mockA && mockA.References == 3);
    owned->Release();
    observe(1, 0); tracker.CompleteSubmission(&mockB, capture(1));
    ExpectQueue(tracker, buffers[0].Get(), &mockB);
    assert(mockA.References == 1 && mockB.References == 2);
    observe(0, 1); tracker.CompleteSubmission(&mockA, capture(0));
    ExpectQueue(tracker, buffers[0].Get(), &mockB);
    ExpectQueue(tracker, buffers[1].Get(), &mockA);
    tracker.Clear();
    assert(mockA.References == 1 && mockB.References == 1);
    ExpectQueue(tracker, buffers[0].Get(), nullptr);

    fresh(); observe(0, 0); observe(1, 1);
    auto firstProof = capture(0);
    tracker.CompleteSubmission(&mockA, firstProof);
    tracker.CompleteSubmission(&mockB, capture(1));
    auto* freshQueue = tracker.TakeQueueForPresent(buffers[0].Get());
    assert(freshQueue == &mockA && mockA.References == 2);
    assert(tracker.TakeQueueForPresent(buffers[0].Get()) == nullptr);
    ExpectQueue(tracker, buffers[0].Get(), nullptr);
    ExpectQueue(tracker, buffers[1].Get(), &mockB); // Per-buffer consumption.
    tracker.CompleteSubmission(&mockB, firstProof);
    assert(tracker.TakeQueueForPresent(buffers[0].Get()) == nullptr);
    freshQueue->Release();
    assert(mockA.References == 1);
    observe(0, 0); tracker.CompleteSubmission(&mockB, capture(0));
    freshQueue = tracker.TakeQueueForPresent(buffers[0].Get());
    assert(freshQueue == &mockB); freshQueue->Release();
    tracker.Clear();
    assert(mockA.References == 1 && mockB.References == 1);

    fresh(); tracker.ExcludeCommandList(lists[0].Get());
    observe(0, 0); assert(capture(0).Count == 0);
    tracker.ExcludeCommandList(nullptr);
    observe(0, 0); token = capture(0); tracker.ExcludeCommandList(lists[0].Get());
    tracker.CompleteSubmission(&mockA, token);
    ExpectQueue(tracker, buffers[0].Get(), nullptr);
    fresh(); observe(0, 0); token = capture(0); fresh();
    tracker.CompleteSubmission(&mockA, token);
    ExpectQueue(tracker, buffers[0].Get(), nullptr);
    observe(0, 0); token = capture(0);
    assert(!tracker.SetTarget(device.Get(), targets, Tracker::MaxBuffers + 1));
    tracker.CompleteSubmission(&mockA, token);
    ExpectQueue(tracker, buffers[0].Get(), nullptr);

    fresh();
    for (auto flag : {D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY, D3D12_RESOURCE_BARRIER_FLAG_END_ONLY})
    {
        auto split = Transition(buffers[0].Get()); split.Flags = flag;
        tracker.OnResourceBarriers(lists[0].Get(), 1, &split);
        assert(capture(0).Count == 0);
    }
    observe(0, 0);
    auto away = Transition(buffers[0].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    tracker.OnResourceBarriers(lists[0].Get(), 1, &away);
    auto fallbackToken = capture(0);
    assert(fallbackToken.Count == 1);
    tracker.CompleteSubmission(&mockA, fallbackToken);
    ExpectQueue(tracker, buffers[0].Get(), nullptr); // Strict PRESENT proof remains absent.
    auto* fallbackQueue = tracker.TakeFallbackQueueForPresent(buffers[0].Get());
    assert(fallbackQueue == &mockA); fallbackQueue->Release();
    auto subresourceZero = Transition(buffers[0].Get()); subresourceZero.Transition.Subresource = 0;
    tracker.OnResourceBarriers(lists[0].Get(), 1, &subresourceZero);
    auto singleSubresourceToken = capture(0);
    assert(singleSubresourceToken.Count == 1); // This swapchain-like texture has exactly one subresource.
    tracker.CompleteSubmission(&mockA, singleSubresourceToken);
    ExpectQueue(tracker, buffers[0].Get(), &mockA);

    // Bounded tracking: slot recycling invalidates stale tokens; reservations
    // at capacity cannot be displaced by newly observed command lists.
    fresh();
    std::vector<Tracker::Submission> reservations;
    for (UINT i = 0; i < Tracker::MaxLists; ++i)
    {
        observe(i, 0); reservations.push_back(capture(i));
        assert(reservations.back().Count == 1);
    }
    observe(Tracker::MaxLists, 0);
    assert(capture(Tracker::MaxLists).Count == 0);
    tracker.CompleteSubmission(&mockA, reservations[0]);
    observe(Tracker::MaxLists, 0);
    auto replacement = capture(Tracker::MaxLists);
    assert(replacement.Count == 1);
    tracker.CompleteSubmission(&mockB, reservations[0]);
    ExpectQueue(tracker, buffers[0].Get(), &mockA);
    tracker.CompleteSubmission(&mockB, replacement);
    ExpectQueue(tracker, buffers[0].Get(), &mockB);

    // Concurrent observations/reset/acquisition/target retirement must preserve
    // COM ownership. Separate lists simulate independent native recording threads.
    fresh();
    std::vector<std::thread> workers;
    for (UINT i = 0; i < 4; ++i)
        workers.emplace_back([&, i] {
            for (UINT n = 0; n < 300; ++n)
            {
                tracker.OnReset(lists[i].Get()); observe(i, i % 2);
                auto submission = capture(i);
                tracker.CompleteSubmission(&mockA, submission);
                auto* selected = tracker.AcquireQueue(buffers[i % 2].Get());
                if (selected) { assert(selected == &mockA); selected->Release(); }
            }
        });
    workers.emplace_back([&] { for (UINT n = 0; n < 100; ++n) fresh(); });
    for (auto& worker : workers) worker.join();
    tracker.Clear();
    assert(mockA.References == 1 && mockB.References == 1 && wrongDevice.References == 1);
    if (info)
    {
        for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0; info->GetMessage(i, nullptr, &size);
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            info->GetMessage(i, message, &size);
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            { std::cerr << message->pDescription << '\n'; return 1; }
        }
    }
    std::cout << "Queue tracker passed: actual WARP submissions, wrong device/type, unrelated resources, Reset and replay, per-buffer queues, exclusions, stale target, bounded records, AddRef/Clear, concurrent lifecycle.\n";
}
