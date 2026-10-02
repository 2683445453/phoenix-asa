#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace g_Hack {
    // This is the requested local firing interval. Remote acceptance is measured
    // separately; network role must not silently replace the user's setting.
    inline float EffectiveFireRateMultiplier(bool, float Requested) {
        return std::isfinite(Requested) ? std::clamp(Requested, 0.25f, 100.0f) : 1.0f;
    }

    // 射速修改的枪械类型筛选。蓝图类名（如 WeapRifle_C、WeapTekRifle_C）
    // 运行时才存在，因此按类名关键字分类，并沿父类链向上匹配以覆盖模组
    // 武器对原版武器类的继承。
    enum class FireRateWeaponKind { Other, TekRifle, Rifle, Shotgun, Sniper, Pistol, Crossbow, Bow };

    inline FireRateWeaponKind ClassifyFireRateWeapon(const SDK::AShooterWeapon* Weapon) {
        if (!Weapon || !Weapon->Class) return FireRateWeaponKind::Other;
        for (auto* Class = Weapon->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct)) {
            std::string Name = Class->GetName();
            for (auto& C : Name) if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
            // 泰克步枪必须优先于“rifle”判断（tekrifle 同样包含 rifle）
            if (Name.find("tekrifle") != std::string::npos) return FireRateWeaponKind::TekRifle;
            if (Name.find("shotgun") != std::string::npos) return FireRateWeaponKind::Shotgun;
            if (Name.find("sniper") != std::string::npos || Name.find("longneck") != std::string::npos)
                return FireRateWeaponKind::Sniper;
            if (Name.find("rifle") != std::string::npos) return FireRateWeaponKind::Rifle;
            if (Name.find("pistol") != std::string::npos) return FireRateWeaponKind::Pistol;
            if (Name.find("crossbow") != std::string::npos) return FireRateWeaponKind::Crossbow;
            if (Name.find("bow") != std::string::npos) return FireRateWeaponKind::Bow;
        }
        return FireRateWeaponKind::Other;
    }

    inline bool FireRateKindEnabled(FireRateWeaponKind Kind) {
        switch (Kind) {
            case FireRateWeaponKind::TekRifle: return g_Config::bFireRateTekRifle;
            case FireRateWeaponKind::Rifle: return g_Config::bFireRateRifle;
            case FireRateWeaponKind::Shotgun: return g_Config::bFireRateShotgun;
            case FireRateWeaponKind::Sniper: return g_Config::bFireRateSniper;
            case FireRateWeaponKind::Pistol: return g_Config::bFireRatePistol;
            case FireRateWeaponKind::Crossbow: return false;
            case FireRateWeaponKind::Bow: return g_Config::bFireRateBow;
            default: return g_Config::bFireRateOther;
        }
    }
}
