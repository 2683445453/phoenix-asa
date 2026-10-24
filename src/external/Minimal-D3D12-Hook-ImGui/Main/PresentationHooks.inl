// Included within g_MDX12; shared by the production hook and WARP regression.
namespace {
    void LogPresentationTarget(const char* Name, void* Address) {
        HMODULE Owner = nullptr;
        char Path[MAX_PATH]{};
        if (Address && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(Address), &Owner)) GetModuleFileNameA(Owner, Path, MAX_PATH);
        char Message[MAX_PATH + 128]{};
        snprintf(Message, sizeof(Message), "Overlay hook target: %s address=%p module=%s", Name, Address, Path);
        g_Hook::LogLifecycle(Message);
    }
    thread_local unsigned PresentationDepth = 0;
    struct PresentationScope {
        bool Outermost = PresentationDepth++ == 0;
        ~PresentationScope() { --PresentationDepth; }
    };
    void DrawPresentedChain(IDXGISwapChain* Chain, UINT Flags, bool Extended) {
        static std::atomic<bool> PresentSeen{false}, Present1Seen{false}, InvalidInterfaceSeen{false};
        auto& Seen = Extended ? Present1Seen : PresentSeen;
        if (!Seen.exchange(true)) {
            char Message[160]{};
            snprintf(Message, sizeof(Message), "Overlay presentation callback: %s chain=%p flags=0x%X", Extended ? "Present1" : "Present", Chain, Flags);
            g_Hook::LogLifecycle(Message);
        }
        if (ShouldBypassFeatures() || (Flags & DXGI_PRESENT_TEST) || !Chain) return;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> Chain3;
        if (SUCCEEDED(Chain->QueryInterface(IID_PPV_ARGS(&Chain3)))) RenderOverlayFrame(Chain3.Get());
        else if (!InvalidInterfaceSeen.exchange(true)) g_Hook::LogLifecycle("Overlay presentation rejected: IDXGISwapChain3 unavailable.");
    }
}
HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain3* Chain, UINT SyncInterval, UINT Flags) {
    g_Hook::CallbackScope Guard;
    PresentationScope Scope;
    if (Scope.Outermost) DrawPresentedChain(Chain, Flags, false);
    return g_HookFunctions::g_oPresent ? g_HookFunctions::g_oPresent(Chain, SyncInterval, Flags) : S_OK;
}
HRESULT STDMETHODCALLTYPE hkPresent1(IDXGISwapChain1* Chain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* Parameters) {
    g_Hook::CallbackScope Guard;
    PresentationScope Scope;
    if (Scope.Outermost) DrawPresentedChain(Chain, Flags, true);
    return g_HookFunctions::g_oPresent1 ? g_HookFunctions::g_oPresent1(Chain, SyncInterval, Flags, Parameters) : S_OK;
}
