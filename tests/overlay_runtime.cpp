// Runs the actual production overlay helper against hidden WARP swap chains.
// Game callbacks and input hooks are stubbed; no game is started or inspected.
#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include <d3d12sdklayers.h>
#include "../src/external/Minimal-D3D12-Hook-ImGui/MinHook/include/MinHook.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

namespace OverlayTest
{
    bool BypassPresentation = false;
    UINT PresentationDraws = 0;
    UINT PresentCalls = 0;
    UINT SeenSync = 0, SeenFlags = 0;
    const DXGI_PRESENT_PARAMETERS* SeenParameters = nullptr;
    bool DropNextDraw = false;
    bool DrawWasDropped = false;
    bool DropMakesBackendNotReady = true;
    UINT Submissions = 0;
    UINT UiFrames = 0;
    UINT InputInstalls = 0;
    void RenderDrawData(ImDrawData* data, ID3D12GraphicsCommandList* list)
    {
        if (DropNextDraw) { DropNextDraw = false; DrawWasDropped = true; return; }
        ImGui_ImplDX12_RenderDrawData(data, list);
    }
    bool IsReady() { return !(DrawWasDropped && DropMakesBackendNotReady) && ImGui_ImplDX12_IsReady(); }
    bool LastRenderSucceeded()
    {
        return !(DrawWasDropped && !DropMakesBackendNotReady) && ImGui_ImplDX12_LastRenderSucceeded();
    }
}

// Replace only these calls inside the production helper for deterministic
// draw failure; the real backend remains compiled and is used for normal draws.
#define ImGui_ImplDX12_RenderDrawData OverlayTest::RenderDrawData
#define ImGui_ImplDX12_IsReady OverlayTest::IsReady
#define ImGui_ImplDX12_LastRenderSucceeded OverlayTest::LastRenderSucceeded
#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/D3D12Overlay.inl"
#undef ImGui_ImplDX12_RenderDrawData
#undef ImGui_ImplDX12_IsReady
#undef ImGui_ImplDX12_LastRenderSucceeded

namespace g_Hook
{
    CallbackScope::CallbackScope() {}
    CallbackScope::~CallbackScope() {}
    bool IsStopping() { return false; }
    bool IsUnloadRequested() { return false; }
    void LogLifecycle(const char* message) { std::cout << message << '\n'; }
}
namespace g_MDX12
{
    namespace inputhook
    {
        void Init(HWND) { ++OverlayTest::InputInstalls; }
        bool Remove(HWND) { return true; }
        bool RemoveAll() { return true; }
        void UpdateInputBlockState() {}
    }
    namespace rawinputhook { void Init() {} }
    namespace cursorhook { void Init() {} void UpdateCursorState() {} }
    DWORD WINAPI MainThread(LPVOID) { return 1; }
    void SetSetupImGuiCallback(SetupImGuiCallback) {}
    void SetupImGui(IDXGISwapChain3*, UINT, UINT)
    {
        ++OverlayTest::UiFrames;
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({220, 120}, ImGuiCond_Always);
        ImGui::Begin("WARP overlay runtime", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("Production draw and font atlas");
        ImGui::End();
    }
}

namespace g_MDX12 {
    bool ShouldBypassFeatures() { return OverlayTest::BypassPresentation; }
    void CountPresentedFrame(IDXGISwapChain3* Chain) {
        ++OverlayTest::PresentationDraws;
        RenderOverlayFrame(Chain);
    }
#define RenderOverlayFrame CountPresentedFrame
#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/PresentationHooks.inl"
#undef RenderOverlayFrame
}

using Microsoft::WRL::ComPtr;

// Keep failed assertions visible in redirected/headless runner logs.
namespace OverlayTest {
    [[noreturn]] void AssertionFailed(const char* expression, int line) {
        std::cerr << "overlay_runtime.cpp:" << line << ": assertion failed: " << expression << std::endl;
        std::abort();
    }
}
#undef assert
#define assert(expression) ((expression) ? static_cast<void>(0) : OverlayTest::AssertionFailed(#expression, __LINE__))

struct Fixture
{
    ComPtr<IDXGIFactory4> Factory;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue, OtherQueue;
    ComPtr<ID3D12Fence> NativeFence;
    ComPtr<ID3D12CommandAllocator> NativeAllocator;
    ComPtr<ID3D12GraphicsCommandList> NativeList;
    ComPtr<IDXGISwapChain3> Chain, Secondary;
    ComPtr<ID3D12InfoQueue> Info;
    UINT64 NativeValue = 0;
    HANDLE NativeEvent = nullptr;
    HWND Window = nullptr, OtherWindow = nullptr;

    Fixture()
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&Factory))));
        ComPtr<IDXGIAdapter> adapter;
        assert(SUCCEEDED(Factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
        assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))));
        Device.As(&Info);
        std::cout << "D3D12 debug layer available: " << (debug ? "yes" : "no")
            << "; InfoQueue available: " << (Info ? "yes" : "no") << '\n';
        D3D12_COMMAND_QUEUE_DESC desc{};
        assert(SUCCEEDED(Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&Queue))));
        assert(SUCCEEDED(Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&OtherQueue))));
        assert(SUCCEEDED(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&NativeFence))));
        assert(SUCCEEDED(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&NativeAllocator))));
        assert(SUCCEEDED(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, NativeAllocator.Get(), nullptr, IID_PPV_ARGS(&NativeList))));
        assert(SUCCEEDED(NativeList->Close()));
        NativeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); assert(NativeEvent);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"PhoenixOverlayWarpTest";
        assert(RegisterClassW(&windowClass));
        Window = CreateWindowW(windowClass.lpszClassName, L"Hidden WARP test", WS_OVERLAPPEDWINDOW,
            0, 0, 384, 256, nullptr, nullptr, windowClass.hInstance, nullptr);
        OtherWindow = CreateWindowW(windowClass.lpszClassName, L"Hidden secondary chain", WS_OVERLAPPEDWINDOW,
            0, 0, 384, 256, nullptr, nullptr, windowClass.hInstance, nullptr);
        assert(Window && OtherWindow);
        Chain = CreateChain(Window); Secondary = CreateChain(OtherWindow);
        g_MDX12::g_HookFunctions::g_oExecuteCommandLists = [](ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
            ++OverlayTest::Submissions; queue->ExecuteCommandLists(count, lists);
        };
        g_MDX12::g_HookFunctions::g_oResizeBuffers = [](IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
            return chain->ResizeBuffers(count, width, height, format, flags);
        };
    }
    ComPtr<IDXGISwapChain3> CreateChain(HWND window)
    {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 320; desc.Height = 180; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> base; ComPtr<IDXGISwapChain3> chain;
        assert(SUCCEEDED(Factory->CreateSwapChainForHwnd(Queue.Get(), window, &desc, nullptr, nullptr, &base)));
        assert(SUCCEEDED(base.As(&chain))); return chain;
    }
    void WaitNative()
    {
        if (!NativeValue || NativeFence->GetCompletedValue() >= NativeValue) return;
        assert(SUCCEEDED(NativeFence->SetEventOnCompletion(NativeValue, NativeEvent)));
        assert(WaitForSingleObject(NativeEvent, 10000) == WAIT_OBJECT_0);
    }
    // Mirrors the two native hooks around actual WARP ResourceBarrier/Execute.
    void NativeFrame(bool wait = true)
    {
        WaitNative();
        assert(SUCCEEDED(NativeAllocator->Reset()));
        assert(SUCCEEDED(NativeList->Reset(NativeAllocator.Get(), nullptr)));
        g_MDX12::g_QueueTracker.OnReset(NativeList.Get());
        ComPtr<ID3D12Resource> buffer;
        assert(SUCCEEDED(Chain->GetBuffer(Chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer))));
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for (auto& barrier : barriers)
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        NativeList->ResourceBarrier(2, barriers);
        g_MDX12::g_QueueTracker.OnResourceBarriers(NativeList.Get(), 2, barriers);
        assert(SUCCEEDED(NativeList->Close()));
        ID3D12CommandList* lists[] = {NativeList.Get()};
        auto token = g_MDX12::g_QueueTracker.CaptureSubmission(1, lists);
        Queue->ExecuteCommandLists(1, lists);
        g_MDX12::g_QueueTracker.CompleteSubmission(Queue.Get(), token);
        assert(SUCCEEDED(Queue->Signal(NativeFence.Get(), ++NativeValue)));
        if (wait) WaitNative();
    }
    void DrawUntilSubmitted()
    {
        const UINT start = OverlayTest::Submissions;
        for (UINT attempt = 0; attempt < 12 && OverlayTest::Submissions == start; ++attempt)
        {
            NativeFrame(); g_MDX12::RenderOverlayFrame(Chain.Get());
            assert(g_MDX12::WaitForOverlayGpu(10000));
            assert(ImGui_ImplDX12_WaitForPendingUploads(10000));
        }
        assert(OverlayTest::Submissions > start);
    }
    void CheckDebugErrors()
    {
        if (!Info) return;
        for (UINT64 i = 0; i < Info->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0; Info->GetMessage(i, nullptr, &size);
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Info->GetMessage(i, message, &size);
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            { std::cerr << message->pDescription << '\n'; assert(false); }
        }
    }
    ~Fixture()
    {
        std::cout << "WARP cleanup: waiting for native work.\n";
        WaitNative(); assert(g_MDX12::FinalCleanupAll());
        std::cout << "WARP cleanup: overlay resources released.\n";
        CheckDebugErrors(); Chain.Reset(); Secondary.Reset();
        DestroyWindow(Window); DestroyWindow(OtherWindow);
        UnregisterClassW(L"PhoenixOverlayWarpTest", GetModuleHandleW(nullptr));
        CloseHandle(NativeEvent);
        Info.Reset(); NativeList.Reset(); NativeAllocator.Reset(); NativeFence.Reset();
        OtherQueue.Reset(); Queue.Reset();
        std::cout << "WARP cleanup: queues and native resources released.\n";
        Device.Reset();
        std::cout << "WARP cleanup: device released.\n";
        Factory.Reset();
        std::cout << "WARP cleanup: factory released.\n";
    }
};

int main()
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::atexit([] { std::cout << "WARP test reached normal CRT exit.\n"; });
    Fixture fixture;
    using namespace g_MDX12;
    using namespace g_MDX12::g_D3D12Resources;
    DXGI_PRESENT_PARAMETERS parameters{};
    g_HookFunctions::g_oPresent1 = [](IDXGISwapChain1*, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
        ++OverlayTest::PresentCalls; OverlayTest::SeenSync = sync;
        OverlayTest::SeenFlags = flags; OverlayTest::SeenParameters = p;
        return DXGI_STATUS_OCCLUDED;
    };
    assert(hkPresent1(fixture.Chain.Get(), 3, DXGI_PRESENT_TEST, &parameters) == DXGI_STATUS_OCCLUDED);
    assert(!TargetChain && OverlayTest::PresentationDraws == 0);
    assert(OverlayTest::SeenParameters == &parameters && OverlayTest::SeenSync == 3 && OverlayTest::SeenFlags == DXGI_PRESENT_TEST);
    OverlayTest::BypassPresentation = true;
    hkPresent1(fixture.Chain.Get(), 0, 0, nullptr);
    assert(!TargetChain && OverlayTest::PresentationDraws == 0);
    OverlayTest::BypassPresentation = false;
    assert(hkPresent1(fixture.Chain.Get(), 0, 0, &parameters) == DXGI_STATUS_OCCLUDED);
    assert(TargetChain && OverlayTest::PresentationDraws == 1);
    g_HookFunctions::g_oPresent = [](IDXGISwapChain3*, UINT, UINT) { return S_FALSE; };
    g_HookFunctions::g_oPresent1 = [](IDXGISwapChain1* chain, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS*) {
        ComPtr<IDXGISwapChain3> chain3; assert(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain3))));
        return hkPresent(chain3.Get(), sync, flags);
    };
    assert(hkPresent1(fixture.Chain.Get(), 0, 0, &parameters) == S_FALSE);
    assert(OverlayTest::PresentationDraws == 2); // Present1 -> Present draws once.
    assert(hkPresent(fixture.Chain.Get(), 0, DXGI_PRESENT_TEST) == S_FALSE);
    assert(OverlayTest::PresentationDraws == 2);
    RenderOverlayFrame(fixture.Chain.Get()); // Bind the game window before native queue proof.
    assert(g_bufferCount == 2 && !g_InitState::g_Initialized && OverlayTest::Submissions == 0);
    assert(OverlayTest::InputInstalls == 1); // F2 must not depend on GPU backend readiness.
    assert(!ImGui::GetCurrentContext());
    RenderOverlayFrame(fixture.Secondary.Get());
    assert(SameObject(TargetChain.Get(), fixture.Chain.Get()));
    LastTargetPresent = GetTickCount64() - 3000;
    RenderOverlayFrame(fixture.Secondary.Get());
    assert(SameObject(TargetChain.Get(), fixture.Chain.Get())); // Other window cannot steal primary.
    assert(OverlayTest::InputInstalls == 1);
    fixture.DrawUntilSubmitted();
    assert(OverlayTest::InputInstalls > 1); // First submitted frame retries the idempotent route.
    assert(g_pd3dCommandQueue == fixture.Queue.Get());
    assert(g_LoadedFontScaleIdx == g_Config::MenuScaleIdx);
    const UINT initialSubmissions = OverlayTest::Submissions;
    RenderOverlayFrame(fixture.Chain.Get());
    assert(OverlayTest::Submissions == initialSubmissions); // Proof consumed each Present.

    // Keep the current buffer's draw in flight behind a CPU-controlled gate.
    fixture.NativeFrame();
    ComPtr<ID3D12Fence> gate;
    assert(SUCCEEDED(fixture.Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))));
    assert(SUCCEEDED(fixture.Queue->Wait(gate.Get(), 1)));
    RenderOverlayFrame(fixture.Chain.Get());
    const UINT pendingSubmissions = OverlayTest::Submissions;
    const UINT pendingUiFrames = OverlayTest::UiFrames;
    assert(pendingSubmissions == initialSubmissions + 1);
    const UINT index = fixture.Chain->GetCurrentBackBufferIndex();
    assert(g_fence->GetCompletedValue() < g_FrameContexts[index].FenceValue);
    const int priorScale = g_LoadedFontScaleIdx;
    g_Config::MenuScaleIdx = (priorScale + 1) % 5;
    fixture.NativeFrame(false); // Fresh proof, but native and overlay work still pending.
    RenderOverlayFrame(fixture.Chain.Get());
    assert(OverlayTest::Submissions == pendingSubmissions && OverlayTest::UiFrames == pendingUiFrames);
    assert(g_LoadedFontScaleIdx == priorScale); // No allocator reuse/font rebuild while pending.
    assert(!WaitForOverlayGpu(0));
    assert(SUCCEEDED(gate->Signal(1)));
    fixture.WaitNative(); assert(WaitForOverlayGpu(10000));
    fixture.DrawUntilSubmitted();
    assert(g_LoadedFontScaleIdx == g_Config::MenuScaleIdx);

    // First scene-copy recording is discarded. Its CPU-side state must stay
    // COPY_DEST so the next successful frame emits correct native barriers.
    assert(PhoenixVibrancy::Frames.empty());
    g_MenuState::g_isOpen = true;
    OverlayTest::DropNextDraw = true;
    fixture.NativeFrame();
    const UINT beforeFailure = OverlayTest::Submissions;
    RenderOverlayFrame(fixture.Chain.Get());
    assert(OverlayTest::DrawWasDropped && OverlayTest::Submissions == beforeFailure);
    assert(!PhoenixVibrancy::Frames.empty() && !PhoenixVibrancy::Frames[index].Readable);
    OverlayTest::DrawWasDropped = false;
    // A transient draw failure may leave the backend itself ready. That
    // independent failure result must discard this recording as well.
    OverlayTest::DropMakesBackendNotReady = false;
    OverlayTest::DropNextDraw = true;
    fixture.NativeFrame(); RenderOverlayFrame(fixture.Chain.Get());
    assert(ImGui_ImplDX12_IsReady() && OverlayTest::DrawWasDropped);
    assert(OverlayTest::Submissions == beforeFailure && !PhoenixVibrancy::Frames[index].Readable);
    OverlayTest::DrawWasDropped = false;
    fixture.DrawUntilSubmitted();
    assert(PhoenixVibrancy::Frames[index].Readable);
    fixture.CheckDebugErrors();

    // Actual DXGI Resize must release the production helper's buffer refs,
    // clear queue proofs and return to waiting for a new native submission.
    fixture.WaitNative();
    fixture.NativeList.Reset(); fixture.NativeAllocator.Reset();
    assert(SUCCEEDED(ResizeOverlayBuffers(fixture.Chain.Get(), 2, 400, 240, DXGI_FORMAT_R8G8B8A8_UNORM, 0)));
    assert(!TargetChain && !g_pd3dDevice && g_FrameContexts.empty());
    const UINT beforeResizeDraw = OverlayTest::Submissions;
    RenderOverlayFrame(fixture.Chain.Get());
    assert(g_bufferCount == 2 && !g_InitState::g_Initialized && OverlayTest::Submissions == beforeResizeDraw);
    assert(SUCCEEDED(fixture.Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&fixture.NativeAllocator))));
    assert(SUCCEEDED(fixture.Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, fixture.NativeAllocator.Get(), nullptr, IID_PPV_ARGS(&fixture.NativeList))));
    assert(SUCCEEDED(fixture.NativeList->Close()));
    fixture.DrawUntilSubmitted();
    assert(g_FrameContexts[0].Resource->GetDesc().Width == 400);
    fixture.CheckDebugErrors();
    // Hook the actual DXGI slot, rather than calling only our wrapper directly.
    ComPtr<IDXGISwapChain1> extended;
    assert(SUCCEEDED(fixture.Chain.As(&extended)));
    void* present1Address = (*reinterpret_cast<void***>(extended.Get()))[22];
    assert(MH_Initialize() == MH_OK);
    assert(MH_CreateHook(present1Address, reinterpret_cast<void*>(hkPresent1), reinterpret_cast<void**>(&g_HookFunctions::g_oPresent1)) == MH_OK);
    assert(MH_EnableHook(present1Address) == MH_OK);
    const UINT beforeNativePresent1 = OverlayTest::PresentationDraws;
    assert(SUCCEEDED(extended->Present1(0, 0, &parameters)));
    assert(OverlayTest::PresentationDraws == beforeNativePresent1 + 1);
    assert(MH_DisableHook(present1Address) == MH_OK);
    assert(MH_RemoveHook(present1Address) == MH_OK);
    assert(MH_Uninitialize() == MH_OK);
    g_HookFunctions::g_oPresent1 = nullptr;
    fixture.CheckDebugErrors();
    std::cout << "Present1 real DXGI detour, argument/result forwarding, TEST/bypass and nested Present dispatch passed.\n";
    std::cout << "Production overlay WARP passed: fixed chain, fresh verified queue, pending draw/font protection, font scale rebuild, discarded draw rollback and Resize rebinding.\n";
    return 0;
}
