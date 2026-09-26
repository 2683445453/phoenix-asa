#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <atomic>
#include <cstdint>

namespace PhoenixUnload {
    inline bool WaitForQuiet(const std::atomic<uint32_t>& Active, DWORD TimeoutMs, DWORD QuietMs = 30) {
        const auto Start = GetTickCount64();
        auto QuietSince = Start;
        while (GetTickCount64() - Start < TimeoutMs) {
            const auto Now = GetTickCount64();
            if (Active.load(std::memory_order_acquire) != 0) QuietSince = Now;
            else if (Now - QuietSince >= QuietMs) return true;
            Sleep(1);
        }
        return false;
    }
    inline bool ThreadImagesIdle(HMODULE Module) {
        const auto Begin = reinterpret_cast<uintptr_t>(Module);
        if (!Begin) return false;
        const auto* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Begin);
        if (Dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(Begin + Dos->e_lfanew);
        if (Nt->Signature != IMAGE_NT_SIGNATURE) return false;
        const auto End = Begin + Nt->OptionalHeader.SizeOfImage;
        const auto InImage = [Begin, End](DWORD64 Value) { return Value >= Begin && Value < End; };
        HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (Snapshot == INVALID_HANDLE_VALUE) return false;
        bool Idle = true;
        THREADENTRY32 Entry{}; Entry.dwSize = sizeof(Entry);
        if (!Thread32First(Snapshot, &Entry)) { CloseHandle(Snapshot); return false; }
        do {
            if (Entry.th32OwnerProcessID != GetCurrentProcessId() || Entry.th32ThreadID == GetCurrentThreadId()) continue;
            HANDLE Thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | SYNCHRONIZE, FALSE, Entry.th32ThreadID);
            if (!Thread) { if (GetLastError() != ERROR_INVALID_PARAMETER) Idle = false; continue; }
            if (WaitForSingleObject(Thread, 0) == WAIT_OBJECT_0) { CloseHandle(Thread); continue; }
            if (SuspendThread(Thread) == DWORD(-1)) { CloseHandle(Thread); Idle = false; continue; }
            CONTEXT Context{}; Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            const bool Read = GetThreadContext(Thread, &Context) != FALSE;
            // No allocation, logging or mutex acquisition while a thread is
            // suspended: it may own a heap/loader lock needed by this worker.
            const bool Inside = !Read || InImage(Context.Rip) || InImage(Context.Rax) || InImage(Context.Rbx) || InImage(Context.Rbp) ||
                InImage(Context.Rcx) || InImage(Context.Rdx) || InImage(Context.Rsi) || InImage(Context.Rdi) ||
                InImage(Context.R8) || InImage(Context.R9) || InImage(Context.R10) || InImage(Context.R11) ||
                InImage(Context.R12) || InImage(Context.R13) || InImage(Context.R14) || InImage(Context.R15);
            ResumeThread(Thread); CloseHandle(Thread);
            if (Inside) { Idle = false; break; }
            Entry.dwSize = sizeof(Entry);
        } while (Thread32Next(Snapshot, &Entry));
        CloseHandle(Snapshot);
        return Idle;
    }
    inline bool WaitForImageExit(HMODULE Module, const std::atomic<uint32_t>& Active, DWORD TimeoutMs) {
        const auto Start = GetTickCount64();
        while (GetTickCount64() - Start < TimeoutMs) {
            if (Active.load(std::memory_order_acquire) == 0 && ThreadImagesIdle(Module)) {
                Sleep(10);
                if (Active.load(std::memory_order_acquire) == 0 && ThreadImagesIdle(Module)) return true;
            }
            Sleep(10);
        }
        return false;
    }
}
