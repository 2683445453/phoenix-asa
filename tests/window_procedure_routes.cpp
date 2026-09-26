#define NOMINMAX
#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/WindowProcedureRoutes.h"
#include <cassert>
#include <iostream>
#include <thread>

namespace {
    constexpr UINT Probe = WM_APP + 31, DelayedProbe = WM_APP + 32;
    PhoenixInput::WindowProcedureRoutes* Routes = nullptr;
    HANDLE Entered = nullptr, Continue = nullptr;
    LRESULT CALLBACK NativeA(HWND Window, UINT Message, WPARAM W, LPARAM L) {
        if (Message == Probe || Message == DelayedProbe) return 100 + LRESULT(W);
        return DefWindowProc(Window, Message, W, L);
    }
    LRESULT CALLBACK NativeB(HWND Window, UINT Message, WPARAM W, LPARAM L) {
        if (Message == Probe || Message == DelayedProbe) return 200 + LRESULT(W);
        return DefWindowProc(Window, Message, W, L);
    }
    LRESULT CALLBACK Foreign(HWND, UINT, WPARAM, LPARAM) { return 999; }
    LRESULT CALLBACK Hook(HWND Window, UINT Message, WPARAM W, LPARAM L) {
        if (Message == DelayedProbe) {
            SetEvent(Entered);
            assert(WaitForSingleObject(Continue, 5000) == WAIT_OBJECT_0);
        }
        // Matches input.cpp's entry-time HWND lookup and local target copy.
        const auto Route = Routes->Resolve(Window);
        return Route.Original ? CallWindowProc(Route.Original, Window, Message, W, L) : DefWindowProc(Window, Message, W, L);
    }
    HWND HiddenWindow(const wchar_t* Class, WNDPROC Native) {
        WNDCLASSW Info{}; Info.lpfnWndProc = Native; Info.hInstance = GetModuleHandle(nullptr); Info.lpszClassName = Class;
        assert(RegisterClassW(&Info));
        auto Window = CreateWindowExW(0, Class, L"phoenix input routing regression", 0, 0, 0, 20, 20, HWND_MESSAGE, nullptr, Info.hInstance, nullptr);
        assert(Window); return Window;
    }
}

int main() {
    PhoenixInput::WindowProcedureRoutes Registry(Hook); Routes = &Registry;
    assert(Registry.RemoveAll()); // no installation is already fully detached
    auto A = HiddenWindow(L"PhoenixRouteTestA", NativeA);
    auto B = HiddenWindow(L"PhoenixRouteTestB", NativeB);
    // ANSI GetWindowLongPtr may return a User32 thunk for a Unicode window.
    // That opaque native target, not its implementation address, must persist.
    const auto OriginalA = reinterpret_cast<WNDPROC>(GetWindowLongPtr(A, GWLP_WNDPROC));
    const auto OriginalB = reinterpret_cast<WNDPROC>(GetWindowLongPtr(B, GWLP_WNDPROC));
    assert(!Registry.Install(nullptr));
    assert(!Registry.Resolve(A).Original);
    assert(Registry.Install(A));
    assert(Registry.Resolve(A).Original == OriginalA && Registry.Resolve(A).Active);
    assert(SendMessage(A, Probe, 7, 0) == 107);
    assert(!Registry.Remove(B));

    Entered = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    Continue = CreateEvent(nullptr, TRUE, FALSE, nullptr); assert(Entered && Continue);
    const auto CachedHook = reinterpret_cast<WNDPROC>(GetWindowLongPtr(A, GWLP_WNDPROC));
    LRESULT DelayedResult = 0;
    std::thread Delayed([&] { DelayedResult = CachedHook(A, DelayedProbe, 8, 0); });
    assert(WaitForSingleObject(Entered, 5000) == WAIT_OBJECT_0);
    // The old callback has entered, but has NOT yet copied its native target.
    assert(Registry.Install(B));
    assert(!Registry.Resolve(A).Active && Registry.Resolve(A).Original == OriginalA);
    assert(Registry.Resolve(B).Active && Registry.Resolve(B).Original == OriginalB);
    assert(SendMessage(A, Probe, 7, 0) == 107 && SendMessage(B, Probe, 7, 0) == 207);
    SetEvent(Continue); Delayed.join(); assert(DelayedResult == 108);
    CloseHandle(Entered); CloseHandle(Continue);

    const auto CopiedOldTarget = Registry.Resolve(A).Original;
    assert(Registry.Remove(B));
    assert(CallWindowProc(CopiedOldTarget, A, Probe, 9, 0) == 109);
    assert(CachedHook(A, Probe, 9, 0) == 109); // lookup AFTER removal also stays on A

    std::atomic<bool> Stop{false}; std::atomic<unsigned> Errors{0}, Readers{0}, Reads{0};
    auto ReadRoutes = [&] {
        ++Readers;
        while (!Stop.load()) {
            if (Hook(A, Probe, 1, 0) != 101 || Hook(B, Probe, 1, 0) != 201) ++Errors;
            ++Reads;
        }
    };
    std::thread First(ReadRoutes), Second(ReadRoutes);
    while (Readers.load() != 2) std::this_thread::yield();
    for (int I = 0; I < 1000; ++I) assert(Registry.Install((I & 1) ? A : B));
    Stop.store(true); First.join(); Second.join(); assert(Errors.load() == 0 && Reads.load() > 0);
    assert(Registry.Remove(A));

    // Reused HWND/native-owner ambiguity must not overwrite an old route.
    SetWindowLongPtr(A, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Foreign));
    assert(!Registry.Install(A));
    assert(Registry.Resolve(A).Original == OriginalA && !Registry.Resolve(A).Active);
    assert(reinterpret_cast<WNDPROC>(GetWindowLongPtr(A, GWLP_WNDPROC)) == Foreign);
    SetWindowLongPtr(A, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OriginalA));

    assert(Registry.Install(B));
    SetWindowLongPtr(B, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Foreign));
    assert(!Registry.Remove(B)); // preserve a foreign subclass and block unload
    assert(reinterpret_cast<WNDPROC>(GetWindowLongPtr(B, GWLP_WNDPROC)) == Foreign);
    assert(Registry.Resolve(B).Original == OriginalB);
    SetWindowLongPtr(B, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Hook));
    assert(!Registry.Remove(A));
    assert(Registry.RemoveAll()); // global/new HWND state cannot hide the actual owner
    assert(Registry.Install(B));
    assert(DestroyWindow(B)); assert(Registry.Remove(B));
    assert(!Registry.Resolve(B).Active && Registry.Resolve(B).Original == OriginalB);
    assert(DestroyWindow(A));
    UnregisterClassW(L"PhoenixRouteTestA", GetModuleHandle(nullptr));
    UnregisterClassW(L"PhoenixRouteTestB", GetModuleHandle(nullptr));
    std::cout << "Window routes: two real hidden windows, delayed callbacks before/after native-target capture, 1000 concurrent switches, retained targets, foreign ownership and destroyed-window removal passed.\n";
}
