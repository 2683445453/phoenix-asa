#pragma once
#include "../Config/Configs.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

// 秒换弹 / 秒蓄力的纯规则层（离线可测，不依赖 SDK 对象）。
// 运行时实现见 Hack/InstantReload.inl；菜单见 Menu/Aimbot_Menu.cpp（Topic::Weapons）。
//
// 本文件刻意不依赖 SDK 类型，因而可以脱离游戏做离线单测；
// 需要读写游戏字段的补丁器在 InstantReloadPatch.h（自带 SDK.hpp）。
namespace PhoenixReload {
    // 秒换弹覆盖的武器族。泰克榴弹发射器是 Genesis DLC 蓝图类，SDK 里没有对应
    // C++ 声明，只能按运行时类名识别（见 ClassifyReloadWeapon）。
    enum class WeaponFamily { None, PumpShotgun, Launcher, Rifle, Crossbow, OtherGun, Bow };
    enum class BowFamily { None, Wood, Compound, Tek };

    inline std::string Normalize(std::string Text) {
        std::string Result;
        Result.reserve(Text.size());
        for (unsigned char C : Text) {
            if (C >= 'A' && C <= 'Z') { Result += char(C + 'a' - 'A'); continue; }
            Result += char(C);
        }
        return Result;
    }
    inline bool Has(const std::string& Haystack, std::string_view Needle) {
        return Haystack.find(Needle) != std::string::npos;
    }
    // 注意：不能用 initializer_list 形参——花括号实参无法推导该模板形参。
    template<class... Needles>
    inline bool HasAny(const std::string& Haystack, Needles... Values) {
        return (Has(Haystack, std::string_view(Values)) || ...);
    }

    // 「制式步枪」= 原版突击步枪（WeapRifle_C / PrimalItem_WeaponRifle_C）。
    // 泰克步枪单列，避免 "tekrifle" 同时命中 "rifle"。
    inline WeaponFamily ClassifyReloadWeapon(const std::string& Identity) {
        const std::string S = Normalize(Identity);
        if (S.empty()) return WeaponFamily::None;
        // 榴弹发射器必须先判：它同时含 grenade 与 launcher 字样。
        if (HasAny(S, "tekgrenadelauncher", "primalweapontekgrenadelauncher",
                "weaptekgrenadelauncher", "grenadelauncher")) return WeaponFamily::Launcher;
        if (Has(S, "crossbow")) return WeaponFamily::Crossbow;
        if (HasAny(S, "tekbow", "boomarang", "boomerang")) return WeaponFamily::Bow;
        if (Has(S, "bow")) return WeaponFamily::Bow;
        if (HasAny(S, "pumpactionshotgun", "machinedshotgun", "shotgun")) return WeaponFamily::PumpShotgun;
        if (HasAny(S, "one_shot_rifle", "oneshotrifle", "longneck")) return WeaponFamily::Rifle;
        if (HasAny(S, "tekrifle", "rifle")) return WeaponFamily::Rifle;
        if (HasAny(S, "sniper", "pistol", "flamethrower", "minigun", "harpoon", "railgun", "cannon",
                "launcher", "rocket", "revolver")) return WeaponFamily::OtherGun;
        return WeaponFamily::None;
    }

    inline BowFamily ClassifyBow(const std::string& Identity) {
        const std::string S = Normalize(Identity);
        if (S.empty() || !Has(S, "bow")) return BowFamily::None;
        if (Has(S, "tekbow")) return BowFamily::Tek;
        if (Has(S, "compoundbow")) return BowFamily::Compound;
        // 模组弓继承原版弓类时会命中 bow 但没有原版类名；归入木弓开关即可。
        return BowFamily::Wood;
    }

    inline bool ReloadFamilyEnabled(WeaponFamily Family) {
        switch (Family) {
            case WeaponFamily::PumpShotgun: return g_Config::bInstantReloadPumpShotgun;
            case WeaponFamily::Launcher: return g_Config::bInstantReloadLauncher;
            case WeaponFamily::Rifle: return g_Config::bInstantReloadRifle;
            case WeaponFamily::Crossbow: return g_Config::bInstantReloadCrossbow;
            case WeaponFamily::OtherGun: return g_Config::bInstantReloadOther;
            default: return false;
        }
    }
    inline const char* ReloadFamilyLabel(WeaponFamily Family) {
        switch (Family) {
            case WeaponFamily::PumpShotgun: return "泵动式霰弹枪";
            case WeaponFamily::Launcher: return "泰克榴弹发射器";
            case WeaponFamily::Rifle: return "制式步枪";
            case WeaponFamily::Crossbow: return "制式弩";
            case WeaponFamily::OtherGun: return "其他枪械";
            default: return "未匹配";
        }
    }
    inline bool BowFamilyEnabled(BowFamily Family) {
        switch (Family) {
            case BowFamily::Wood: return g_Config::bFastBowWood;
            case BowFamily::Compound: return g_Config::bFastBowCompound;
            case BowFamily::Tek: return g_Config::bFastBowTek;
            default: return false;
        }
    }
    inline const char* BowFamilyLabel(BowFamily Family) {
        switch (Family) {
            case BowFamily::Wood: return "木弓";
            case BowFamily::Compound: return "复合弓";
            case BowFamily::Tek: return "泰克复合弓";
            default: return "未匹配";
        }
    }

    inline constexpr float InstantChargePullSeconds = 0.001f;

    // Remote ammo is authoritative; Tek launcher slots have a separate Blueprint reload path.
    inline bool CanPatchReload(bool Authority, WeaponFamily Family) {
        return Authority && Family != WeaponFamily::None && Family != WeaponFamily::Bow &&
            Family != WeaponFamily::Launcher;
    }
    inline constexpr int MaxSaneClipCapacity = 512;
    inline int SanitizeClipCapacity(int Value, int Fallback) {
        if (Value >= 1 && Value <= MaxSaneClipCapacity) return Value;
        return (Fallback >= 1 && Fallback <= MaxSaneClipCapacity) ? Fallback : 1;
    }

}
