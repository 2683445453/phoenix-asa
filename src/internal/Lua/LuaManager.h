#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>

#pragma comment(lib, "wininet.lib")

#define SOL_ALL_SAFETIES_ON 1
#include "../../external/sol2/sol.hpp"
#include "../Util/Util.h"

namespace fs = std::filesystem;

struct LuaScript {
    std::string name;
    fs::path path;
    bool isLoaded = false;
    bool hasError = false;
    bool isWorkshop = false;
    bool isLibrary = false;

    std::string downloadUrl;
    std::string scriptContent;
    std::string lastError;

    sol::environment env;
};

class LuaManager {
public:
    static LuaManager& Get() {
        static LuaManager* instance = nullptr;
        if (!instance) {
            instance = new LuaManager();
        }
        return *instance;
    }

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

    void Initialize(const std::string& scriptDir);
    void Shutdown();
    void ReloadAll();
    void RefreshFileList();
    std::vector<LuaScript>& GetScripts() { return m_scripts; }
    bool SetScriptState(int index, bool load);

    lua_State* GetState() { return m_lua ? m_lua->lua_state() : nullptr; }
    const std::string& GetScriptDir() const { return m_scriptDir; }
    
    void Lua_OnShutDown(LuaScript& script);
    void Lua_OnPostRender();
    void Lua_OnConsoleMessage(std::string Message);
    void Lua_OnDisconnect(std::string ServerIP, int32_t ServerPort);
    void Lua_OnWorldTick();
    void Lua_OnPaintMenu(float MenuAlpha);
    void Lua_OnPaint();
    void Lua_OnMenuOpen();

    void Update();
    void ActualReloadAll();

    void FetchWorkshopScripts();

private:
    static bool CheckNoLuaFlag() {
        return g_Util::HasCommandLineArg(L"-nolua");
    }

    bool m_enabled = !CheckNoLuaFlag();
    bool m_needsReload = false;
    std::mutex m_luaMutex;
    std::atomic<bool> m_pendingReset{ false };
    std::atomic<bool> m_stopRequested{ false };
    std::thread m_fetchThread;

    LuaManager() = default;
    ~LuaManager() { Shutdown(); }

    std::unique_ptr<sol::state> m_lua;
    std::string m_scriptDir;
    std::vector<LuaScript> m_scripts;

    void InitVM();
    void Bind_G();
    void BindClient();
    void BindImGui();
    void BindSDK();
    void BindSystem();
    bool ExecuteScript(LuaScript& script);

    std::string HttpRequest(const std::string& url);
};
