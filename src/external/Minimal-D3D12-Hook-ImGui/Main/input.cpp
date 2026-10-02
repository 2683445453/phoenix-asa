#include "mdx12_api.h"
#include "hooks.h"
#include "WindowProcedureRoutes.h"
#include "../../../internal/Menu/FeatureShortcuts.h"
#include "../../../internal/Hack/Hack.h"
#include "../../../internal/Hack/DeinoFlightUI.h"
#include "../../../internal/Lua/LuaManager.h"

#include "../MinHook/include/MinHook.h"

#pragma warning(push)
#pragma warning(disable: 26451)
#pragma warning(disable: 26812)
#pragma warning(disable: 6387)
#pragma warning(pop)

namespace g_MDX12 {
    namespace cursorhook {
        static HCURSOR g_lastCursor = nullptr;
        static bool g_initialized = false;
        static bool g_menuCursorStateActive = false;
        static bool g_wasVisibleBeforeMenu = false;
        static HCURSOR g_cursorBeforeMenu = nullptr;
        static bool g_hasDeferredCursorRequest = false;
        static HCURSOR g_deferredCursor = nullptr;
        static bool g_hasClipRectBeforeMenu = false;
        static RECT g_clipRectBeforeMenu{};
        static bool g_hasDeferredClipRequest = false;
        static bool g_deferredClipIsNull = true;
        static RECT g_deferredClipRect{};
        static POINT g_lastReportedPos = { 0, 0 };

        BOOL WINAPI hkGetCursorPos(LPPOINT lpPoint) {
            g_Hook::CallbackScope Guard;

            if (!lpPoint) {
                if (g_HookFunctions::g_oGetCursorPos) return g_HookFunctions::g_oGetCursorPos(nullptr);
                return FALSE;
            }

            if (g_MenuState::g_isOpen && g_InputState::g_blockMouseInput) {
                RECT rect;
                GetWindowRect(g_ProcessWindow::g_mainWindow, &rect);
                lpPoint->x = rect.left + (rect.right - rect.left) / 2;
                lpPoint->y = rect.top + (rect.bottom - rect.top) / 2;
                g_lastReportedPos = *lpPoint;
                return TRUE;
            }

            BOOL result = g_HookFunctions::g_oGetCursorPos ? g_HookFunctions::g_oGetCursorPos(lpPoint) : FALSE;

            if (result) {
                g_lastReportedPos = *lpPoint;
            }

            return result;
        }

        BOOL WINAPI hkSetCursorPos(int X, int Y) {
            g_Hook::CallbackScope Guard;

            if (g_MenuState::g_isOpen && g_InputState::g_blockMouseInput) {
                return TRUE;
            }

            return g_HookFunctions::g_oSetCursorPos ? g_HookFunctions::g_oSetCursorPos(X, Y) : FALSE;
        }

        HCURSOR WINAPI hkSetCursor(HCURSOR hCursor) {
            g_Hook::CallbackScope Guard;

            if (g_MenuState::g_isOpen) {
                // ImGui draws the menu cursor. Do not let the game mutate the
                // process cursor while the menu is open. Remember the game's
                // most recent request so it can be applied when F2 closes the
                // menu. ImGui's Win32 cursor updates are disabled separately.
                g_hasDeferredCursorRequest = true;
                g_deferredCursor = hCursor;
                if (hCursor) g_lastCursor = hCursor;
                return g_lastCursor;
            }

            g_lastCursor = hCursor;
            return g_HookFunctions::g_oSetCursor ? g_HookFunctions::g_oSetCursor(hCursor) : nullptr;
        }

        int WINAPI hkShowCursor(BOOL bShow) {
            g_Hook::CallbackScope Guard;

            // Never synthesize or suppress ShowCursor calls. Its display count
            // is stateful, so an unmatched FALSE/TRUE permanently hides the
            // game's cursor after the overlay closes. The menu hides only the
            // current cursor handle and draws its own software cursor instead.
            return g_HookFunctions::g_oShowCursor ? g_HookFunctions::g_oShowCursor(bShow) : 0;
        }

        BOOL WINAPI hkClipCursor(const RECT* lpRect) {
            g_Hook::CallbackScope Guard;

            if (g_MenuState::g_isOpen) {
                g_hasDeferredClipRequest = true;
                g_deferredClipIsNull = lpRect == nullptr;
                if (lpRect) g_deferredClipRect = *lpRect;
                return TRUE;
            }

            return g_HookFunctions::g_oClipCursor ? g_HookFunctions::g_oClipCursor(lpRect) : FALSE;
        }

        BOOL WINAPI hkGetMouseMovePointsEx(UINT cbSize, LPMOUSEMOVEPOINT lppt, LPMOUSEMOVEPOINT lpptBuf, int nBufPoints, DWORD resolution) {
            g_Hook::CallbackScope Guard;

            if (g_MenuState::g_isOpen && g_InputState::g_blockMouseInput) {
                if (lppt) {
                    memset(lppt, 0, cbSize);
                }
                return 0;
            }

            return g_HookFunctions::g_oGetMouseMovePointsEx ? g_HookFunctions::g_oGetMouseMovePointsEx(cbSize, lppt, lpptBuf, nBufPoints, resolution) : 0;
        }

        void RestoreForUnload() {
            if (!g_menuCursorStateActive) return;

            if (g_HookFunctions::g_oClipCursor) {
                if (g_hasDeferredClipRequest) {
                    g_HookFunctions::g_oClipCursor(
                        g_deferredClipIsNull ? nullptr : &g_deferredClipRect);
                }
                else if (g_hasClipRectBeforeMenu) {
                    g_HookFunctions::g_oClipCursor(&g_clipRectBeforeMenu);
                }
                else {
                    g_HookFunctions::g_oClipCursor(nullptr);
                }
            }

            if (g_HookFunctions::g_oSetCursor) {
                HCURSOR Cursor = g_hasDeferredCursorRequest ?
                    g_deferredCursor : (g_wasVisibleBeforeMenu ? g_cursorBeforeMenu : nullptr);
                if (Cursor) g_lastCursor = Cursor;
                g_HookFunctions::g_oSetCursor(Cursor);
            }

            g_menuCursorStateActive = false;
            g_hasDeferredCursorRequest = false;
            g_deferredCursor = nullptr;
            g_hasDeferredClipRequest = false;
            g_deferredClipIsNull = true;

            // Ask the game window to refresh its own cursor immediately. The
            // message is asynchronous so closing F2 cannot re-enter this WndProc.
            if (g_ProcessWindow::g_mainWindow) {
                PostMessage(g_ProcessWindow::g_mainWindow, WM_SETCURSOR,
                    reinterpret_cast<WPARAM>(g_ProcessWindow::g_mainWindow),
                    MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            }
        }

        void UpdateCursorState() {
            const bool MenuOpen = g_MenuState::g_isOpen;
            if (MenuOpen == g_menuCursorStateActive) return;

            if (!MenuOpen) {
                RestoreForUnload();
                return;
            }

            CURSORINFO CursorInfo{};
            CursorInfo.cbSize = sizeof(CursorInfo);
            if (GetCursorInfo(&CursorInfo)) {
                g_wasVisibleBeforeMenu = (CursorInfo.flags & CURSOR_SHOWING) != 0;
                g_cursorBeforeMenu = CursorInfo.hCursor;
                if (CursorInfo.hCursor) g_lastCursor = CursorInfo.hCursor;
            }
            else {
                g_wasVisibleBeforeMenu = false;
                g_cursorBeforeMenu = nullptr;
            }
            g_hasDeferredCursorRequest = false;
            g_deferredCursor = nullptr;
            g_hasDeferredClipRequest = false;
            g_deferredClipIsNull = true;

            g_hasClipRectBeforeMenu = g_HookFunctions::g_oGetClipCursor &&
                g_HookFunctions::g_oGetClipCursor(&g_clipRectBeforeMenu);

            // Synchronize the real cursor before hiding it. This also covers
            // focus changes where no WM_MOUSEMOVE reached ImGui immediately
            // before the menu was opened.
            POINT CursorPosition{};
            const BOOL HasCursorPosition = g_HookFunctions::g_oGetCursorPos ?
                g_HookFunctions::g_oGetCursorPos(&CursorPosition) : GetCursorPos(&CursorPosition);
            if (HasCursorPosition && g_ProcessWindow::g_mainWindow && ImGui::GetCurrentContext()) {
                ScreenToClient(g_ProcessWindow::g_mainWindow, &CursorPosition);
                ImGui::GetIO().MousePos = ImVec2(
                    static_cast<float>(CursorPosition.x),
                    static_cast<float>(CursorPosition.y));
            }

            // Hide the native cursor without touching ShowCursor's display
            // counter. The original handle (and any game update made while the
            // menu is open) is restored when the menu closes.
            if (g_HookFunctions::g_oSetCursor) {
                g_HookFunctions::g_oSetCursor(nullptr);
            }
            if (g_HookFunctions::g_oClipCursor) {
                g_HookFunctions::g_oClipCursor(nullptr);
            }
            g_menuCursorStateActive = true;
        }

        void Init() {
            HMODULE user32 = GetModuleHandleA("user32.dll");
            if (!user32) return;

            FARPROC getCursorPosAddr = GetProcAddress(user32, "GetCursorPos");
            FARPROC setCursorPosAddr = GetProcAddress(user32, "SetCursorPos");
            FARPROC setCursorAddr = GetProcAddress(user32, "SetCursor");
            FARPROC showCursorAddr = GetProcAddress(user32, "ShowCursor");
            FARPROC clipCursorAddr = GetProcAddress(user32, "ClipCursor");
            FARPROC getMouseMovePointsExAddr = GetProcAddress(user32, "GetMouseMovePointsEx");
            FARPROC getClipCursorAddr = GetProcAddress(user32, "GetClipCursor");

            if (getCursorPosAddr) {
                MH_CreateHook(getCursorPosAddr, reinterpret_cast<LPVOID>(hkGetCursorPos), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oGetCursorPos));
            }

            if (setCursorPosAddr) {
                MH_CreateHook(setCursorPosAddr, reinterpret_cast<LPVOID>(hkSetCursorPos), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oSetCursorPos));
            }

            if (setCursorAddr) {
                MH_CreateHook(setCursorAddr, reinterpret_cast<LPVOID>(hkSetCursor), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oSetCursor));
            }

            if (showCursorAddr) {
                MH_CreateHook(showCursorAddr, reinterpret_cast<LPVOID>(hkShowCursor), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oShowCursor));
            }

            if (clipCursorAddr) {
                MH_CreateHook(clipCursorAddr, reinterpret_cast<LPVOID>(hkClipCursor), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oClipCursor));
            }

            if (getMouseMovePointsExAddr) {
                MH_CreateHook(getMouseMovePointsExAddr, reinterpret_cast<LPVOID>(hkGetMouseMovePointsEx), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oGetMouseMovePointsEx));
            }

            if (getClipCursorAddr) {
                g_HookFunctions::g_oGetClipCursor = reinterpret_cast<PFN_GetClipCursor>(getClipCursorAddr);
            }

            if (g_HookFunctions::g_oGetCursorPos) {
                g_HookFunctions::g_oGetCursorPos(&g_lastReportedPos);
            }
            else {
                GetCursorPos(&g_lastReportedPos);
            }

            CURSORINFO CursorInfo{};
            CursorInfo.cbSize = sizeof(CursorInfo);
            if (GetCursorInfo(&CursorInfo) && CursorInfo.hCursor) {
                g_lastCursor = CursorInfo.hCursor;
            }

            MH_EnableHook(MH_ALL_HOOKS);
            g_initialized = true;
        }

        void Remove() {
            if (!g_initialized) return;

            RestoreForUnload();

            HMODULE user32 = GetModuleHandleA("user32.dll");
            if (!user32) return;

            FARPROC getCursorPosAddr = GetProcAddress(user32, "GetCursorPos");
            FARPROC setCursorPosAddr = GetProcAddress(user32, "SetCursorPos");
            FARPROC setCursorAddr = GetProcAddress(user32, "SetCursor");
            FARPROC showCursorAddr = GetProcAddress(user32, "ShowCursor");
            FARPROC clipCursorAddr = GetProcAddress(user32, "ClipCursor");
            FARPROC getMouseMovePointsExAddr = GetProcAddress(user32, "GetMouseMovePointsEx");

            if (getCursorPosAddr) MH_DisableHook(getCursorPosAddr);
            if (setCursorPosAddr) MH_DisableHook(setCursorPosAddr);
            if (setCursorAddr) MH_DisableHook(setCursorAddr);
            if (showCursorAddr) MH_DisableHook(showCursorAddr);
            if (clipCursorAddr) MH_DisableHook(clipCursorAddr);
            if (getMouseMovePointsExAddr) MH_DisableHook(getMouseMovePointsExAddr);

            g_HookFunctions::g_oGetCursorPos = nullptr;
            g_HookFunctions::g_oSetCursorPos = nullptr;
            g_HookFunctions::g_oSetCursor = nullptr;
            g_HookFunctions::g_oShowCursor = nullptr;
            g_HookFunctions::g_oClipCursor = nullptr;
            g_HookFunctions::g_oGetClipCursor = nullptr;
            g_HookFunctions::g_oGetMouseMovePointsEx = nullptr;
            g_menuCursorStateActive = false;
            g_hasDeferredCursorRequest = false;
            g_deferredCursor = nullptr;
            g_hasClipRectBeforeMenu = false;
            g_hasDeferredClipRequest = false;
            g_deferredClipIsNull = true;
            g_initialized = false;
        }
    }

    namespace rawinputhook {
        bool FlightWheelCaptured() {
            const auto V=PhoenixDeinoUI::Read();
            DWORD PID=0; GetWindowThreadProcessId(GetForegroundWindow(),&PID);
            return g_Config::bDeinoFlight && !g_MenuState::g_isOpen && PID==GetCurrentProcessId() &&
                PhoenixDeinoUI::Fresh(V,GetTickCount64()) && (V.Panel ||
                (V.Keys[2] && (GetAsyncKeyState(V.Keys[2])&0x8000)) || (V.Keys[3] && (GetAsyncKeyState(V.Keys[3])&0x8000)));
        }
        void FilterFlightWheel(RAWINPUT* R) {
            if(R && R->header.dwType==RIM_TYPEMOUSE && (R->data.mouse.usButtonFlags&RI_MOUSE_WHEEL)) {
                R->data.mouse.usButtonFlags&=~RI_MOUSE_WHEEL; R->data.mouse.usButtonData=0;
            }
        }
        UINT WINAPI hkGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader) {
            g_Hook::CallbackScope Guard;

            if (g_MenuState::g_isOpen && g_InputState::g_blockMouseInput) {
                if (pcbSize) *pcbSize = 0;
                if (pData && pcbSize && *pcbSize > 0) {
                    memset(pData, 0, *pcbSize);
                }
                return 0;
            }

            const UINT Result=g_HookFunctions::g_oGetRawInputData ? g_HookFunctions::g_oGetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader) : 0;
            if(Result!=UINT(-1) && Result>=sizeof(RAWINPUT) && uiCommand==RID_INPUT && pData && FlightWheelCaptured())
                FilterFlightWheel(static_cast<RAWINPUT*>(pData));
            return Result;
        }

        UINT WINAPI hkGetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader) {
            g_Hook::CallbackScope Guard;

            if (g_MenuState::g_isOpen && g_InputState::g_blockMouseInput) {
                if (pcbSize) {
                    *pcbSize = 0;
                }

                if (pData && pcbSize && *pcbSize > 0) {
                    memset(pData, 0, *pcbSize);
                }

                return 0;
            }

            const UINT Result=g_HookFunctions::g_oGetRawInputBuffer ? g_HookFunctions::g_oGetRawInputBuffer(pData, pcbSize, cbSizeHeader) : 0;
            if(Result!=UINT(-1) && pData && FlightWheelCaptured()) {
                auto* R=pData;
                for(UINT I=0;I<Result;++I) {
                    FilterFlightWheel(R);
                    R=reinterpret_cast<RAWINPUT*>((reinterpret_cast<uintptr_t>(R)+R->header.dwSize+7u)&~uintptr_t(7u));
                }
            }
            return Result;
        }

        void Init() {
            HMODULE user32 = GetModuleHandleA("user32.dll");
            if (!user32) return;

            FARPROC getRawInputDataAddr = GetProcAddress(user32, "GetRawInputData");
            FARPROC getRawInputBufferAddr = GetProcAddress(user32, "GetRawInputBuffer");

            if (getRawInputDataAddr) {
                MH_CreateHook(getRawInputDataAddr, reinterpret_cast<LPVOID>(hkGetRawInputData), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oGetRawInputData));
            }

            if (getRawInputBufferAddr) {
                MH_CreateHook(getRawInputBufferAddr, reinterpret_cast<LPVOID>(hkGetRawInputBuffer), reinterpret_cast<LPVOID*>(&g_HookFunctions::g_oGetRawInputBuffer));
            }

            if (getRawInputDataAddr) MH_EnableHook(getRawInputDataAddr);
            if (getRawInputBufferAddr) MH_EnableHook(getRawInputBufferAddr);
        }

        void Remove() {
            HMODULE user32 = GetModuleHandleA("user32.dll");
            if (!user32) return;

            FARPROC getRawInputDataAddr = GetProcAddress(user32, "GetRawInputData");
            FARPROC getRawInputBufferAddr = GetProcAddress(user32, "GetRawInputBuffer");

            if (getRawInputDataAddr && g_HookFunctions::g_oGetRawInputData) {
                MH_DisableHook(getRawInputDataAddr);
                MH_RemoveHook(getRawInputDataAddr);
            }

            if (getRawInputBufferAddr && g_HookFunctions::g_oGetRawInputBuffer) {
                MH_DisableHook(getRawInputBufferAddr);
                MH_RemoveHook(getRawInputBufferAddr);
            }

            g_HookFunctions::g_oGetRawInputData = nullptr;
            g_HookFunctions::g_oGetRawInputBuffer = nullptr;
        }
    }

    namespace inputhook {
        LRESULT APIENTRY WndProcHook(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
        static PhoenixInput::WindowProcedureRoutes sWindowRoutes(WndProcHook);
        static bool g_menuKeyDown = false;

        void UpdateInputBlockState() {
            HWND foreground = GetForegroundWindow();
            bool isGameForeground = (foreground == g_ProcessWindow::g_mainWindow);
            char className[256] = { 0 };

            if (foreground && foreground != g_ProcessWindow::g_mainWindow) {
                GetClassNameA(foreground, className, sizeof(className));
            }

            std::unordered_set<std::string> menuClasses = {
                "#32770",
                "ConsoleWindowClass",
                "Edit",
                "ListBox",
            };

            bool gameHasMenuOpen = false;

            if (foreground && foreground != g_ProcessWindow::g_mainWindow) {
                DWORD pid;
                GetWindowThreadProcessId(foreground, &pid);
                DWORD gamePid = GetCurrentProcessId();

                if (pid == gamePid) {
                    gameHasMenuOpen = true;
                }
            }

            g_InputState::g_blockMouseInput = (g_MenuState::g_isOpen && !gameHasMenuOpen);
        }

        LRESULT APIENTRY WndProcHook(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            g_Hook::CallbackScope Guard;
            const auto Route = sWindowRoutes.Resolve(hwnd);
            const WNDPROC originalWndProc = Route.Original;
            if (!Route.Active || g_Hook::IsStopping()) return originalWndProc ? CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam) : DefWindowProc(hwnd, uMsg, wParam, lParam);

            if (uMsg == WM_CLOSE || uMsg == WM_DESTROY ||
                uMsg == WM_QUERYENDSESSION || (uMsg == WM_ENDSESSION && wParam != FALSE)) {
                // Run Lua shutdown callbacks while Unreal objects are still
                // valid and before the original window procedure tears down
                // the game window.
                LuaManager::Get().SetEnabled(false);

                // Start the same coordinated teardown used by the menu unload
                // button before Unreal destroys objects whose VMTs we hooked.
                // Keep this asynchronous: the worker waits for this WndProc
                // callback to return before it releases the DLL.
                g_Hook::RequestUnload();
            }

            const bool KeyDown = uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN;
            const bool KeyUp = uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP;
            const unsigned Vk = static_cast<unsigned>(wParam);
            const bool Repeated = (lParam & (1LL << 30)) != 0;
            const unsigned Modifiers = ((GetKeyState(VK_CONTROL) & 0x8000) ? PhoenixInterface::Ctrl : 0) |
                ((GetKeyState(VK_MENU) & 0x8000) ? PhoenixInterface::Alt : 0) |
                ((GetKeyState(VK_SHIFT) & 0x8000) ? PhoenixInterface::Shift : 0);
            const unsigned Chord = Vk | Modifiers;
            static bool Consumed[256]{};
            static bool BindingHeld = false;
            static unsigned BindingVk = 0;
            if (uMsg == WM_KILLFOCUS) {
                PhoenixDeinoUI::Clear();
                std::fill(std::begin(Consumed), std::end(Consumed), false);
                g_menuKeyDown = false; BindingHeld = false;
                g_MenuState::g_pCurrentBindingKey = nullptr;
                PhoenixShortcuts::PendingToggles.store(0);
            }
            if (BindingHeld && (KeyDown || KeyUp) && Vk == BindingVk) {
                if (KeyUp) BindingHeld = false;
                return 0;
            }
            if (g_MenuState::g_pCurrentBindingKey) {
                if (KeyDown && !Repeated) {
                    if (Vk == VK_ESCAPE) g_MenuState::g_pCurrentBindingKey = nullptr;
                    else if (Vk != VK_CONTROL && Vk != VK_MENU && Vk != VK_SHIFT) {
                        const unsigned NewKey = (Vk == VK_BACK || Vk == VK_DELETE) ? 0 : Chord;
                        if (PhoenixShortcuts::Assign(*g_MenuState::g_pCurrentBindingKey, NewKey))
                            g_MenuState::g_pCurrentBindingKey = nullptr;
                    }
                    BindingHeld = true; BindingVk = Vk;
                }
                if (KeyDown || KeyUp || uMsg == WM_CHAR || uMsg == WM_SYSCHAR) return 0;
            }
            if ((KeyDown || KeyUp) && Vk < 256 && Consumed[Vk]) {
                if (KeyUp) Consumed[Vk] = false;
                return 0;
            }
            const bool Foreground = GetForegroundWindow() == hwnd;
            if (KeyDown && PhoenixInterface::ShouldTrigger(PhoenixShortcuts::StopKey, Chord,
                Repeated, Foreground, false, g_MenuState::g_isOpen, true)) {
                PhoenixShortcuts::StopAll(); PhoenixShortcuts::RequestStop();
                if (Vk < 256) Consumed[Vk] = true;
                return 0;
            }
            if (KeyDown && PhoenixShortcuts::GameplayReady.load()) {
                for (size_t I = 0; I < std::size(PhoenixShortcuts::Actions); ++I) {
                    if (PhoenixInterface::ShouldTrigger(PhoenixShortcuts::Actions[I].Key, Chord,
                        Repeated, Foreground, false, g_MenuState::g_isOpen)) {
                        PhoenixShortcuts::PendingToggles.fetch_xor(1u << I);
                        if (Vk < 256) Consumed[Vk] = true;
                        return 0;
                    }
                }
            }

            if (KeyDown && Chord == g_MenuState::g_openKey && !g_menuKeyDown && !Repeated && Foreground) {
                g_menuKeyDown = true;
                g_MenuState::g_isOpen = !g_MenuState::g_isOpen;

                // 状态重置
                if (ImGui::GetCurrentContext() != nullptr) {
                    ImGuiIO& io = ImGui::GetIO();

                    if (g_MenuState::g_isOpen) {
                        // 开启菜单时：清除鼠标按键状态，防止带入之前的点击或拖拽指令
                        io.ClearInputKeys();
                        // 强制将当前帧的鼠标位置设置为系统真实位置，防止 ImGui 沿用上一帧的缓存坐标
                        POINT p;
                        if (g_HookFunctions::g_oGetCursorPos) g_HookFunctions::g_oGetCursorPos(&p);
                        else GetCursorPos(&p);
                        ScreenToClient(hwnd, &p);
                        io.MousePos = ImVec2((float)p.x, (float)p.y);
                    }
                    else {
                        // 关闭菜单时：将 ImGui 的鼠标坐标移出屏幕，防止关闭瞬间 ImGui 还在触发 Hover 状态导致系统光标视觉撕裂
                        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
                        for (int i = 0; i < ImGuiMouseButton_COUNT; i++) {
                            io.MouseDown[i] = false;
                        }
                    }
                }

                // Cursor/raw-input detours are initialized with the renderer.
                // If F2 arrives earlier, only retain the menu state; the first
                // initialized overlay frame applies input and cursor capture.
                if (ImGui::GetCurrentContext() != nullptr) {
                    UpdateInputBlockState();
                    cursorhook::UpdateCursorState();
                }

                return 0;
            }
            else if (KeyUp && Vk == (g_MenuState::g_openKey & 0xff)) {
                g_menuKeyDown = false;
                return 0;
            }

            if (!g_MenuState::g_isOpen && Foreground && g_Config::bDeinoFlight) {
                const auto V=PhoenixDeinoUI::Read();
                const int Wheel=uMsg==WM_MOUSEWHEEL?static_cast<short>(HIWORD(wParam)):0;
                const bool H=V.Keys[2] && (GetAsyncKeyState(V.Keys[2])&0x8000), U=V.Keys[3] && (GetAsyncKeyState(V.Keys[3])&0x8000);
                if(PhoenixDeinoUI::Input((KeyDown||KeyUp)?Vk:0,KeyDown,Repeated,Wheel,H,U,GetTickCount64())) {
                    if(KeyDown && Vk<256) Consumed[Vk]=true;
                    return 0;
                }
                if((uMsg==WM_CHAR || uMsg==WM_SYSCHAR) && V.Panel && PhoenixDeinoUI::Fresh(V,GetTickCount64()) && (wParam==9 || wParam==32)) return 0;
            }
            if (!g_MenuState::g_isOpen &&
                (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) &&
                g_Hack::HandleFreeCameraKey(static_cast<unsigned int>(wParam),
                    uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN, (lParam & (1LL << 30)) != 0)) return 0;
            if (uMsg == WM_INPUT || uMsg == WM_INPUT_DEVICE_CHANGE) {
                if (g_MenuState::g_isOpen && g_InputState::g_blockMouseInput) {
                    return 0;
                }
            }

            if (g_MenuState::g_isOpen && ImGui::GetCurrentContext() != nullptr) {
                ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam);
                ImGuiIO& io = ImGui::GetIO();

                if (g_InputState::g_blockMouseInput) {
                    switch (uMsg) {
                    case WM_MOUSEMOVE:
                    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
                    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
                    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
                    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
                        return 0;
                    }
                }

                if (io.WantCaptureKeyboard) {
                    switch (uMsg) {
                    case WM_KEYDOWN: case WM_KEYUP:
                    case WM_SYSKEYDOWN: case WM_SYSKEYUP:
                    case WM_CHAR: case WM_SYSCHAR:
                    case WM_DEADCHAR: case WM_SYSDEADCHAR:
                    case WM_HOTKEY:
                        return 0;
                    case WM_IME_SETCONTEXT:
                    case WM_IME_NOTIFY:
                    case WM_IME_STARTCOMPOSITION:
                    case WM_IME_ENDCOMPOSITION:
                    case WM_IME_COMPOSITION:
                    case WM_IME_CHAR:
                        return 0;
                    }
                }
            }

            if (originalWndProc) {
                return CallWindowProc(originalWndProc, hwnd, uMsg, wParam, lParam);
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        void Init(HWND hWindow) {
            if (!hWindow || g_Hook::IsStopping()) return;
            if (sWindowRoutes.Install(hWindow)) g_ProcessWindow::g_mainWindow = hWindow;
        }

        bool Remove(HWND hWindow) {
            return sWindowRoutes.Remove(hWindow);
        }

        bool RemoveAll() {
            return sWindowRoutes.RemoveAll();
        }

        void ReinstallWindowHook() {
            if (!g_ProcessWindow::g_mainWindow) return;

            if (!sWindowRoutes.IsInstalled(g_ProcessWindow::g_mainWindow)) {
                Init(g_ProcessWindow::g_mainWindow);
            }
        }
    }
}
