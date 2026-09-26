#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <mutex>

namespace PhoenixD3D12
{
// Observes native command-list recordings. It never submits work or reads
// private D3D object memory. A missing proof deliberately yields no queue.
class SwapChainQueueTracker
{
public:
    static constexpr UINT MaxBuffers = 16;
    static constexpr UINT MaxLists = 256;

    struct Submission
    {
        struct Entry
        {
            UINT Slot = 0;
            std::uint64_t Generation = 0;
            std::uint64_t Reservation = 0;
            std::uint16_t Buffers = 0;
            std::uint16_t TouchedBuffers = 0;
        };
        std::uint64_t TargetEpoch = 0;
        UINT Count = 0;
        std::array<Entry, MaxLists> Entries{};
    };

    struct Diagnostics
    {
        std::uint64_t ResetCalls = 0;
        std::uint64_t LegacyBarrierCalls = 0;
        std::uint64_t LegacyTargetBarriers = 0;
        std::uint64_t LegacyWholeTargetBarriers = 0;
        std::uint64_t LegacyPresentTransitions = 0;
        std::uint64_t EnhancedBarrierCalls = 0;
        std::uint64_t EnhancedTextureBarriers = 0;
        std::uint64_t EnhancedTargetBarriers = 0;
        std::uint64_t EnhancedPresentTransitions = 0;
        std::uint64_t ExecuteCalls = 0;
        std::uint64_t DirectExecuteCalls = 0;
        std::uint64_t CaptureCalls = 0;
        std::uint64_t SubmittedListMatches = 0;
        std::uint64_t CapturedEntries = 0;
        std::uint64_t AcceptedEntries = 0;
        std::uint64_t RejectedEntries = 0;
        void* TargetDevice = nullptr;
        void* LastQueue = nullptr;
        void* LastQueueOwner = nullptr;
        HRESULT LastQueueOwnerResult = E_PENDING;
        D3D12_COMMAND_LIST_TYPE LastQueueType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        bool LastQueueOwnerMatches = false;
        UINT BufferCount = 0;
        std::uint16_t SingleSubresourceBuffers = 0;
    };

    SwapChainQueueTracker() = default;
    SwapChainQueueTracker(const SwapChainQueueTracker&) = delete;
    SwapChainQueueTracker& operator=(const SwapChainQueueTracker&) = delete;

    // Call when installing a new swap chain/buffer set, never every Present.
    // Even an invalid replacement retires all previous pointers and proofs.
    bool SetTarget(ID3D12Device* device, ID3D12Resource* const* buffers, UINT count)
    {
        std::lock_guard<std::mutex> guard(Mutex);
        ClearLocked();
        if (!device || !buffers || !count || count > MaxBuffers)
            return false;
        Microsoft::WRL::ComPtr<IUnknown> identity;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&identity))))
            return false;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, MaxBuffers> targets;
        for (UINT i = 0; i < count; ++i)
        {
            if (!buffers[i])
                return false;
            Microsoft::WRL::ComPtr<IUnknown> owner;
            if (FAILED(buffers[i]->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != identity.Get())
                return false;
            for (UINT previous = 0; previous < i; ++previous)
                if (buffers[i] == buffers[previous])
                    return false;
            const auto description = buffers[i]->GetDesc();
            D3D12_FEATURE_DATA_FORMAT_INFO formatInfo{};
            formatInfo.Format = description.Format;
            UINT planes = 1;
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,
                &formatInfo, sizeof(formatInfo))) && formatInfo.PlaneCount)
                planes = formatInfo.PlaneCount;
            if (description.MipLevels == 1 && description.DepthOrArraySize == 1 && planes == 1)
                SingleSubresourceBuffers |= static_cast<std::uint16_t>(1u << i);
            targets[i] = buffers[i];
        }
        DeviceIdentity = identity;
        Buffers = targets;
        BufferCount = count;
        Stats.TargetDevice = identity.Get();
        Stats.BufferCount = count;
        Stats.SingleSubresourceBuffers = SingleSubresourceBuffers;
        return true;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> guard(Mutex);
        ClearLocked();
    }

    // The renderer has one reusable overlay list. Replacement releases the
    // old exclusion. Set this again after SetTarget/Clear if appropriate.
    void ExcludeCommandList(ID3D12CommandList* list)
    {
        std::lock_guard<std::mutex> guard(Mutex);
        Excluded = list;
        for (auto& record : Lists)
            if (record.List.Get() == list)
                record = {};
    }

    // Call after a successful native Reset. Failed Reset keeps the native
    // recording intact and must not call this method.
    void OnReset(ID3D12GraphicsCommandList* list)
    {
        if (!list)
            return;
        std::lock_guard<std::mutex> guard(Mutex);
        if (BufferCount)
            ++Stats.ResetCalls;
        Microsoft::WRL::ComPtr<IUnknown> identity;
        list->QueryInterface(IID_PPV_ARGS(&identity));
        for (auto& record : Lists)
        {
            if (record.List.Get() == list || (identity && record.Identity.Get() == identity.Get()))
            {
                record.Generation = NextSerial();
                record.PresentBuffers = 0;
                record.TouchedBuffers = 0;
                record.Reservation = 0;
                return;
            }
        }
    }

    // Call after the native ResourceBarrier. Only a final, non-split,
    // all-subresource transition into PRESENT constitutes buffer evidence.
    void OnResourceBarriers(ID3D12GraphicsCommandList* list, UINT count,
                            const D3D12_RESOURCE_BARRIER* barriers)
    {
        if (!list || !barriers || !count)
            return;
        std::lock_guard<std::mutex> guard(Mutex);
        if (!BufferCount || Excluded.Get() == list)
            return;
        ++Stats.LegacyBarrierCalls;
        ListRecord* record = nullptr;
        for (UINT i = 0; i < count; ++i)
        {
            const auto& barrier = barriers[i];
            if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
                continue;
            UINT buffer = 0;
            for (; buffer < BufferCount; ++buffer)
                if (Buffers[buffer].Get() == barrier.Transition.pResource)
                    break;
            if (buffer == BufferCount)
                continue;
            ++Stats.LegacyTargetBarriers;
            if (!record)
                record = FindOrCreateLocked(list);
            if (!record)
                return;
            // Further recording invalidates any captured but incomplete use.
            record->Reservation = 0;
            const auto bit = static_cast<std::uint16_t>(1u << buffer);
            // Subresource zero covers the complete resource when the public
            // resource description proves that this swapchain buffer has only
            // one mip, array slice, and plane.
            const bool wholeTransition =
                barrier.Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
                (barrier.Transition.Subresource == 0 && (SingleSubresourceBuffers & bit));
            if (barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_NONE && wholeTransition)
            {
                record->TouchedBuffers |= bit;
                ++Stats.LegacyWholeTargetBarriers;
            }
            record->PresentBuffers &= static_cast<std::uint16_t>(~bit);
            if (barrier.Flags == D3D12_RESOURCE_BARRIER_FLAG_NONE && wholeTransition &&
                barrier.Transition.StateAfter == D3D12_RESOURCE_STATE_PRESENT)
            {
                record->PresentBuffers |= bit;
                ++Stats.LegacyPresentTransitions;
            }
        }
    }

    // Enhanced barriers are used by newer D3D12 render paths. Treat a
    // texture layout transition into PRESENT as the same per-buffer evidence
    // as a legacy ResourceBarrier transition.
    void OnEnhancedBarriers(ID3D12GraphicsCommandList* list, UINT32 groupCount,
                            const D3D12_BARRIER_GROUP* groups)
    {
        if (!list || !groups || !groupCount)
            return;
        std::lock_guard<std::mutex> guard(Mutex);
        if (!BufferCount || Excluded.Get() == list)
            return;
        ++Stats.EnhancedBarrierCalls;
        ListRecord* record = nullptr;
        for (UINT32 groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            const auto& group = groups[groupIndex];
            if (group.Type != D3D12_BARRIER_TYPE_TEXTURE ||
                !group.pTextureBarriers || !group.NumBarriers)
                continue;
            for (UINT32 barrierIndex = 0; barrierIndex < group.NumBarriers; ++barrierIndex)
            {
                ++Stats.EnhancedTextureBarriers;
                const auto& barrier = group.pTextureBarriers[barrierIndex];
                UINT buffer = 0;
                for (; buffer < BufferCount; ++buffer)
                    if (Buffers[buffer].Get() == barrier.pResource)
                        break;
                if (buffer == BufferCount)
                    continue;
                ++Stats.EnhancedTargetBarriers;
                if (!record)
                    record = FindOrCreateLocked(list);
                if (!record)
                    return;
                record->Reservation = 0;
                const auto bit = static_cast<std::uint16_t>(1u << buffer);
                record->TouchedBuffers |= bit;
                record->PresentBuffers &= static_cast<std::uint16_t>(~bit);
                if (barrier.LayoutAfter == D3D12_BARRIER_LAYOUT_PRESENT)
                {
                    record->PresentBuffers |= bit;
                    ++Stats.EnhancedPresentTransitions;
                }
            }
        }
    }

    // Record enough public queue metadata to diagnose why exact backbuffer
    // evidence was not accepted. No queue is retained by this diagnostic path.
    void ObserveExecution(ID3D12CommandQueue* queue)
    {
        D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Microsoft::WRL::ComPtr<IUnknown> owner;
        HRESULT ownerResult = E_POINTER;
        if (queue)
        {
            type = queue->GetDesc().Type;
            ownerResult = queue->GetDevice(IID_PPV_ARGS(&owner));
        }
        std::lock_guard<std::mutex> guard(Mutex);
        if (!BufferCount)
            return;
        ++Stats.ExecuteCalls;
        if (type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            ++Stats.DirectExecuteCalls;
        Stats.LastQueue = queue;
        Stats.LastQueueType = type;
        Stats.LastQueueOwner = owner.Get();
        Stats.LastQueueOwnerResult = ownerResult;
        Stats.LastQueueOwnerMatches = SUCCEEDED(ownerResult) && DeviceIdentity &&
            owner.Get() == DeviceIdentity.Get();
    }

    Diagnostics GetDiagnostics()
    {
        std::lock_guard<std::mutex> guard(Mutex);
        return Stats;
    }

    // Call immediately BEFORE forwarding ExecuteCommandLists. Recording
    // evidence is consumed here; re-executing an old closed list is not proof.
    Submission CaptureSubmission(UINT count, ID3D12CommandList* const* lists)
    {
        Submission submission;
        if (!lists || !count)
            return submission;
        std::lock_guard<std::mutex> guard(Mutex);
        if (!BufferCount)
            return submission;
        ++Stats.CaptureCalls;
        submission.TargetEpoch = TargetEpoch;
        for (UINT i = 0; i < count; ++i)
        {
            if (!lists[i] || lists[i] == Excluded.Get())
                continue;
            Microsoft::WRL::ComPtr<IUnknown> submittedIdentity;
            lists[i]->QueryInterface(IID_PPV_ARGS(&submittedIdentity));
            for (UINT slot = 0; slot < MaxLists; ++slot)
            {
                auto& record = Lists[slot];
                const bool sameList = record.List.Get() == lists[i] ||
                    (submittedIdentity && record.Identity.Get() == submittedIdentity.Get());
                if (!sameList)
                    continue;
                ++Stats.SubmittedListMatches;
                if (!record.PresentBuffers && !record.TouchedBuffers)
                    break;
                if (submission.Count == MaxLists)
                    return submission;
                record.Reservation = NextSerial();
                submission.Entries[submission.Count++] =
                    {slot, record.Generation, record.Reservation, record.PresentBuffers, record.TouchedBuffers};
                ++Stats.CapturedEntries;
                record.PresentBuffers = 0;
                record.TouchedBuffers = 0;
                break;
            }
        }
        return submission;
    }

    // Call AFTER the original ExecuteCommandLists returns. No queue becomes
    // visible until its DIRECT type and canonical COM device identity match.
    // Every reservation is single-use, including when validation fails.
    void CompleteSubmission(ID3D12CommandQueue* queue, const Submission& submission)
    {
        if (!submission.Count || submission.Count > MaxLists)
            return;
        Microsoft::WRL::ComPtr<IUnknown> owner;
        const bool direct = queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
        const bool hasOwner = direct && SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&owner)));
        std::lock_guard<std::mutex> guard(Mutex);
        if (submission.TargetEpoch != TargetEpoch)
            return;
        const bool valid = hasOwner && DeviceIdentity && owner.Get() == DeviceIdentity.Get();
        for (UINT i = 0; i < submission.Count; ++i)
        {
            const auto& entry = submission.Entries[i];
            if (entry.Slot >= MaxLists)
                continue;
            auto& record = Lists[entry.Slot];
            if (record.Generation != entry.Generation || !entry.Reservation ||
                record.Reservation != entry.Reservation)
                continue;
            record.Reservation = 0;
            if (!valid)
            {
                ++Stats.RejectedEntries;
                continue;
            }
            ++Stats.AcceptedEntries;
            for (UINT buffer = 0; buffer < BufferCount; ++buffer)
            {
                const auto bit = static_cast<std::uint16_t>(1u << buffer);
                if (entry.Buffers & bit)
                {
                    Queues[buffer] = queue;
                    FallbackQueues[buffer].Reset();
                }
                else if (entry.TouchedBuffers & bit)
                    FallbackQueues[buffer] = queue;
            }
        }
    }

    // The caller owns the returned AddRef and must Release it. Selection is
    // per exact buffer; a different buffer cannot borrow another's proof.
    ID3D12CommandQueue* AcquireQueue(ID3D12Resource* buffer)
    {
        std::lock_guard<std::mutex> guard(Mutex);
        for (UINT i = 0; i < BufferCount; ++i)
        {
            if (Buffers[i].Get() == buffer && Queues[i])
            {
                Queues[i]->AddRef();
                return Queues[i].Get();
            }
        }
        return nullptr;
    }

    // Rendering requires fresh native evidence for EACH Present. Transfer the
    // stored owning reference and consume this buffer's proof, so an enhanced
    // barrier/unobserved subsequent recording cannot inherit an earlier queue.
    ID3D12CommandQueue* TakeQueueForPresent(ID3D12Resource* buffer)
    {
        std::lock_guard<std::mutex> guard(Mutex);
        for (UINT i = 0; i < BufferCount; ++i)
            if (Buffers[i].Get() == buffer)
                return Queues[i].Detach();
        return nullptr;
    }

    // Compatibility path for D3D12 interposers that touch the exact native
    // backbuffer but do not expose a canonical transition into PRESENT.
    ID3D12CommandQueue* TakeFallbackQueueForPresent(ID3D12Resource* buffer)
    {
        std::lock_guard<std::mutex> guard(Mutex);
        for (UINT i = 0; i < BufferCount; ++i)
            if (Buffers[i].Get() == buffer)
                return FallbackQueues[i].Detach();
        return nullptr;
    }

private:
    struct ListRecord
    {
        Microsoft::WRL::ComPtr<ID3D12CommandList> List;
        Microsoft::WRL::ComPtr<IUnknown> Identity;
        std::uint64_t Generation = 0;
        std::uint64_t Reservation = 0;
        std::uint16_t PresentBuffers = 0;
        std::uint16_t TouchedBuffers = 0;
    };

    std::uint64_t NextSerial() { return ++Serial; }

    void ClearLocked()
    {
        TargetEpoch = NextSerial();
        BufferCount = 0;
        SingleSubresourceBuffers = 0;
        Stats = {};
        Excluded.Reset();
        for (auto& record : Lists)
            record = {};
        for (auto& queue : Queues)
            queue.Reset();
        for (auto& queue : FallbackQueues)
            queue.Reset();
        for (auto& buffer : Buffers)
            buffer.Reset();
        DeviceIdentity.Reset();
        NextSlot = 0;
    }

    ListRecord* FindOrCreateLocked(ID3D12CommandList* list)
    {
        Microsoft::WRL::ComPtr<IUnknown> identity;
        list->QueryInterface(IID_PPV_ARGS(&identity));
        for (auto& record : Lists)
            if (record.List.Get() == list || (identity && record.Identity.Get() == identity.Get()))
                return &record;
        // Pin tracked native lists with AddRef so a freed pointer cannot be
        // mistaken for an old recording. Recycle only non-reserved slots.
        for (UINT i = 0; i < MaxLists; ++i)
        {
            const UINT slot = (NextSlot + i) % MaxLists;
            if (Lists[slot].Reservation)
                continue;
            auto& record = Lists[slot];
            record = {};
            record.List = list;
            record.Identity = identity;
            record.Generation = NextSerial();
            NextSlot = (slot + 1) % MaxLists;
            return &record;
        }
        return nullptr;
    }

    std::mutex Mutex;
    std::uint64_t Serial = 0;
    std::uint64_t TargetEpoch = 0;
    UINT BufferCount = 0;
    UINT NextSlot = 0;
    std::uint16_t SingleSubresourceBuffers = 0;
    Diagnostics Stats{};
    Microsoft::WRL::ComPtr<IUnknown> DeviceIdentity;
    Microsoft::WRL::ComPtr<ID3D12CommandList> Excluded;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, MaxBuffers> Buffers;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandQueue>, MaxBuffers> Queues;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandQueue>, MaxBuffers> FallbackQueues;
    std::array<ListRecord, MaxLists> Lists;
};
}
