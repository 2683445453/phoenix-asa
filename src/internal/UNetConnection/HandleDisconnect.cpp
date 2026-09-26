#pragma once
#include "../../external/CppSDK/SDK.hpp"
#include "../Lua/LuaManager.h"
#include "../Legit/Aimbot/Aimbot.h"
#include "../Hack/TekSpear.h"
#include "HandleDisconnect.h"

namespace g_UNetConnection {
	void HandleDisconnect(SDK::UNetConnection* UNetConnection) {
		g_Aimbot::Reset();
		g_TekSpear::RequestReset();
		LuaManager::Get().Lua_OnDisconnect(UNetConnection ? UNetConnection->GetFirstIP() : "?", UNetConnection ? UNetConnection->GetPort() : 0);
	}
}
