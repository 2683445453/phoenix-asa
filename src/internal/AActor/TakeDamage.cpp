#pragma once
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "../Config/Configs.h"
#include "../Hack/Hack.h"
#include "../../external/CppSDK/SDK.hpp"

/*
2026/8/22

要做的事情有很多，
 -重新设计ESP
 -重新设计Log
 -重新设计Aimbot

 -让初始化和模块更清晰
 -让lua系统在manual map的情况下正确工作

 -添加更多有用的hook
 -添加自动扔垃圾功能
 -添加材质绘制
 -添加图标绘制

 -添加一个很酷的功能，让用户可以在运行时直接使用FindObject进行调试，不确定有没有用

 一直想做一个好看的菜单，虽然其实目前并不太用得上，应该把时间放在更有价值的事情上，
*/

namespace g_AActor {

	// 无用，不会在游玩专用服务器时工作。除非你想开发单机修改器。
	void TakeDamage(SDK::AActor* _this, float DamageAmount, SDK::FDamageEvent* DamageEvent, SDK::AController* Instigator, SDK::AActor* DamageCauser) {
        if (!_this || !DamageEvent || !Instigator || !DamageCauser) return;

		if (g_Config::bLogDamage) g_Hack::DamageLog(_this, DamageAmount, DamageEvent, Instigator, DamageCauser);
	}
}