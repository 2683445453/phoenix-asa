#pragma once
#include "../../external/CppSDK/SDK.hpp"
#include "../Lua/LuaManager.h"
#include "OutputTextLine.h"

namespace g_UConsole {
	void OutputTextLine(SDK::UConsole* Console, SDK::FString* Message) {
		LuaManager::Get().Lua_OnConsoleMessage(Message->ToString());
	}
}