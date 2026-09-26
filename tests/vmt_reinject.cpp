#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/UnloadBarrier.h"
struct HostObject { void** Vmt; std::atomic<bool>* Hold; };
using Function = int(__fastcall*)(HostObject*, int);
int __fastcall Native(HostObject*, int Value) { return Value + 1; }
int __fastcall Foreign(HostObject*, int Value) { return Value + 2; }
int main() {
    void* NativeStorage[]{nullptr,nullptr,reinterpret_cast<void*>(&Native),reinterpret_cast<void*>(&Native),nullptr};
    void* OriginalStorage[5]{}; std::memcpy(OriginalStorage,NativeStorage,sizeof(NativeStorage));
    void* ForeignStorage[]{reinterpret_cast<void*>(&Foreign),nullptr};
    std::atomic<bool> Hold{false};
    HostObject Object{NativeStorage+2,&Hold};
    for (int Iteration=0; Iteration<20; ++Iteration) {
        HMODULE Module=LoadLibraryW(L"release/reinject-harvest/tests/vmt_reinject_plugin.dll"); assert(Module);
        auto Install=reinterpret_cast<bool(*)(HostObject*)>(GetProcAddress(Module,"Install"));
        auto Stop=reinterpret_cast<bool(*)()>(GetProcAddress(Module,"Stop"));
        auto Calls=reinterpret_cast<uint32_t(*)()>(GetProcAddress(Module,"Calls"));
        assert(Install && Stop && Calls && Install(&Object));
        auto** Published=Object.Vmt;
        MEMORY_BASIC_INFORMATION Memory{}; assert(VirtualQuery(Published,&Memory,sizeof(Memory)));
        assert(Memory.Type==MEM_PRIVATE && Memory.AllocationBase!=GetModuleHandleW(nullptr));
        assert(reinterpret_cast<Function>(Object.Vmt[0])(&Object,4)==105);
        if (Iteration==0) {
            Object.Vmt=ForeignStorage;
            assert(!Stop()); // preserve another owner's subclass, retain DLL
            assert(Object.Vmt==ForeignStorage && Published[0]==reinterpret_cast<void*>(&Native));
            Object.Vmt=Published;
        }
        if (Iteration==1) {
            Hold.store(true);
            int Result=0;
            std::thread Caller([&] { Result=reinterpret_cast<Function>(Published[0])(&Object,7); });
            while (!Calls()) Sleep(1);
            assert(!Stop()); // detaches but cannot release a live callback image
            Hold.store(false); Caller.join(); assert(Result==108);
        }
        assert(Stop()); assert(Object.Vmt==NativeStorage+2);
        assert(FreeLibrary(Module));
        assert(GetModuleHandleW(L"vmt_reinject_plugin.dll")==nullptr);
        // A caller preempted after loading the old vptr still has safe native
        // storage after the DLL image has actually been released.
        assert(reinterpret_cast<Function>(Published[0])(&Object,9)==10);
        assert(reinterpret_cast<Function>(Object.Vmt[0])(&Object,9)==10);
        assert(std::memcmp(NativeStorage,OriginalStorage,sizeof(NativeStorage))==0);
    }
    std::atomic<uint32_t> Active{1};
    assert(!PhoenixUnload::WaitForQuiet(Active,5,1));
    Active.store(0); assert(PhoenixUnload::WaitForQuiet(Active,30,5));
    std::cout<<"20 real DLL load/hook/detach/unload/reload cycles passed; original vtables unchanged, delayed callers safe, foreign ownership and live callbacks block release.\n";
}
