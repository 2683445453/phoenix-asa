#pragma once
#include <string_view>

namespace g_Hack {
    // 自动灭火的纯规则：身体着火 buff 与灭火手雷的物品类名匹配。
    // 调用方负责构造归一化（小写、去非字母数字）的类名链。
    inline bool IsBurningBuffChain(std::string_view NormalizedClassChain) {
        // 燃烧 buff（Buff_Burning_C / Buff_Ignition_C 等）。避免单用 "fire"，
        // 防止把火系坐骑、火焰龙等增益误判为燃烧。
        return NormalizedClassChain.find("burning") != std::string_view::npos ||
            NormalizedClassChain.find("ignit") != std::string_view::npos ||
            NormalizedClassChain.find("onfire") != std::string_view::npos;
    }

    inline bool IsAntifireGrenadeChain(std::string_view NormalizedClassChain) {
        // 灭火手雷物品：PrimalItem_AntifireGrenade_C（模组变体沿继承链匹配）。
        return NormalizedClassChain.find("antifire") != std::string_view::npos;
    }

    inline bool IsThrowableGrenadeChain(std::string_view NormalizedClassChain) {
        // 手雷类投掷物物品（碎片/灭火/烟雾/毒气/泰克/集束等）；排除榴弹发射器等
        // 非投掷武器（TekGrenadeLauncher 同样含 grenade 字样）。
        return NormalizedClassChain.find("grenade") != std::string_view::npos &&
            NormalizedClassChain.find("launcher") == std::string_view::npos;
    }
}
