#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <cstring>
#include <mutex>

namespace ezVMT {
    struct HookEntry {
        void* detour = nullptr;
        void*** classInstance = nullptr;
        void** originalVmt = nullptr;
        void** customVmt = nullptr;
        void* originalFunc = nullptr;
        size_t index = 0, totalCount = 0;
        bool isEnabled = false, published = false;
    };
    inline std::vector<HookEntry> g_Hooks;
    inline std::recursive_mutex g_Mutex;
    inline size_t g_RetiredBytes = 0;
    inline bool Accessible(const void* Address, size_t Size, bool Write = false) {
        auto Cursor = reinterpret_cast<uintptr_t>(Address);
        if (!Cursor || !Size || Cursor > UINTPTR_MAX - Size) return false;
        const auto End = Cursor + Size;
        while (Cursor < End) {
            MEMORY_BASIC_INFORMATION Region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(Cursor), &Region, sizeof(Region)) ||
                Region.State != MEM_COMMIT || (Region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
            const DWORD P = Region.Protect & 0xff;
            const bool Writable = P == PAGE_READWRITE || P == PAGE_WRITECOPY || P == PAGE_EXECUTE_READWRITE || P == PAGE_EXECUTE_WRITECOPY;
            if (Write ? !Writable : !(Writable || P == PAGE_READONLY || P == PAGE_EXECUTE_READ)) return false;
            const auto Next = reinterpret_cast<uintptr_t>(Region.BaseAddress) + Region.RegionSize;
            if (Next <= Cursor) return false;
            Cursor = Next < End ? Next : End;
        }
        return true;
    }
    inline bool IsWritableAddress(const void* Address) { return Accessible(Address, sizeof(void*), true); }
    inline bool IsExecutable(const void* Address) {
        MEMORY_BASIC_INFORMATION Region{};
        if (!Address || !VirtualQuery(Address, &Region, sizeof(Region)) || Region.State != MEM_COMMIT ||
            (Region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD P = Region.Protect & 0xff;
        return P == PAGE_EXECUTE || P == PAGE_EXECUTE_READ || P == PAGE_EXECUTE_READWRITE || P == PAGE_EXECUTE_WRITECOPY;
    }
    inline size_t GetVMTSizeDynamic(void** Vmt) {
        size_t Count = 0;
        while (Count < 4096 && Accessible(Vmt + Count, sizeof(void*)) && IsExecutable(Vmt[Count])) ++Count;
        return Count;
    }
    inline HookEntry* FindHookEntry(void* Detour) {
        for (auto& Entry : g_Hooks) if (Entry.detour == Detour) return &Entry;
        return nullptr;
    }
    inline bool CreateHook(void* Detour, void* Instance, size_t Index, void** Original = nullptr) {
        std::scoped_lock Lock(g_Mutex);
        if (!IsExecutable(Detour) || !IsWritableAddress(Instance) || FindHookEntry(Detour)) return false;
        auto*** Object = reinterpret_cast<void***>(Instance);
        for (const auto& Existing : g_Hooks) if (Existing.classInstance == Object) return false;
        auto** Native = *Object;
        const size_t Count = GetVMTSizeDynamic(Native);
        if (Index >= Count || !Accessible(Native - 2, (Count + 2) * sizeof(void*))) return false;
        // Ark .data/.bss zero bytes are game-owned state, never spare storage.
        auto** Storage = static_cast<void**>(VirtualAlloc(nullptr, (Count + 2) * sizeof(void*), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!Storage) return false;
        std::memcpy(Storage, Native - 2, (Count + 2) * sizeof(void*));
        auto** Shadow = Storage + 2;
        Shadow[Index] = Detour;
        HookEntry Entry{Detour, Object, Native, Shadow, Native[Index], Index, Count, false, false};
        try { g_Hooks.push_back(Entry); }
        catch (...) { VirtualFree(Storage, 0, MEM_RELEASE); return false; }
        if (Original) *Original = Native[Index];
        return true;
    }
    inline bool EnableHook(void* Detour) {
        std::scoped_lock Lock(g_Mutex);
        auto* Entry = FindHookEntry(Detour);
        if (!Entry || Entry->isEnabled || !IsWritableAddress(Entry->classInstance)) return false;
        Entry->customVmt[Entry->index] = Detour;
        void* Previous = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(Entry->classInstance), Entry->customVmt, Entry->originalVmt);
        if (Previous != Entry->originalVmt) { Entry->customVmt[Entry->index] = Entry->originalFunc; return false; }
        Entry->isEnabled = Entry->published = true;
        return true;
    }
    inline bool DisableEntry(HookEntry& Entry) {
        // A delayed caller may already hold the shadow table in a register.
        if (!Accessible(Entry.customVmt, Entry.totalCount * sizeof(void*), true)) return false;
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&Entry.customVmt[Entry.index]), Entry.originalFunc);
        if (!Entry.isEnabled) return true;
        if (!IsWritableAddress(Entry.classInstance)) {
            if (Accessible(Entry.classInstance, sizeof(void*))) return false;
            Entry.isEnabled = false; return true; // object memory has been unmapped
        }
        void* Previous = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(Entry.classInstance), Entry.originalVmt, Entry.customVmt);
        if (Previous != Entry.customVmt && Previous != Entry.originalVmt) return false;
        Entry.isEnabled = false;
        return true;
    }
    inline bool DisableHook(void* Detour) {
        std::scoped_lock Lock(g_Mutex);
        auto* Entry = FindHookEntry(Detour);
        return Entry && DisableEntry(*Entry);
    }
    inline void Retire(HookEntry& Entry) {
        if (!Entry.published) VirtualFree(Entry.customVmt - 2, 0, MEM_RELEASE);
        else {
            // Published tables remain native-only until process exit. Freeing
            // them can fault a preempted caller that already loaded the vptr.
            g_RetiredBytes += (Entry.totalCount + 2) * sizeof(void*);
        }
    }
    inline bool RemoveHook(void* Detour) {
        std::scoped_lock Lock(g_Mutex);
        for (auto It = g_Hooks.begin(); It != g_Hooks.end(); ++It) if (It->detour == Detour) {
            if (!DisableEntry(*It)) return false;
            Retire(*It); g_Hooks.erase(It); return true;
        }
        return true;
    }
    inline bool RemoveAllHooks() {
        std::scoped_lock Lock(g_Mutex);
        bool Complete = true;
        for (auto It = g_Hooks.begin(); It != g_Hooks.end();) {
            if (!DisableEntry(*It)) { Complete = false; ++It; continue; }
            Retire(*It); It = g_Hooks.erase(It);
        }
        return Complete;
    }
    inline void* GetOriginal(void* Detour) {
        std::scoped_lock Lock(g_Mutex);
        auto* Entry = FindHookEntry(Detour);
        return Entry ? Entry->originalFunc : nullptr;
    }
}
