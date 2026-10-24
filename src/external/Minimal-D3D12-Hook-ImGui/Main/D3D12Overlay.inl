#include "mdx12_api.h"
#include "hooks.h"
#include "../../../internal/Menu/VibrancyBackdrop.h"
#include "../../../internal/Menu/DrawImGui.h"
#include "../../../internal/Menu/ConfigImGui.h"
#include "../Font/HarmonyOS_Sans_SC_Regular.h"
#include <wrl/client.h>
#include <array>
#include <algorithm>

namespace g_MDX12 {
    using Microsoft::WRL::ComPtr;
    PhoenixD3D12::SwapChainQueueTracker g_QueueTracker;
    namespace {
        ComPtr<IDXGISwapChain3> TargetChain;
        ComPtr<IUnknown> TargetIdentity;
        DXGI_FORMAT TargetFormat = DXGI_FORMAT_UNKNOWN;
        ULONGLONG LastTargetPresent = 0, QueueWaitStarted = 0;
        bool RenderFailed = false, UnfencedSubmission = false, FrameSubmittedLogged = false, QueueDiagnosticsLogged = false;
        std::array<bool, PhoenixVibrancy::SrvBase> SrvUsed{};
        std::string LastStatus;
        void Status(const char* Message, HRESULT Result = S_OK) {
            char Buffer[384]{};
            snprintf(Buffer, sizeof(Buffer), "Overlay: %s (HRESULT=0x%08lX)", Message, static_cast<unsigned long>(Result));
            if (LastStatus != Buffer) { LastStatus = Buffer; g_Hook::LogLifecycle(Buffer); }
        }
        bool DeviceRemoved() {
            return g_D3D12Resources::g_pd3dDevice && FAILED(g_D3D12Resources::g_pd3dDevice->GetDeviceRemovedReason());
        }
        bool SameObject(IUnknown* A, IUnknown* B) {
            if (!A || !B) return false;
            ComPtr<IUnknown> Left, Right;
            return SUCCEEDED(A->QueryInterface(IID_PPV_ARGS(&Left))) && SUCCEEDED(B->QueryInterface(IID_PPV_ARGS(&Right))) && Left.Get() == Right.Get();
        }
        template<class T> void Release(T*& Value) { if (Value) { Value->Release(); Value = nullptr; } }
        bool UploadsIdle(UINT Timeout) {
            return !ImGui::GetCurrentContext() || !ImGui::GetIO().BackendRendererUserData || ImGui_ImplDX12_WaitForPendingUploads(Timeout);
        }
        bool AllIdle(UINT Timeout) { return WaitForOverlayGpu(Timeout) && UploadsIdle(Timeout); }
        void BuildOverlayFonts() {
        ImGuiIO& io = ImGui::GetIO();
        const ImWchar* range = io.Fonts->GetGlyphRangesChineseFull();

        // 75%, 100%, 125%, 150%, 200% 对应的字体大小
        float fontSizes[] = { 14.0f, 18.0f, 22.0f, 26.0f, 34.0f };
        int safeIdx = std::clamp(g_Config::MenuScaleIdx, 0, 4);
        g_Config::MenuScaleIdx = safeIdx;

        char WindowsDir[MAX_PATH]{};
        GetWindowsDirectoryA(WindowsDir, MAX_PATH);
        const std::string FontDir = std::string(WindowsDir) + "/Fonts/";
        auto HasFont = [&](const char* Name) { return GetFileAttributesA((FontDir + Name).c_str()) != INVALID_FILE_ATTRIBUTES; };
        ImFontConfig Body;
        g_HarmonyOS_Sans_SC_Regular = HasFont("segoeui.ttf") ?
            io.Fonts->AddFontFromFileTTF((FontDir + "segoeui.ttf").c_str(), fontSizes[safeIdx], &Body, io.Fonts->GetGlyphRangesDefault()) : nullptr;
        ImFontConfig Chinese;
        Chinese.FontDataOwnedByAtlas = false;
        Chinese.MergeMode = g_HarmonyOS_Sans_SC_Regular != nullptr;
        ImFont* Fallback = io.Fonts->AddFontFromMemoryTTF((void*)g_Fonts::HarmonyOS_Sans_SC_Regular,
            sizeof(g_Fonts::HarmonyOS_Sans_SC_Regular), fontSizes[safeIdx], &Chinese, range);
        if (!g_HarmonyOS_Sans_SC_Regular) g_HarmonyOS_Sans_SC_Regular = Fallback;
        io.FontDefault = g_HarmonyOS_Sans_SC_Regular;
        g_DrawImGui::g_TitleFont = nullptr;
        if (HasFont("georgia.ttf")) {
            g_DrawImGui::g_TitleFont = io.Fonts->AddFontFromFileTTF((FontDir + "georgia.ttf").c_str(), fontSizes[safeIdx] * 1.2f);
            ImFontConfig SerifChinese; SerifChinese.MergeMode = true;
            if (HasFont("simsun.ttc")) io.Fonts->AddFontFromFileTTF((FontDir + "simsun.ttc").c_str(), fontSizes[safeIdx] * 1.2f, &SerifChinese, range);
            else {
                SerifChinese.FontDataOwnedByAtlas = false;
                io.Fonts->AddFontFromMemoryTTF((void*)g_Fonts::HarmonyOS_Sans_SC_Regular,
                    sizeof(g_Fonts::HarmonyOS_Sans_SC_Regular), fontSizes[safeIdx] * 1.2f, &SerifChinese, range);
            }
        }
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Dynamic atlas creation starts after RendererHasTextures is published.


            g_LoadedFontScaleIdx = safeIdx;
        }
        void ShutdownBackend() {
            if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX12_Shutdown();
            SrvUsed.fill(false);
        }
        void DestroyContext() {
            ShutdownBackend();
            if (ImGui::GetCurrentContext()) {
                if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
            }
            g_LoadedFontScaleIdx = -1;
        }
        bool InitializeBackend() {
            if (!ImGui::GetCurrentContext()) {
                ImGui::CreateContext(); g_DrawImGui::SetupCustomImGuiStyle();
                if (!ImGui_ImplWin32_Init(g_ProcessWindow::g_mainWindow)) return false;
                auto& IO = ImGui::GetIO(); IO.IniFilename = nullptr;
                IO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
                BuildOverlayFonts();
            }
            if (!ImGui::GetIO().BackendRendererUserData) {
                ImGui_ImplDX12_InitInfo Info{};
                Info.Device = g_D3D12Resources::g_pd3dDevice; Info.CommandQueue = g_D3D12Resources::g_pd3dCommandQueue;
                Info.NumFramesInFlight = static_cast<int>(g_D3D12Resources::g_bufferCount);
                Info.RTVFormat = TargetFormat; Info.SrvDescriptorHeap = g_D3D12Resources::g_pd3dSrvDescHeap;
                Info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* I, D3D12_CPU_DESCRIPTOR_HANDLE* CPU, D3D12_GPU_DESCRIPTOR_HANDLE* GPU) {
                    *CPU = {}; *GPU = {};
                    for (unsigned Slot = 0; Slot < SrvUsed.size(); ++Slot) if (!SrvUsed[Slot]) {
                        SrvUsed[Slot] = true;
                        const auto Increment = I->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                        *CPU = I->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(); CPU->ptr += SIZE_T(Slot) * Increment;
                        *GPU = I->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(); GPU->ptr += UINT64(Slot) * Increment; return;
                    }
                };
                Info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* I, D3D12_CPU_DESCRIPTOR_HANDLE CPU, D3D12_GPU_DESCRIPTOR_HANDLE) {
                    const auto Base = I->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
                    const auto Increment = I->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    if (CPU.ptr >= Base && Increment && (CPU.ptr - Base) % Increment == 0) {
                        const auto Slot = (CPU.ptr - Base) / Increment;
                        if (Slot < SrvUsed.size()) SrvUsed[Slot] = false;
                    }
                };
                if (!ImGui_ImplDX12_Init(&Info)) return false;
            }
            return true;
        }
        // Only after both the draw and texture-upload fences have completed.
        void ReleaseFrameResources() {
            g_InitState::g_Initialized = false; g_QueueTracker.Clear(); PhoenixVibrancy::Release(); DestroyContext();
            Release(g_D3D12Resources::g_pd3dCommandList); Release(g_D3D12Resources::g_pd3dRtvDescHeap); Release(g_D3D12Resources::g_pd3dSrvDescHeap);
            for (auto& F : g_D3D12Resources::g_FrameContexts) { Release(F.Resource); Release(F.CommandAllocator); }
            g_D3D12Resources::g_FrameContexts.clear(); g_D3D12Resources::g_bufferCount = 0;
            Release(g_D3D12Resources::g_fence);
            if (g_D3D12Resources::g_fenceEvent) { CloseHandle(g_D3D12Resources::g_fenceEvent); g_D3D12Resources::g_fenceEvent = nullptr; }
            g_D3D12Resources::g_fenceValue = 0;
            Release(g_D3D12Resources::g_pd3dCommandQueue); Release(g_D3D12Resources::g_pd3dDevice);
            UnfencedSubmission = false;
        }
        bool BindChain(IDXGISwapChain3* Chain) {
            DXGI_SWAP_CHAIN_DESC Desc{};
            const HRESULT DescResult = Chain->GetDesc(&Desc);
            if (FAILED(DescResult)) { Status("swapchain GetDesc failed", DescResult); return false; }
            if (!Desc.OutputWindow || !IsWindow(Desc.OutputWindow) ||
                !Desc.BufferCount || Desc.BufferCount > 16 || Desc.SampleDesc.Count != 1) { Status("swapchain window or buffer description rejected"); return false; }
            DWORD WindowPID = 0; GetWindowThreadProcessId(Desc.OutputWindow, &WindowPID);
            if (WindowPID != GetCurrentProcessId()) { Status("swapchain window belongs to another process"); return false; }
            if (g_ProcessWindow::g_mainWindow && g_ProcessWindow::g_mainWindow != Desc.OutputWindow && !inputhook::Remove(g_ProcessWindow::g_mainWindow)) return false;
            ComPtr<ID3D12Device> Device;
            std::vector<ComPtr<ID3D12Resource>> Buffers(Desc.BufferCount);
            std::vector<ID3D12Resource*> Raw(Desc.BufferCount);
            for (UINT I = 0; I < Desc.BufferCount; ++I) {
                const HRESULT BufferResult = Chain->GetBuffer(I, IID_PPV_ARGS(&Buffers[I]));
                if (FAILED(BufferResult)) { Status("swapchain GetBuffer failed", BufferResult); return false; }
                Raw[I] = Buffers[I].Get();
            }
            // Use the resource's actual device. A frame-generation interposer
            // may wrap the swapchain's GetDevice result with another identity.
            const HRESULT DeviceResult = Buffers[0]->GetDevice(IID_PPV_ARGS(&Device));
            if (FAILED(DeviceResult)) { Status("backbuffer GetDevice failed", DeviceResult); return false; }
            if (FAILED(Device->GetDeviceRemovedReason())) { Status("backbuffer device removed", Device->GetDeviceRemovedReason()); return false; }
            if (!g_QueueTracker.SetTarget(Device.Get(), Raw.data(), Desc.BufferCount)) return false;
            g_D3D12Resources::g_pd3dDevice = Device.Detach();
            g_D3D12Resources::g_bufferCount = Desc.BufferCount; g_D3D12Resources::g_FrameContexts.resize(Desc.BufferCount);
            for (UINT I = 0; I < Desc.BufferCount; ++I) g_D3D12Resources::g_FrameContexts[I].Resource = Buffers[I].Detach();
            TargetChain = Chain; Chain->QueryInterface(IID_PPV_ARGS(&TargetIdentity)); TargetFormat = Desc.BufferDesc.Format;
            g_ProcessWindow::g_mainWindow = Desc.OutputWindow;
            // Install the menu-key route as soon as the real game window is
            // known. Queue discovery may legitimately take several frames (or
            // use enhanced barriers), but F2 must not depend on GPU startup.
            inputhook::Init(g_ProcessWindow::g_mainWindow);
            RenderFailed = false; FrameSubmittedLogged = false; QueueDiagnosticsLogged = false;
            QueueWaitStarted = GetTickCount64();
            char Details[256]{};
            snprintf(Details, sizeof(Details), "Overlay binding: swapchain=%p device=%p hwnd=%p buffers=%u format=%u", Chain, g_D3D12Resources::g_pd3dDevice, Desc.OutputWindow, Desc.BufferCount, unsigned(TargetFormat));
            g_Hook::LogLifecycle(Details);
            Status("swapchain bound; waiting for native backbuffer submission to verify its queue"); return true;
        }
        bool CreateFrameResources() {
            using namespace g_D3D12Resources;
            if (!g_pd3dRtvDescHeap) {
                D3D12_DESCRIPTOR_HEAP_DESC D{}; D.NumDescriptors = g_bufferCount; D.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                if (FAILED(g_pd3dDevice->CreateDescriptorHeap(&D, IID_PPV_ARGS(&g_pd3dRtvDescHeap)))) return false;
            }
            if (!g_pd3dSrvDescHeap) {
                D3D12_DESCRIPTOR_HEAP_DESC D{}; D.NumDescriptors = PhoenixVibrancy::SrvBase + g_bufferCount;
                D.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; D.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                if (FAILED(g_pd3dDevice->CreateDescriptorHeap(&D, IID_PPV_ARGS(&g_pd3dSrvDescHeap)))) return false;
            }
            auto Handle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
            const auto Increment = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            for (auto& F : g_FrameContexts) {
                if (!F.CommandAllocator && FAILED(g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&F.CommandAllocator)))) return false;
                F.Descriptor = Handle; g_pd3dDevice->CreateRenderTargetView(F.Resource, nullptr, Handle); Handle.ptr += Increment;
            }
            if (!g_pd3dCommandList) {
                if (FAILED(g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_FrameContexts[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)))) return false;
                if (FAILED(g_pd3dCommandList->Close())) return false;
                g_QueueTracker.ExcludeCommandList(g_pd3dCommandList);
            }
            if (!g_fence && FAILED(g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) return false;
            if (!g_fenceEvent) g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!g_fenceEvent || !InitializeBackend()) return false;
            rawinputhook::Init(); cursorhook::Init(); g_InitState::g_Initialized = true; return true;
        }
    }
    bool WaitForOverlayGpu(UINT TimeoutMs) {
        using namespace g_D3D12Resources;
        if (DeviceRemoved()) return true;
        if (UnfencedSubmission) return false;
        if (!g_fence || !g_fenceValue) return true;
        auto Completed = g_fence->GetCompletedValue();
        if (Completed == UINT64_MAX) return DeviceRemoved();
        if (Completed >= g_fenceValue) return true;
        if (!TimeoutMs || !g_fenceEvent) return false;
        if (FAILED(g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent)) || WaitForSingleObject(g_fenceEvent, TimeoutMs) != WAIT_OBJECT_0) return false;
        Completed = g_fence->GetCompletedValue(); return Completed != UINT64_MAX && Completed >= g_fenceValue;
    }
    void RenderOverlayFrame(IDXGISwapChain3* Chain) {
        using namespace g_D3D12Resources;
        std::unique_lock<std::mutex> Lock(g_InitState::g_InitMutex, std::try_to_lock);
        if (!Lock || !Chain || g_Hook::IsStopping()) return;
        if (TargetChain && !SameObject(Chain, TargetIdentity.Get())) {
            if (GetTickCount64() - LastTargetPresent < 2000 || !AllIdle(0)) return;
            DXGI_SWAP_CHAIN_DESC Candidate{};
            if (FAILED(Chain->GetDesc(&Candidate)) || (IsWindow(g_ProcessWindow::g_mainWindow) && Candidate.OutputWindow != g_ProcessWindow::g_mainWindow)) return;
            ReleaseFrameResources(); TargetChain.Reset(); TargetIdentity.Reset();
        }
        if (!TargetChain && !BindChain(Chain)) return;
        LastTargetPresent = GetTickCount64();
        if (!g_bufferCount && !BindChain(Chain)) return;
        if (RenderFailed) return;
        if (DeviceRemoved()) { RenderFailed = true; Status("device removed; overlay submissions stopped", g_pd3dDevice->GetDeviceRemovedReason()); return; }
        const UINT Index = Chain->GetCurrentBackBufferIndex();
        if (Index >= g_FrameContexts.size()) { Status("invalid backbuffer index; frame skipped"); return; }
        auto& Frame = g_FrameContexts[Index];
        ComPtr<ID3D12CommandQueue> VerifiedQueue; VerifiedQueue.Attach(g_QueueTracker.TakeQueueForPresent(Frame.Resource));
        const auto QueueWaitNow = GetTickCount64();
        bool UsedTransitionFallback = false;
        if (!VerifiedQueue && QueueWaitStarted && QueueWaitNow - QueueWaitStarted >= 1000) {
            VerifiedQueue.Attach(g_QueueTracker.TakeFallbackQueueForPresent(Frame.Resource));
            UsedTransitionFallback = VerifiedQueue != nullptr;
            if (UsedTransitionFallback) {
                char FallbackDetails[192]{};
                snprintf(FallbackDetails, sizeof(FallbackDetails),
                    "Overlay queue compatibility: queue=%p buffer=%u resource=%p selected from latest exact backbuffer transition",
                    VerifiedQueue.Get(), Index, Frame.Resource);
                g_Hook::LogLifecycle(FallbackDetails);
            }
        }
        if (!VerifiedQueue) {
            const auto Now = QueueWaitNow;
            if (!QueueDiagnosticsLogged && QueueWaitStarted && Now - QueueWaitStarted >= 3000) {
                QueueDiagnosticsLogged = true;
                const auto D = g_QueueTracker.GetDiagnostics();
                char Details[1024]{};
                snprintf(Details, sizeof(Details),
                    "Overlay queue diagnostics after %llums: targetDevice=%p buffers=%u single=0x%04X execute=%llu direct=%llu lastQueue=%p type=%u owner=%p ownerHR=0x%08lX ownerMatch=%u reset=%llu capture=%llu listMatches=%llu entries=%llu accepted=%llu rejected=%llu legacy[calls=%llu target=%llu whole=%llu present=%llu] enhanced[calls=%llu textures=%llu target=%llu present=%llu]",
                    static_cast<unsigned long long>(Now - QueueWaitStarted), D.TargetDevice, D.BufferCount,
                    static_cast<unsigned>(D.SingleSubresourceBuffers),
                    static_cast<unsigned long long>(D.ExecuteCalls), static_cast<unsigned long long>(D.DirectExecuteCalls),
                    D.LastQueue, static_cast<unsigned>(D.LastQueueType), D.LastQueueOwner,
                    static_cast<unsigned long>(D.LastQueueOwnerResult), D.LastQueueOwnerMatches ? 1u : 0u,
                    static_cast<unsigned long long>(D.ResetCalls), static_cast<unsigned long long>(D.CaptureCalls),
                    static_cast<unsigned long long>(D.SubmittedListMatches), static_cast<unsigned long long>(D.CapturedEntries),
                    static_cast<unsigned long long>(D.AcceptedEntries), static_cast<unsigned long long>(D.RejectedEntries),
                    static_cast<unsigned long long>(D.LegacyBarrierCalls), static_cast<unsigned long long>(D.LegacyTargetBarriers),
                    static_cast<unsigned long long>(D.LegacyWholeTargetBarriers), static_cast<unsigned long long>(D.LegacyPresentTransitions),
                    static_cast<unsigned long long>(D.EnhancedBarrierCalls), static_cast<unsigned long long>(D.EnhancedTextureBarriers),
                    static_cast<unsigned long long>(D.EnhancedTargetBarriers), static_cast<unsigned long long>(D.EnhancedPresentTransitions));
                g_Hook::LogLifecycle(Details);
            }
            return;
        }
        if (!SameObject(VerifiedQueue.Get(), g_pd3dCommandQueue)) {
            if (!AllIdle(0)) return;
            ShutdownBackend(); Release(g_pd3dCommandQueue); g_pd3dCommandQueue = VerifiedQueue.Detach();
            char Details[160]{};
            snprintf(Details, sizeof(Details), "Overlay queue: queue=%p device=%p buffer=%u resource=%p", g_pd3dCommandQueue, g_pd3dDevice, Index, Frame.Resource);
            g_Hook::LogLifecycle(Details);
            Status(UsedTransitionFallback ?
                "backbuffer queue verified from exact transition compatibility proof; font uploads use the same queue" :
                "native backbuffer queue verified; font uploads use the same queue");
        }
        if (!g_InitState::g_Initialized && !CreateFrameResources()) { RenderFailed = true; Status("resource initialization failed; submissions stopped"); return; }
        if (!UploadsIdle(0)) return;
        // Never reset an allocator or alter font resources still used by the GPU.
        const auto Completed = g_fence->GetCompletedValue();
        if (Completed == UINT64_MAX || Completed < Frame.FenceValue) return;
        if (g_LoadedFontScaleIdx != std::clamp(g_Config::MenuScaleIdx, 0, 4)) {
            if (!AllIdle(0)) return;
            DestroyContext();
        }
        if (!InitializeBackend()) return;
        ImGui_ImplDX12_NewFrame(); if (!ImGui_ImplDX12_IsReady()) return;
        ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        inputhook::UpdateInputBlockState(); cursorhook::UpdateCursorState();
        ImGui::GetIO().MouseDrawCursor = g_MenuState::g_isOpen;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        if (!ImGui_ImplDX12_SetFrameResourceIndex(Index)) { ImGui::EndFrame(); return; }
        HRESULT HR = Frame.CommandAllocator->Reset();
        if (SUCCEEDED(HR)) HR = g_pd3dCommandList->Reset(Frame.CommandAllocator, nullptr);
        if (FAILED(HR)) { ImGui::EndFrame(); RenderFailed = true; Status("command reset failed; submissions stopped", HR); return; }
        D3D12_RESOURCE_BARRIER Barrier{}; Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition = {Frame.Resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET};
        g_pd3dCommandList->ResourceBarrier(1, &Barrier); g_pd3dCommandList->OMSetRenderTargets(1, &Frame.Descriptor, FALSE, nullptr);
        ID3D12DescriptorHeap* Heaps[] = {g_pd3dSrvDescHeap}; g_pd3dCommandList->SetDescriptorHeaps(1, Heaps);
        const bool SceneWasReadable = Index < PhoenixVibrancy::Frames.size() && PhoenixVibrancy::Frames[Index].Readable;
        const auto DiscardSceneCopy = [&]() { if (Index < PhoenixVibrancy::Frames.size()) PhoenixVibrancy::Frames[Index].Readable = SceneWasReadable; };
        PhoenixVibrancy::Capture(g_pd3dDevice, g_pd3dCommandList, Frame.Resource, g_pd3dSrvDescHeap, Index, g_bufferCount, g_MenuState::g_isOpen);
        SetupImGui(Chain, 0, 0); ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
        std::swap(Barrier.Transition.StateBefore, Barrier.Transition.StateAfter); g_pd3dCommandList->ResourceBarrier(1, &Barrier);
        HR = g_pd3dCommandList->Close();
        if (FAILED(HR)) { DiscardSceneCopy(); RenderFailed = true; Status("command close failed; submissions stopped", HR); return; }
        if (!ImGui_ImplDX12_IsReady() || !ImGui_ImplDX12_LastRenderSucceeded()) { DiscardSceneCopy(); return; }
        ID3D12CommandList* Lists[] = {g_pd3dCommandList}; g_HookFunctions::g_oExecuteCommandLists(g_pd3dCommandQueue, 1, Lists);
        const auto Value = ++g_fenceValue; HR = g_pd3dCommandQueue->Signal(g_fence, Value);
        if (FAILED(HR)) { UnfencedSubmission = RenderFailed = true; Status("queue signal failed; all overlay resources retained", HR); return; }
        Frame.FenceValue = Value;
        if (!FrameSubmittedLogged) {
            FrameSubmittedLogged = true;
            inputhook::Init(g_ProcessWindow::g_mainWindow);
            Status("first frame submitted on verified queue");
        }
    }
    HRESULT ResizeOverlayBuffers(IDXGISwapChain* Chain, UINT Count, UINT Width, UINT Height, DXGI_FORMAT Format, UINT Flags) {
        std::unique_lock<std::mutex> Lock(g_InitState::g_InitMutex);
        if (TargetChain && SameObject(Chain, TargetIdentity.Get())) {
            if (!AllIdle(5000)) { Status("resize deferred; GPU resources still in use"); return DXGI_ERROR_WAS_STILL_DRAWING; }
            ReleaseFrameResources(); TargetChain.Reset(); TargetIdentity.Reset();
        }
        return g_HookFunctions::g_oResizeBuffers ? g_HookFunctions::g_oResizeBuffers(Chain, Count, Width, Height, Format, Flags) : S_OK;
    }
    void CleanupRenderResources() { if (AllIdle(5000)) ReleaseFrameResources(); }
    void CleanupRenderResources_NoInput() { if (AllIdle(5000)) ReleaseFrameResources(); }
    bool FinalCleanupAll() {
        std::lock_guard<std::mutex> Lock(g_InitState::g_InitMutex);
        if (!AllIdle(5000)) return false;
        ReleaseFrameResources(); TargetChain.Reset(); TargetIdentity.Reset(); return inputhook::RemoveAll();
    }
    void InitProcessName() {}
    bool Initialize(LPVOID Parameter) { return MainThread(Parameter) == 1; }
}
extern "C" {
    __declspec(dllexport) void SetOverlayWaitTimeout(UINT Milliseconds) { g_MDX12::g_InitState::g_waitTimeoutMs = Milliseconds; }
    __declspec(dllexport) void SetSetupImGuiCallback(g_MDX12::SetupImGuiCallback Callback) { g_MDX12::SetSetupImGuiCallback(Callback); }
}
