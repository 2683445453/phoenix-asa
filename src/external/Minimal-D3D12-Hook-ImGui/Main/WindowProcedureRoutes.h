#pragma once
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace PhoenixInput {
    // Keep each published HWND's native target until callback drain and DLL
    // teardown. A callback can enter after Remove() without having copied its
    // target yet; switching to another window must not change that target.
    class WindowProcedureRoutes {
    public:
        struct Route { WNDPROC Original = nullptr; bool Active = false; };
        explicit WindowProcedureRoutes(WNDPROC Hook) : Hook(Hook) {}
        WindowProcedureRoutes(const WindowProcedureRoutes&) = delete;
        WindowProcedureRoutes& operator=(const WindowProcedureRoutes&) = delete;

        Route Resolve(HWND Window) const {
            std::scoped_lock Lock(RoutesMutex);
            const auto It = Originals.find(Window);
            return It == Originals.end() ? Route{} : Route{It->second, Window == InstalledWindow.load() && !OwnershipLost.load()};
        }
        bool IsInstalled(HWND Window) const {
            return Window && Window == InstalledWindow.load() && !OwnershipLost.load();
        }
        bool Install(HWND Window) {
            std::scoped_lock Lock(MutationMutex);
            if (!Window || !Hook || OwnershipLost.load() || !IsWindow(Window)) return false;
            if (InstalledWindow.load() == Window)
                return reinterpret_cast<WNDPROC>(GetWindowLongPtr(Window, GWLP_WNDPROC)) == Hook;
            const auto PreviousWindow = InstalledWindow.load();
            if (PreviousWindow && !RemoveLocked(PreviousWindow)) return false;
            const auto Current = reinterpret_cast<WNDPROC>(GetWindowLongPtr(Window, GWLP_WNDPROC));
            if (!Current || Current == Hook) return false;
            // A reused HWND or a different subclass owner must not overwrite
            // the immutable target of an older, possibly delayed callback.
            {
                std::scoped_lock RoutesLock(RoutesMutex);
                const auto Existing = Originals.find(Window);
                if (Existing != Originals.end() && Existing->second != Current) return false;
                try { Originals.emplace(Window, Current); }
                catch (...) { return false; }
            }

            // Publication precedes installing the hook. Resolve copies the
            // target under the route lock, then calls native code after unlock.
            // Do not hold that lock across User32: reentrant callbacks can
            // resolve and forward even while installation/removal is active.
            SetLastError(0);
            const auto Previous = reinterpret_cast<WNDPROC>(SetWindowLongPtr(Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Hook)));
            if (!Previous && GetLastError() != 0) return false;
            InstalledWindow = Window;
            if (Previous != Current) {
                // Another component changed the subclass between read/write.
                // Restore its target if we still own the slot, and fail closed:
                // unknown external callback chains cannot justify DLL release.
                if (reinterpret_cast<WNDPROC>(GetWindowLongPtr(Window, GWLP_WNDPROC)) == Hook)
                    SetWindowLongPtr(Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Previous));
                OwnershipLost = true;
                return false;
            }
            return true;
        }
        bool Remove(HWND Window) {
            std::scoped_lock Lock(MutationMutex);
            return RemoveLocked(Window);
        }
        bool RemoveAll() {
            std::scoped_lock Lock(MutationMutex);
            return RemoveLocked(InstalledWindow.load());
        }

    private:
        bool RemoveLocked(HWND Window) {
            if (OwnershipLost.load()) return false;
            if (!InstalledWindow.load()) return true;
            if (!Window || Window != InstalledWindow.load()) return false;
            const auto Original = Resolve(Window).Original;
            if (!Original) return false;
            if (!IsWindow(Window)) { InstalledWindow = nullptr; return true; }
            const auto Current = reinterpret_cast<WNDPROC>(GetWindowLongPtr(Window, GWLP_WNDPROC));
            if (Current != Hook && Current != Original) return false;
            if (Current == Hook) {
                SetLastError(0);
                const auto Previous = reinterpret_cast<WNDPROC>(SetWindowLongPtr(Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Original)));
                if ((!Previous && GetLastError() != 0) || Previous != Hook ||
                    reinterpret_cast<WNDPROC>(GetWindowLongPtr(Window, GWLP_WNDPROC)) != Original) {
                    OwnershipLost = true;
                    return false;
                }
            }
            InstalledWindow = nullptr;
            return true;
        }
        mutable std::mutex RoutesMutex;
        std::recursive_mutex MutationMutex;
        std::unordered_map<HWND, WNDPROC> Originals;
        std::atomic<HWND> InstalledWindow{nullptr};
        WNDPROC Hook = nullptr;
        std::atomic<bool> OwnershipLost{false};
    };
}
