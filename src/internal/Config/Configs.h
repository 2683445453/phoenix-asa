#pragma once
#include "../../external/CppSDK/SDK.hpp"

namespace g_Config {
#define PHX_BOOL(Name, Default) inline bool Name = Default;
#define PHX_FLOAT(Name, Default, Min, Max) inline float Name = Default;
#define PHX_COLOR(Name, R, G, B, A) inline float Name[4] = {R,G,B,A};
#include "FeatureExpansion.inc"
#include "DeinoLegacyRuntime.inc"
#undef PHX_BOOL
#undef PHX_FLOAT
#undef PHX_COLOR
    inline bool bShowFeatureHelp = true;
    inline bool bAutoDropResources = false;
    inline int AutoDropResourceMask = 0;
    inline bool bUnderwaterWeapons = false;
    inline bool bShowWelcome = true;
    inline bool bStaticThreatIndicators = true;
    // 菜单设置
    inline bool bMenuNeedReset = false;
    inline bool bMenuLockResize = false;
    inline float MenuColor[4] = { 0.431f, 0.906f, 0.718f, 1.000f };
    inline int MenuScaleIdx = 1;

    inline bool bInitWelcome = false;

    inline bool bWeaponFireRate = false;
    inline float WeaponFireRateMultiplier = 2.0f;
    // 射速修改枪械类型筛选
    inline bool bFireRateTekRifle = true;
    inline bool bFireRateRifle = true;
    inline bool bFireRateShotgun = true;
    inline bool bFireRateSniper = true;
    inline bool bFireRatePistol = true;
    inline bool bFireRateBow = true;
    inline bool bFireRateOther = true;
    // 独立于通用射速倍率的制式弩按住速射；只在本地主机应用。
    inline bool bCrossbowHoldRapidFire = false;
    inline bool bProjectilePrediction = false;
    inline float ProjectilePredictionMaxSeconds = 3.0f;
    inline float ProjectilePredictionDelayMs = 0.0f;
    inline bool bAutoLootContainers = false;
    inline bool bAutoLootSupplyCrates = true;
    inline bool bAutoLootBags = true;
    inline bool bAutoLootBeaverDams = false;
    inline bool bAutoLootWildEggs = true;
    inline float AutoLootRangeMeters = 10.0f;
    inline float AutoLootIntervalMs = 750.0f;
    inline float AutoLootMaxWeightPercent = 85.0f;

    // 其他功能
    inline bool bSuperFlyer = false;
    inline bool bForceTurn = false;
    inline bool bAutoFeed = false;
    inline bool bAutoGeneScannerMarker = false;
    inline float AutoFeedRangeMeters = 50.0f;
    inline float AutoFeedIntervalSeconds = 1.0f;
    inline char AutoFeedDinoFilter[256] = "";
    inline bool bAutoPickupDroppedItems = false;
    inline float AutoPickupRangeMeters = 2.5f;
    inline float AutoPickupIntervalMs = 350.0f;
    inline float AutoPickupMaxWeightPercent = 85.0f;
    inline bool bAutoPickupUseDroppedItemFilter = true;
    inline bool bAutoOpenGachaCrystals = false;
    // Delay between batches of up to 32 crystals, not between individual uses.
    inline float AutoOpenGachaIntervalMs = 50.0f;
    inline bool bAutoPullGachaCrystals = false;
    inline float AutoPullGachaRangeMeters = 12.0f;
    inline bool bAutoDepositGachaGear = false;
    inline float AutoDepositGrinderRangeMeters = 12.0f;
    inline bool bAutoDepositFishingBlueprints = false;
    inline float AutoDepositVaultRangeMeters = 12.0f;
    inline bool bAutoFishingQTE = false;
    inline bool bAutoFishingRecast = false;
    inline bool bAutoEatDrink = false;
    inline float AutoEatDrinkThresholdPercent = 45.0f;
    inline bool bAutoMedicalBrew = false;
    inline float MedicalBrewThresholdPercent = 50.0f;
    inline bool bAutoEnergyBrew = false;
    inline float EnergyBrewThresholdPercent = 35.0f;
    // 自动汤类增益（buff 到期自动续）
    inline bool bAutoBuffSoup = false;
    inline bool bSoupBattleTartare = true;
    inline bool bSoupCactusBroth = true;
    inline bool bSoupFocalChili = true;
    inline bool bSoupFriaCurry = true;
    inline bool bSoupCalienSoup = true;
    inline bool bSoupLazarusChowder = true;
    inline bool bSoupEnduroStew = true;
    inline bool bSoupShadowSteak = true;
    inline bool bSoupMushroomBrew = true;
    // 身体着火自动投掷灭火手雷
    inline bool bAutoFireExtinguish = false;
    inline float FireExtinguishIntervalSeconds = 5.0f;
    inline bool bAutoSwitchFishingRod = false;
    inline bool bAutoRecoverFishingBait = false;
    inline bool bPumpShotgunNoReload = false;
    inline bool bAutoRepairItems = false;
    inline float AutoRepairThresholdPercent = 35.0f;
    inline float AutoRepairIntervalSeconds = 2.0f;
    inline bool bAutoRepairIncludeBackpack = false;
    inline bool bAutoCraft = false;
    inline float AutoCraftRangeMeters = 30.0f;
    inline float AutoCraftIntervalSeconds = 1.0f;
    inline bool bDinoAttackSpeed = false;
    inline float DinoAttackSpeedMultiplier = 2.0f;
    inline bool bDragontopiaSkillTuning = false;
    inline float DragontopiaFlightSpeedMultiplier = 1.0f;
    inline float DragontopiaDiveBoostMultiplier = 1.0f;
    inline float DragontopiaManeuverPowerMultiplier = 1.0f;
    inline float DragontopiaStaminaCostMultiplier = 1.0f;
    inline float DragontopiaCooldownMultiplier = 1.0f;
    inline float DragontopiaSkillDamageMultiplier = 1.0f;
    inline bool bTastyBaitOverride = false;
    inline float TastyBaitRank1ReductionPercent = 30.0f;
    inline float TastyBaitRank2ReductionPercent = 60.0f;
    inline bool bSuicide = false;
    inline bool bDropItem = false;
    inline SDK::FItemNetID dropItemID;
    inline bool bUseItem = false;
    inline SDK::FItemNetID useItemID;
    inline SDK::int32 useItemSlotID;

    inline bool bLogDamage = true;
    inline float LogDamageColor[4] = { 1.0f, 1.0f, 1.0f, 0.80f };

    inline bool bFastReconnected = false;
    inline bool bFastRespawned = false;

    inline bool bBlockedChat = false;

    inline bool bPotatoGraphics = false;

    inline bool bShowInfo = true;

    // 生物列表
    inline char entitySearchBuf[256] = { 0 };
    inline bool bEnableFilter = false;

    // 建筑列表
    inline char structureSearchBuf[256] = "";
    inline bool bEnableStructureFilter = false;

    // 物品列表
    inline char itemSearchBuf[256] = "";

    // 自瞄
    inline bool bAimbotEnabled = false;
    inline bool bAimPlayers = true;
    inline bool bAimWildDinos = true;
    inline bool bAimTamedDinos = false;
    inline bool bAimBosses = true;
    // 泰克长矛招架目标类型筛选（玩家 / 生物 / Boss）
    inline bool bSpearAimPlayers = true;
    inline bool bSpearAimCreatures = true;
    inline bool bSpearAimBosses = true;
    inline bool bAimHead = true;
    inline bool bAimBody = true;
    inline bool bAimRemoveSpread = true;
    inline bool bTekPistolHoldTarget = true;
    inline bool bShotgunConverge = true;
    inline bool bTekSpearAutoBlock = false;
    inline bool bTekSpearAutoDeflect = false;
    inline int TekSpearTargetCount = 1;
    inline bool bTekSpearAutoFace = true;
    inline float TekSpearTargetRangeMeters = 300.0f;
    inline bool bDrawTekSpearCircle = true;
    inline bool bDrawTekSpearTargetLine = true;
    inline bool bDrawTekSpearTargetInfo = true;
    inline float TekSpearTargetColor[4] = {0.15f, 0.9f, 1.0f, 0.9f};
    inline float TekSpearMeleeRangeMeters = 4.5f;
    inline float TekSpearParryLeadSeconds = 0.04f;
    inline float TekSpearParryTapSeconds = 0.08f;
    inline float TekSpearProjectileRangeMeters = 60.0f;
    inline float TekSpearLookAheadSeconds = 0.35f;
    inline float AimbotMaxDistanceMeters = 300.0f;
    inline float AimbotCircleRadiusPercent = 30.0f;
    inline bool bDrawAimCircle = true;
    inline bool bDrawAimTargetLine = true;
    inline bool bDrawAimTargetInfo = true;
    inline float AimTargetInfoScale = 1.0f;
    inline float AimCircleThickness = 1.5f;
    inline float AimTargetLineThickness = 1.5f;
    inline float AimTargetMarkerSize = 5.0f;
    inline float AimCircleColor[4] = {1.0f, 1.0f, 1.0f, 0.65f};
    inline float AimTargetLineColor[4] = {1.0f, 0.25f, 0.1f, 0.95f};

    // 扳机
    inline bool bTriggerbotEnabled = false;
    inline float TriggerDelay = 0.0f;
    inline float TriggerRandomPercent = 0.0f;
    inline float TriggerHitChance = 50.0f;

    // 掉落物
    inline bool bDrawDroppedItems = false;
    inline float DroppedItemMaxDistance = 500.0f;

    inline float DroppedItemNameColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline float DroppedItemDistanceColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    inline float DroppedItemMeatColor[4] = { 1.0f, 0.62f, 0.34f, 1.0f };
    inline float DroppedItemCryopodColor[4] = { 1.0f, 0.39f, 1.0f, 1.0f };
    inline float DroppedItemEggColor[4] = { 1.0f, 0.96f, 0.92f, 1.0f };
    inline float DroppedItemPiledColor[4] = { 0.19f, 1.0f, 0.19f, 1.0f };
    inline float DroppedItemWoodColor[4] = { 0.92f, 0.53f, 0.2f, 1.0f };
    inline float DroppedItemThatchColor[4] = { 0.94f, 0.94f, 0.71f, 1.0f };
    inline float DroppedItemHideColor[4] = { 1.0f, 0.82f, 0.67f, 1.0f };
    inline float DroppedItemPeltColor[4] = { 0.69f, 0.69f, 0.69f, 1.0f };
    inline float DroppedItemKeratinColor[4] = { 0.57f, 1.0f, 0.88f, 1.0f };
    inline float DroppedItemChitinColor[4] = { 0.57f, 1.0f, 0.88f, 1.0f };
    inline float DroppedItemCorruptedPolymerColor[4] = { 0.57f, 1.0f, 0.88f, 1.0f };
    inline float DroppedItemPolymer_OrganicColor[4] = { 0.57f, 1.0f, 0.88f, 1.0f };
    inline float DroppedItemPolymerColor[4] = { 0.57f, 1.0f, 0.88f, 1.0f };
    inline float DroppedItemMetalColor[4] = { 0.75f, 0.75f, 0.75f, 1.0f };
    inline float DroppedItemStoneColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    inline float DroppedItemCrystalColor[4] = { 0.78f, 0.96f, 1.0f, 1.0f };
    inline float DroppedItemGemColor[4] = { 0.0f, 1.0f, 0.5f, 1.0f };
    inline float DroppedItemPearlColor[4] = { 0.29f, 0.0f, 0.51f, 1.0f };
    inline float DroppedItemSpoiledMeatColor[4] = { 0.39f, 0.69f, 0.39f, 1.0f };

    inline bool bDroppedItemMeat = false;
    inline bool bDroppedItemCryopod = false;
    inline bool bDroppedItemEgg = false;
    inline bool bDroppedItemPiled = false;
    inline bool bDroppedItemWood = false;
    inline bool bDroppedItemThatch = false;
    inline bool bDroppedItemHide = false;
    inline bool bDroppedItemPelt = false;
    inline bool bDroppedItemKeratin = false;
    inline bool bDroppedItemChitin = false;
    inline bool bDroppedItemCorruptedPolymer = false;
    inline bool bDroppedItemPolymer_Organic = false;
    inline bool bDroppedItemPolymer = false;
    inline bool bDroppedItemMetal = false;
    inline bool bDroppedItemStone = false;
    inline bool bDroppedItemCrystal = false;
    inline bool bDroppedItemGem = false;
    inline bool bDroppedItemPearl = false;
    inline bool bDroppedItemSpoiledMeat = false;

    // 宝箱
    inline bool bDrawSupplyDrops = false;
    inline float SupplyDropMaxDistance = 10000.0f;

    // 建筑
    inline bool bDrawStructures = false;
    inline bool bStructureTypeFilter = false;
    inline bool bStructureAutoTurret = true;
    inline bool bStructureHeavyTurret = true;
    inline bool bStructureTekTurret = true;
    inline bool bStructureGenerator = true;
    inline bool bStructureTekGenerator = true;
    inline bool bStructureWindTurbine = true;
    inline bool bStructureSupplyDrop = true;
    inline bool bStructureLootCrate = true;
    inline bool bStructureBuriedTreasure = true;
    inline bool bStructureOther = false;
    inline bool bStructureShowLootContents = false;
    inline bool bStructureShowOwner = true;
    inline bool bOnlyDrawStructuresEnemy = false;
    inline float StructureMaxDistance = 10000.0f;
    inline float StructureNameColor[4] = { 1.0f, 1.0f, 0.7f, 1.0f };
    inline float StructureOwnerColor[4] = { 0.4f, 1.0f, 1.0f, 1.0f };
    inline float StructureDistanceColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline float StructureHealthColor1[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    inline float StructureHealthColor2[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

    // 建筑 - 队友
    inline bool bTeamDrawStructures = false;
    inline float TeamStructureMaxDistance = 10000.0f;
    inline float TeamStructureHealthColor1[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    inline float TeamStructureHealthColor2[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

    // 水源
    inline bool bDrawWater = false;
    inline float WaterMaxCount = 3;
    inline float WaterMaxDistance = 500.0f;
    inline float WaterNameColor[4] = { 0.0f, 0.75f, 1.0f, 1.0f };
    inline float WaterDistanceColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // 附近资源点（独立于全局 ESP 开关）
    inline float ModelHighlightIntensity = 3.0f;
    inline float ModelHighlightOpacity = 1.0f;
    inline bool bResourceESPEnabled = false;
    inline float ResourceESPMaxDistance = 300.0f;
    inline bool bResourceModelHighlight = true;
    inline bool bResourceShowLabels = false;
    inline bool bResourceMetal = true;
    inline bool bResourceCrystal = true;
    inline bool bResourceObsidian = false;
    inline bool bResourceOil = false;
    inline bool bResourcePearl = false;
    inline bool bResourceElementShard = false;
    inline bool bAutoHarvestResources = false;
    inline bool bDinoHarvestRange = false;
    inline float DinoHarvestRangeMultiplier = 2.0f;
    inline bool bAutoHarvestTekStrider = false;
    inline bool bAutoHarvestApproach = true;
    inline bool bAutoHarvestSprint = true;
    inline bool bAutoHarvestSafeNavigation = true;
    inline bool bAutoHarvestBreakObstacles = true;
    inline bool bAutoHarvestExperimentalNavigation = false;
    inline float AutoHarvestSearchRadiusMeters = 30.0f;
    inline float AutoHarvestAttackDistanceMeters = 4.5f;
    inline float AutoHarvestAttackIntervalMs = 850.0f;
    inline float AutoHarvestMaxSafeDropMeters = 2.5f;
    inline float AutoHarvestTekStriderBeamRangeMeters = 35.0f;
    inline float ResourceMetalColor[4] = { 0.78f, 0.78f, 0.82f, 1.0f };
    inline float ResourceCrystalColor[4] = { 0.45f, 0.92f, 1.0f, 1.0f };
    inline float ResourceObsidianColor[4] = { 0.66f, 0.35f, 0.95f, 1.0f };
    inline float ResourceOilColor[4] = { 0.95f, 0.72f, 0.20f, 1.0f };
    inline float ResourcePearlColor[4] = { 0.95f, 0.95f, 0.88f, 1.0f };
    inline float ResourceElementShardColor[4] = { 0.45f, 1.0f, 0.45f, 1.0f };

    inline float ESPFontPixels = 16.0f;
    inline float ESPIconPixels = 18.0f;
    inline float ESPMinFontPixels = 13.0f;
    inline float ESPBoxPaddingPixels = 2.0f;
    inline float ESPBoxThicknessPixels = 1.25f;
    inline bool bESPCornerBox = true;
    inline bool bESPPreventOverlap = true;

    // 全局
    inline bool bModelHighlightEnabled = false;
    inline bool bESPPlayers = true;
    inline bool bESPCreatures = true;
    inline bool bShowCreatureAggro = false;
    inline float ModelHighlightMaxDistance = 300.0f;
    inline bool ESPCharacterEnabled(bool IsCreature) {
        return IsCreature ? bESPCreatures : bESPPlayers;
    }
    // The old master is retained only for loading legacy configurations.
    inline bool bESPEnabled = false;
	inline int ESPScaleIdx = 1;
	inline float ESPScale = 1.0f;

    inline bool bDrawBox = false;
    inline float BoxColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    inline bool bDrawHealthBar = false;
    inline float HealthBarColor1[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    inline float HealthBarColor2[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

    inline bool bDrawName = false;
    inline float NameColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline bool bDrawSpecies = false;
    inline bool bDrawGrowth = false;
    inline bool bDrawTorpor = false;
    inline float TorporColor[4] = { 0.95f, 0.5f, 0.95f, 1.0f };
    inline bool bDrawRagdoll = false;
    inline float RagdollColor[4] = { 1.0f, 0.0f, 0.25f, 0.75f };
    inline bool bDrawDistance = false;
    inline float DistanceColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline bool bDrawAimPoints = false;
    inline float AimPointsColor[4] = { 1.0f, 0.0f, 0.0f, 0.5f };
    inline bool bDrawAimSkeleton = false;
    inline float AimSkeletonColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // 生物
    inline bool bDinoDrawBox = false;
    inline float DinoBoxColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline bool bDinoDrawHealthBar = false;
    inline bool bDinoDrawName = false;
    inline float DinoNameColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline bool bDinoDrawTorpor = false;
    inline float DinoTorporColor[4] = { 0.95f, 0.5f, 0.95f, 1.0f };
    inline bool bDinoDrawRagdoll = false;
    inline float DinoRagdollColor[4] = { 1.0f, 0.0f, 0.25f, 0.75f };
    inline bool bDinoDrawDistance = false;
    inline float DinoDistanceColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline bool bDinoDistanceTextScale = true;
    inline float DinoNearTextScale = 1.25f;
    inline float DinoFarTextScale = 0.80f;
    inline float DinoTextScaleDistance = 300.0f;
    inline bool bDinoModelHighlight = false;
    inline bool bDinoModelOnly = false;
    inline float DinoModelHighlightMaxDistance = 300.0f;

    // 队友
    inline bool bESPTeamEnabled = false;

    inline bool bDrawBoxTeam = false;
    inline float BoxColorTeam[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
    inline bool bDrawHealthBarTeam = false;
    inline float TeamHealthColor1[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    inline float TeamHealthColor2[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

    inline bool bDrawNameTeam = false;
    inline float NameColorTeam[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
    inline bool bDrawSpeciesTeam = false;
    inline bool bDrawGrowthTeam = false;
    inline bool bDrawTorporTeam = false;
    inline float TorporColorTeam[4] = { 0.0f, 0.9f, 1.0f, 1.0f };
    inline bool bDrawRagdollTeam = false;
    inline float RagdollColorTeam[4] = { 1.0f, 0.0f, 0.25f, 0.75f };
    inline bool bDrawDistanceTeam = false;
    inline float DistanceColorTeam[4] = { 0.0f, 0.5f, 1.0f, 1.0f };

    // 队友 - 生物
    inline bool bTeamDinoDrawBox = false;
    inline float TeamDinoBoxColor[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
    inline bool bTeamDinoDrawHealthBar = false;
    inline bool bTeamDinoDrawName = false;
    inline float TeamDinoNameColor[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
    inline bool bTeamDinoDrawTorpor = false;
    inline float TeamDinoTorporColor[4] = { 0.0f, 0.9f, 1.0f, 1.0f };
    inline bool bTeamDinoDrawRagdoll = false;
    inline float TeamDinoRagdollColor[4] = { 1.0f, 0.0f, 0.25f, 0.75f };
    inline bool bTeamDinoDrawDistance = false;
    inline float TeamDinoDistanceColor[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
    inline bool bTeamDinoModelHighlight = false;
    inline bool bTeamDinoModelOnly = false;

    // OOF
    inline bool bEnableOOF = false;
    inline float OOFColor[4] = { 1.0f, 1.0f, 1.0f, 0.75f };
    inline float OOFRadius = 1.00f;
    inline float OOFSize = 12.0f;
    inline float OOFMaxDistance = 300.0f;
    inline float OOFBreathSpeed = 0.1f;
    inline float OOFMinAlpha = 0.25f;
    inline float OOFMaxAlpha = 1.0f;

    //自动换甲
    inline bool bAutomatic = false;
    inline float ArmorRange = 20.0f;
    // 参与自动换甲的部位（5 个身体部位 + 副手盾牌）
    inline bool bArmorSwapHead = true;
    inline bool bArmorSwapShirt = true;
    inline bool bArmorSwapPants = true;
    inline bool bArmorSwapBoots = true;
    inline bool bArmorSwapGloves = true;
    inline bool bArmorSwapShield = true;
    // 只替换已装备部位：空槽位不自动穿戴
    inline bool bArmorSwapOnlyEquipped = true;

    // The camera shortcut toggles this runtime state; activation is never persisted.
    inline bool bFreeCameraEnabled = false;
    inline float FreeCameraSpeedMeters = 15.0f;
}
