#include "external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "internal/Menu/DrawImGui.h"
#include "external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "internal/AActor/Tick.h"
#include "internal/UNetConnection/HandleDisconnect.h"
#include "internal/UConsole/OutputTextLine.h"
#include "internal/UGameViewportClient/PostRender.h"
#include "internal/UMovementComponent/PhysicsRotation.h"
#include "internal/AActor/TakeDamage.h"
#include "internal/Config/ConfigManager.h"
#include "internal/Language/LanguageManager.h"
#include "internal/Lua/LuaManager.h"
#include "internal/Util/Util.h"
#include <filesystem>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <system_error>

namespace {
    HANDLE g_StartupLog = INVALID_HANDLE_VALUE;
    std::mutex g_StartupLogMutex;

    std::filesystem::path GetModuleDirectory(LPVOID module) {
        wchar_t modulePath[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(static_cast<HMODULE>(module), modulePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return std::filesystem::path(".");
        }
        return std::filesystem::path(modulePath).parent_path();
    }

    std::string StartupLogTimestamp(bool forFilename = false) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::ostringstream stream;
        stream << std::setfill('0')
            << std::setw(4) << time.wYear
            << (forFilename ? "" : "-") << std::setw(2) << time.wMonth
            << (forFilename ? "" : "-") << std::setw(2) << time.wDay
            << (forFilename ? "_" : " ") << std::setw(2) << time.wHour
            << (forFilename ? "" : ":") << std::setw(2) << time.wMinute
            << (forFilename ? "" : ":") << std::setw(2) << time.wSecond;
        if (!forFilename) {
            stream << '.' << std::setw(3) << time.wMilliseconds;
        }
        return stream.str();
    }

    void InitializeStartupLog(LPVOID module) {
        const std::filesystem::path moduleDirectory = GetModuleDirectory(module);
        const std::filesystem::path logDirectory = moduleDirectory / "logs";
        std::error_code error;
        std::filesystem::create_directories(logDirectory, error);
        const std::string filename = "phoenix_dll_" + StartupLogTimestamp(true) +
            "_PID" + std::to_string(GetCurrentProcessId()) + ".log";
        const std::filesystem::path logPath =
            (error ? moduleDirectory : logDirectory) / filename;
        g_StartupLog = CreateFileW(logPath.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_StartupLog == INVALID_HANDLE_VALUE) {
            OutputDebugStringA("[phoenix] Unable to create DLL startup log.\n");
        }
    }

    void StartupLog(const std::string& message) {
        std::scoped_lock lock(g_StartupLogMutex);
        const std::string line = "[" + StartupLogTimestamp() + "] " + message + "\r\n";
        OutputDebugStringA(line.c_str());
        if (g_StartupLog == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(g_StartupLog, line.c_str(), static_cast<DWORD>(line.size()),
            &written, nullptr);
        FlushFileBuffers(g_StartupLog);
    }

}

namespace g_Hook {
    void LogLifecycle(const char* Message) { StartupLog(Message ? Message : ""); }
    void CloseLifecycleLog() {
        std::scoped_lock Lock(g_StartupLogMutex);
        if (g_StartupLog != INVALID_HANDLE_VALUE) { CloseHandle(g_StartupLog); g_StartupLog = INVALID_HANDLE_VALUE; }
    }
}

bool init(LPVOID lpParam) {
    const std::filesystem::path moduleDirectory = GetModuleDirectory(lpParam);
    StartupLog("DLL initialization thread started. Module directory: " +
        moduleDirectory.string());
    StartupLog("Waiting for UWorld and UEngine...");
    const ULONGLONG engineWaitStarted = GetTickCount64();

    while (true) {
        if (g_Hook::IsUnloadRequested()) return false;
        SDK::UWorld* World = SDK::UWorld::GetWorld();

        // During the first seconds of process startup GObjects is published before
        // every UObject/FName entry is ready. UEngine::GetEngine() scans that array
        // and resolves names, so calling it while GWorld is still null can race the
        // engine bootstrap and dereference a half-initialized name entry. Wait for
        // the game to publish a world before performing any global object scan.
        if (World) {
            SDK::UEngine* Engine = SDK::UEngine::GetEngine();
            if (Engine) break;
        }

        Sleep(100);
    }
    StartupLog("UWorld and UEngine are ready after " +
        std::to_string(GetTickCount64() - engineWaitStarted) + " ms.");

    if (g_Util::HasCommandLineArg(L"-lowspec")) g_Config::bPotatoGraphics = true;
    // Ark's -novid controls game videos, not the overlay presentation.

	StartupLog("Initializing configuration manager...");
	ConfigManager& configManager = ConfigManager::Get();
	configManager.Initialize((moduleDirectory / "cfg").string());
	bool configLoaded = configManager.LoadConfig("phoenix_autosave.ini");
	// Import an existing installation's settings once; keep the original file.
	std::error_code configError;
	const bool phoenixConfigExists = std::filesystem::exists(
		moduleDirectory / "cfg" / "phoenix_autosave.ini", configError);
	if (!configLoaded && !phoenixConfigExists && !configError &&
		configManager.LoadConfig("38s_autosave.ini")) {
		configLoaded = true;
		const bool migrated = configManager.SaveConfig("phoenix_autosave.ini");
		configManager.RefreshFileList();
		StartupLog(migrated ? "Previous configuration imported into phoenix." :
			"WARNING: previous configuration loaded but could not be saved as phoenix.");
	}
	if (!configLoaded) {
		const bool configSaved = configManager.SaveConfig("phoenix_autosave.ini");
		configManager.RefreshFileList();
		StartupLog(configSaved ?
            "Default configuration created successfully." :
            "WARNING: default configuration could not be saved.");
	}
	else {
		StartupLog("Configuration loaded successfully.");
	}

    StartupLog("Initializing D3D12 hook runtime...");
    if (!g_MDX12::Initialize(lpParam)) {
        StartupLog("ERROR: hook initialization failed; requesting coordinated rollback.");
        return false;
    }
    StartupLog("D3D12 hook runtime returned; registering callbacks...");
    g_MDX12::SetSetupImGuiCallback(g_DrawImGui::MyImGuiDraw);
    g_MDX12::SetSetupActorTickCallback(g_AActor::Tick);
    g_MDX12::SetSetupHandleDisconnectCallback(g_UNetConnection::HandleDisconnect);
    g_MDX12::SetSetupOutputTextLineCallback(g_UConsole::OutputTextLine);
    g_MDX12::SetSetupPostRenderCallback(g_UGameViewportClient::PostRender);
    g_MDX12::SetSetupPhysicsRotationCallback(g_UMovementComponent::PhysicsRotation);
    g_MDX12::SetSetupTakeDamageCallback(g_AActor::TakeDamage);
    StartupLog("All hook callbacks registered.");

    StartupLog("Initializing language resources...");
    LanguageManager::Get().Initialize((moduleDirectory / "language").string());
    LanguageManager::Get().FetchWorkshopScripts();
    StartupLog("Language resources initialized. Initializing Lua runtime...");
    LuaManager::Get().Initialize((moduleDirectory / "lua").string());
    LuaManager::Get().FetchWorkshopScripts();
    StartupLog("DLL initialization completed successfully.");
    return true;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    InitializeStartupLog(lpParam);
    if (!init(lpParam)) { g_Hook::RequestUnload(); return 6; }
    g_Hook::SetRuntimeReady(true);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_Hook::SetModuleHandle(hModule);
        if (HANDLE h = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr)) g_Hook::SetInitializationThread(h);
        break;

    case DLL_PROCESS_DETACH:
        // Cleanup is performed by the explicit unload worker before
        // FreeLibraryAndExitThread. Avoid blocking under the loader lock.
        break;
    }
    return TRUE;
}
