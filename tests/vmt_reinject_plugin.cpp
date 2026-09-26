#define NOMINMAX
#include "../src/external/ezVMT/include/ezVMT.hpp"
#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/UnloadBarrier.h"
#include <atomic>

struct HostObject { void** Vmt; std::atomic<bool>* Hold; };
using Function = int(__fastcall*)(HostObject*, int);
Function Original = nullptr;
std::atomic<uint32_t> Active{0};
HMODULE Module = nullptr;
int __fastcall Detour(HostObject* Object, int Value) {
    Active.fetch_add(1);
    while (Object->Hold && Object->Hold->load()) Sleep(1);
    const auto Result = Original(Object, Value) + 100;
    Active.fetch_sub(1);
    return Result;
}
extern "C" __declspec(dllexport) bool Install(HostObject* Object) {
    return ezVMT::CreateHook(reinterpret_cast<void*>(&Detour), Object, 0, reinterpret_cast<void**>(&Original)) &&
        ezVMT::EnableHook(reinterpret_cast<void*>(&Detour));
}
extern "C" __declspec(dllexport) uint32_t Calls() { return Active.load(); }
extern "C" __declspec(dllexport) bool Stop() {
    return ezVMT::RemoveAllHooks() && PhoenixUnload::WaitForQuiet(Active, 80, 5) &&
        PhoenixUnload::ThreadImagesIdle(Module);
}
BOOL WINAPI DllMain(HMODULE Instance, DWORD Reason, void*) {
    if (Reason == DLL_PROCESS_ATTACH) Module = Instance;
    return TRUE;
}
