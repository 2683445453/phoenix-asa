#pragma once
#include "../Config/Configs.h"
#include "../Config/InterfaceRules.h"
#include <string>
#include <array>
#include <atomic>

namespace PhoenixShortcuts {
    struct Action { const char* Id; const char* Label; unsigned Key; bool* Enabled; };
    inline unsigned MenuKey = PhoenixInterface::DefaultMenuKey;
    inline unsigned StopKey = PhoenixInterface::DefaultStopKey;
    inline Action Actions[] = {
        {"aim", "自动锁定", 0, &g_Config::bAimbotEnabled},
        {"trigger", "自动开火", 0, &g_Config::bTriggerbotEnabled},
        {"parry", "长矛自动招架", 0, &g_Config::bTekSpearAutoBlock},
        {"deflect", "飞行物偏转", 0, &g_Config::bTekSpearAutoDeflect},
        {"harvest", "骑乘自动采集", 0, &g_Config::bAutoHarvestResources},
        {"dino_range", "普通坐骑本地采集范围", 0, &g_Config::bDinoHarvestRange},
        {"loot", "自动吸包", 0, &g_Config::bAutoLootContainers},
        {"pickup", "自动拾取", 0, &g_Config::bAutoPickupDroppedItems},
        {"craft", "自动制作", 0, &g_Config::bAutoCraft},
        {"repair", "自动维修", 0, &g_Config::bAutoRepairItems},
        {"gacha", "自动开水晶", 0, &g_Config::bAutoOpenGachaCrystals},
        {"fishing", "自动钓鱼 QTE", 0, &g_Config::bAutoFishingQTE},
        {"camera", "灵魂出窍", 'Z', &g_Config::bFreeCameraEnabled},
        {"models", "原生模型显示", 0, &g_Config::bModelHighlightEnabled},
        {"resources", "资源高亮", 0, &g_Config::bResourceESPEnabled},
        {"drop", "自动丢弃物资", 0, &g_Config::bAutoDropResources},
        {"underwater", "水下枪械与相枪", 0, &g_Config::bUnderwaterWeapons},
        {"grinder", "装备建筑转入研磨机", 0, &g_Config::bAutoDepositGachaGear},
        {"blueprints", "蓝图转入保险箱", 0, &g_Config::bAutoDepositFishingBlueprints}
    };
    inline std::atomic<bool> GameplayReady{false}, StopRequested{false};
    inline std::atomic<int> PendingLevelStat{-1};
    inline std::atomic<unsigned> PendingToggles{0};
    inline void RequestStop() { PendingToggles.store(0); StopRequested.store(true); }
    inline std::string BindingError;
    inline const char* Conflict(unsigned Key, const unsigned* Except) {
        if (!Key) return nullptr;
        if (Except != &MenuKey && Key == MenuKey) return "菜单开关";
        if (Except != &StopKey && Key == StopKey) return "停止自动化";
        for (const auto& A : Actions)
            if (&A.Key != Except && A.Key == Key) return A.Label;
        return nullptr;
    }
    inline bool Assign(unsigned& Target, unsigned Chord) {
        if (!Chord && (&Target == &MenuKey || &Target == &StopKey)) {
            BindingError = "菜单与停止键必须保留一个有效绑定。"; return false;
        }
        if (Chord && !PhoenixInterface::IsAssignable(Chord)) {
            BindingError = "请选择字母、数字、F1–F24、Insert、Home、End 或翻页键，可加 Ctrl / Alt / Shift。"; return false;
        }
        if (const char* Name = Conflict(Chord, &Target)) {
            BindingError = std::string("该快捷键已用于：") + Name; return false;
        }
        Target = Chord; BindingError.clear(); return true;
    }
    inline void Normalize() {
        // Validate the entire loaded set in stable order, preserving unrelated bindings.
        if (!PhoenixInterface::IsAssignable(MenuKey)) MenuKey = PhoenixInterface::DefaultMenuKey;
        if (!PhoenixInterface::IsAssignable(StopKey) || StopKey == MenuKey)
            StopKey = MenuKey == PhoenixInterface::DefaultStopKey ? PhoenixInterface::Alt | 0x7b : PhoenixInterface::DefaultStopKey;
        for (size_t I = 0; I < std::size(Actions); ++I) {
            unsigned& K = Actions[I].Key;
            if (!K) continue;
            if (!PhoenixInterface::IsAssignable(K) || K == MenuKey || K == StopKey) { K = 0; continue; }
            for (size_t J = 0; J < I; ++J) if (Actions[J].Key == K) { K = 0; break; }
        }
    }
    inline void StopAll() {
        PendingLevelStat.store(-1);
        bool* Flags[] = {
            &g_Config::bFastAim, &g_Config::bNoRecoil, &g_Config::bFastBow, &g_Config::bInstantThrowables, &g_Config::bProjectileTrajectoryTuning, &g_Config::bTekNoHeat,
            &g_Config::bMountedWeapons, &g_Config::bMountedFullRotation, &g_Config::bMountedThirdPerson,
            &g_Config::bDinoMoveSpeed, &g_Config::bDeinonychusBigJump, &g_Config::bDeinonychusAirSpeed,
            &g_Config::bGargantarClient, &g_Config::bGargantarReach, &g_Config::bGargantarFlightSpeed,
            &g_Config::bGargantarFastHold, &g_Config::bGargantarAttackInterval, &g_Config::bGargantarTransitVisual,
            &g_Config::bNativeMountJump, &g_Config::bNativeMountHover,
            &g_Config::bDeinoAirLatch,
            &g_Config::bDeinoGlideProbe, &g_Config::bDeinoAutoJumpTest, &g_Config::bDeinoLatchTrace, &g_Config::bDeinoPointPounceTest,
            &g_Config::bDeinoPointZeroStart, &g_Config::bDeinoFlight, &g_Config::bDeinoHoverFollowView,
            &g_Config::bDeinoEndHoverTest, &g_Config::bDeinoAutoVectorTest, &g_Config::bDeinoEndJumpTest,
            &g_Config::bJackalopeBigJump, &g_Config::bJackalopeChargeCap,
            &g_Config::bJackalopeFastCharge, &g_Config::bFloatingStructures,
            &g_Config::bAutoClaimBabies, &g_Config::bAutoImprint, &g_Config::bAutoMount,
            &g_Config::bAutoActivateTurrets, &g_Config::bAutoExtractEmbryos, &g_Config::bAutoRemoveBola,
            &g_Config::bAutoFillGacha, &g_Config::bAutoFeedBabies, &g_Config::bAutoFertilize, &g_Config::bAutoFillTurrets,
            &g_Config::bAimbotEnabled, &g_Config::bTriggerbotEnabled,
            &g_Config::bTekSpearAutoBlock, &g_Config::bTekSpearAutoDeflect,
            &g_Config::bAutoHarvestResources, &g_Config::bDinoHarvestRange, &g_Config::bAutoLootContainers,
            &g_Config::bAutoPickupDroppedItems, &g_Config::bAutoCraft,
            &g_Config::bAutoRepairItems, &g_Config::bAutoFeed,
            &g_Config::bAutoGeneScannerMarker, &g_Config::bAutoEatDrink,
            &g_Config::bAutoMedicalBrew, &g_Config::bAutoEnergyBrew,
            &g_Config::bAutoBuffSoup, &g_Config::bAutoFireExtinguish,
            &g_Config::bAutoOpenGachaCrystals, &g_Config::bAutoPullGachaCrystals,
            &g_Config::bAutoDropResources, &g_Config::bUnderwaterWeapons,
            &g_Config::bAutoDepositGachaGear, &g_Config::bAutoDepositFishingBlueprints,
            &g_Config::bAutoFishingQTE, &g_Config::bAutoFishingRecast,
            &g_Config::bAutoSwitchFishingRod, &g_Config::bAutoRecoverFishingBait,
            &g_Config::bFreeCameraEnabled, &g_Config::bAutomatic, &g_Config::bSuperFlyer,
            &g_Config::bForceTurn, &g_Config::bWeaponFireRate, &g_Config::bPumpShotgunNoReload,
            // 秒换弹与弓秒蓄力会修改当前武器字段，必须纳入停止自动化。
            &g_Config::bInstantReload, &g_Config::bFastBow, &g_Config::bSprintAttack,
            &g_Config::bShotgunConverge, &g_Config::bTekChestJetNoCooldown,
            &g_Config::bTekGloveDashNoCooldown, &g_Config::bDinoAttackSpeed,
            &g_Config::bDragontopiaSkillTuning, &g_Config::bTastyBaitOverride,
            &g_Config::bFastRespawned
        };
        for (bool* Flag : Flags) *Flag = false;
    }
}
