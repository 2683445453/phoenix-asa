#include "ConfigManager.h"
#include "Configs.h"
#include "../Menu/FeatureShortcuts.h"
#include <regex>
#include <unordered_set>

void ConfigManager::Initialize(const std::string& configDir) {
    if (configDir.empty()) return;

    try {
        fs::path p = fs::absolute(configDir);
        m_configDir = p.string();
        if (!fs::exists(m_configDir)) {
            fs::create_directories(m_configDir);
        }
    }
    catch (const fs::filesystem_error&) {
        return;
    }

    RefreshFileList();
}

void ConfigManager::RefreshFileList() {
    std::error_code ec;

    if (m_configDir.empty()) {
        m_configs.clear();
        return;
    }

    // 1. 组合为单条安全路径判断，不抛任何异常
    fs::path dirPath = fs::absolute(m_configDir, ec);
    if (ec || !fs::is_directory(dirPath, ec)) return;

    // 2. 用哈希集合记录磁盘上的绝对路径，查找复杂度从 O(N) 降为 O(1)
    std::unordered_set<fs::path> currentFiles;

    fs::directory_iterator dirIt(dirPath, ec);
    if (!ec) {
        for (const auto& entry : dirIt) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".ini") {
                fs::path fullPath = fs::absolute(entry.path(), ec);
                if (!ec) {
                    currentFiles.insert(std::move(fullPath));
                }
            }
        }
    }

    // 3. C++20 std::erase_if 极其优雅地剔除已被删除的文件
    std::erase_if(m_configs, [&](const ConfigFile& c) {
        return !currentFiles.contains(c.path);
        });

    // 4. 补充新增文件（由于已经在 m_configs 里的文件已经被留下了，只需过滤掉即可）
    for (const auto& filePath : currentFiles) {
        bool exists = std::any_of(m_configs.begin(), m_configs.end(), [&](const ConfigFile& c) {
            return c.path == filePath;
            });

        if (!exists) {
            m_configs.push_back(ConfigFile{
                .name = filePath.filename().string(),
                .path = filePath
                });
        }
    }
}

bool ConfigManager::IsValidConfigName(const std::string& name) {
    if (name.empty() || name.length() > 16) return false;
    std::regex validPattern("^[a-zA-Z0-9_-]+$");
    if (!std::regex_match(name, validPattern)) return false;
    if (name[0] == '.') return false;

    return true;
}

bool ConfigManager::CreateConfig(const std::string& name) {
    if (!IsValidConfigName(name)) return false;

    try {
        fs::path configPath = fs::path(m_configDir) / (name + ".ini");
        if (fs::exists(configPath)) return false;
        std::ofstream file(configPath);
        if (!file.is_open()) return false;

        file << "# Configuration File: " << name << "\n";
        file << "# Auto-generated\n\n";
        file.close();

        RefreshFileList();
        return true;
    }
    catch (...) {
        return false;
    }
}

bool ConfigManager::SaveConfig(const std::string& filename) {
    try {
        fs::path configPath = fs::path(m_configDir) / filename;
        std::ofstream file(configPath);
        if (!file.is_open()) return false;

        file << "# Configuration File\n";
        file << "# Format: key=value\n\n";

        // 菜单功能
        file << "[Menu]\n";
#define PHX_BOOL(Name, Default) CONFIG_BOOL(g_Config::Name);
#define PHX_FLOAT(Name, Default, Min, Max) CONFIG_FLOAT(g_Config::Name);
#define PHX_COLOR(Name, R, G, B, A) CONFIG_COLOR(g_Config::Name);
#include "FeatureExpansion.inc"
#undef PHX_BOOL
#undef PHX_FLOAT
#undef PHX_COLOR
        CONFIG_COLOR(g_Config::MenuColor);
		CONFIG_BOOL(g_Config::bMenuLockResize);
		CONFIG_INT(g_Config::MenuScaleIdx);
        file << "HoverHelpVersion=2\n";
        CONFIG_BOOL(g_Config::bShowFeatureHelp);
        CONFIG_BOOL(g_Config::bAutoDropResources);
        CONFIG_INT(g_Config::AutoDropResourceMask);
        CONFIG_BOOL(g_Config::bUnderwaterWeapons);
        CONFIG_BOOL(g_Config::bShowWelcome);
        CONFIG_BOOL(g_Config::bStaticThreatIndicators);
        WriteValue(file, "Shortcut.menu", PhoenixShortcuts::MenuKey);
        WriteValue(file, "Shortcut.stop", PhoenixShortcuts::StopKey);
        for (const auto& Action : PhoenixShortcuts::Actions)
            WriteValue(file, std::string("Shortcut.") + Action.Id, Action.Key);
        file << "\n";

        // 其他功能
        file << "[Misc]\n";
        CONFIG_FLOAT(g_Config::FreeCameraSpeedMeters);
        CONFIG_BOOL(g_Config::bAutoFeed);
        CONFIG_BOOL(g_Config::bAutoGeneScannerMarker);
        CONFIG_FLOAT(g_Config::AutoFeedRangeMeters);
        CONFIG_FLOAT(g_Config::AutoFeedIntervalSeconds);
        CONFIG_STRING(g_Config::AutoFeedDinoFilter, 256);
        CONFIG_BOOL(g_Config::bWeaponFireRate);
        CONFIG_FLOAT(g_Config::WeaponFireRateMultiplier);
        CONFIG_BOOL(g_Config::bFireRateTekRifle);
        CONFIG_BOOL(g_Config::bFireRateRifle);
        CONFIG_BOOL(g_Config::bFireRateShotgun);
        CONFIG_BOOL(g_Config::bFireRateSniper);
        CONFIG_BOOL(g_Config::bFireRatePistol);
        CONFIG_BOOL(g_Config::bFireRateBow);
        CONFIG_BOOL(g_Config::bFireRateOther);
        CONFIG_BOOL(g_Config::bCrossbowHoldRapidFire);
        CONFIG_BOOL(g_Config::bProjectilePrediction);
        CONFIG_FLOAT(g_Config::ProjectilePredictionMaxSeconds);
        CONFIG_FLOAT(g_Config::ProjectilePredictionDelayMs);
        CONFIG_BOOL(g_Config::bAutoLootContainers);
        CONFIG_BOOL(g_Config::bAutoLootSupplyCrates);
        CONFIG_BOOL(g_Config::bAutoLootBags);
        CONFIG_BOOL(g_Config::bAutoLootBeaverDams);
        CONFIG_BOOL(g_Config::bAutoLootWildEggs);
        CONFIG_FLOAT(g_Config::AutoLootRangeMeters);
        CONFIG_FLOAT(g_Config::AutoLootIntervalMs);
        CONFIG_FLOAT(g_Config::AutoLootMaxWeightPercent);
        CONFIG_BOOL(g_Config::bAutoPickupDroppedItems);
        CONFIG_FLOAT(g_Config::AutoPickupRangeMeters);
        CONFIG_FLOAT(g_Config::AutoPickupIntervalMs);
        CONFIG_FLOAT(g_Config::AutoPickupMaxWeightPercent);
        CONFIG_BOOL(g_Config::bAutoPickupUseDroppedItemFilter);
        CONFIG_BOOL(g_Config::bAutoOpenGachaCrystals);
        CONFIG_FLOAT(g_Config::AutoOpenGachaIntervalMs);
        CONFIG_BOOL(g_Config::bAutoPullGachaCrystals);
        CONFIG_FLOAT(g_Config::AutoPullGachaRangeMeters);
        CONFIG_BOOL(g_Config::bAutoDepositGachaGear);
        CONFIG_FLOAT(g_Config::AutoDepositGrinderRangeMeters);
        CONFIG_BOOL(g_Config::bAutoDepositFishingBlueprints);
        CONFIG_FLOAT(g_Config::AutoDepositVaultRangeMeters);
        CONFIG_BOOL(g_Config::bAutoFishingQTE);
        CONFIG_BOOL(g_Config::bAutoFishingRecast);
        CONFIG_BOOL(g_Config::bAutoEatDrink);
        CONFIG_FLOAT(g_Config::AutoEatDrinkThresholdPercent);
        CONFIG_BOOL(g_Config::bAutoMedicalBrew);
        CONFIG_FLOAT(g_Config::MedicalBrewThresholdPercent);
        CONFIG_BOOL(g_Config::bAutoEnergyBrew);
        CONFIG_FLOAT(g_Config::EnergyBrewThresholdPercent);
        CONFIG_BOOL(g_Config::bAutoBuffSoup);
        CONFIG_BOOL(g_Config::bSoupBattleTartare);
        CONFIG_BOOL(g_Config::bSoupCactusBroth);
        CONFIG_BOOL(g_Config::bSoupFocalChili);
        CONFIG_BOOL(g_Config::bSoupFriaCurry);
        CONFIG_BOOL(g_Config::bSoupCalienSoup);
        CONFIG_BOOL(g_Config::bSoupLazarusChowder);
        CONFIG_BOOL(g_Config::bSoupEnduroStew);
        CONFIG_BOOL(g_Config::bSoupShadowSteak);
        CONFIG_BOOL(g_Config::bSoupMushroomBrew);
        CONFIG_BOOL(g_Config::bAutoFireExtinguish);
        CONFIG_FLOAT(g_Config::FireExtinguishIntervalSeconds);
        CONFIG_BOOL(g_Config::bAutoSwitchFishingRod);
        CONFIG_BOOL(g_Config::bAutoRecoverFishingBait);
		CONFIG_BOOL(g_Config::bPumpShotgunNoReload);
        // 秒换弹与弓秒蓄力（见 Hack/InstantReload.inl）
        CONFIG_BOOL(g_Config::bInstantReload);
        CONFIG_BOOL(g_Config::bInstantReloadPumpShotgun);
        CONFIG_BOOL(g_Config::bInstantReloadLauncher);
        CONFIG_BOOL(g_Config::bInstantReloadRifle);
        CONFIG_BOOL(g_Config::bInstantReloadCrossbow);
        CONFIG_BOOL(g_Config::bInstantReloadOther);
        CONFIG_BOOL(g_Config::bInstantReloadProtectAmmo);
        CONFIG_FLOAT(g_Config::InstantReloadRemoteIntervalMs);
        CONFIG_BOOL(g_Config::bFastBowWood);
        CONFIG_BOOL(g_Config::bFastBowCompound);
        CONFIG_BOOL(g_Config::bFastBowTek);
        CONFIG_BOOL(g_Config::bFastBowForceShot);
        CONFIG_BOOL(g_Config::bInstantThrowLauncher);
        CONFIG_BOOL(g_Config::bAutoRepairItems);
        CONFIG_FLOAT(g_Config::AutoRepairThresholdPercent);
        CONFIG_FLOAT(g_Config::AutoRepairIntervalSeconds);
        CONFIG_BOOL(g_Config::bAutoRepairIncludeBackpack);
        CONFIG_BOOL(g_Config::bAutoCraft);
        CONFIG_FLOAT(g_Config::AutoCraftRangeMeters);
        CONFIG_FLOAT(g_Config::AutoCraftIntervalSeconds);
		CONFIG_BOOL(g_Config::bSuperFlyer);
        // 由于船只的角度会在开启此功能后在不经意间导致角度怪异，所以最好不要保存此配置
        // 2026/4/3 @zetsr
        // 考虑到跟一键自杀那种类型的功能不一样，保存到配置应该没有问题
        CONFIG_BOOL(g_Config::bForceTurn);

        CONFIG_BOOL(g_Config::bLogDamage);
        CONFIG_COLOR(g_Config::LogDamageColor);

        CONFIG_BOOL(g_Config::bAutomatic);
        CONFIG_FLOAT(g_Config::ArmorRange);
        CONFIG_BOOL(g_Config::bArmorSwapHead);
        CONFIG_BOOL(g_Config::bArmorSwapShirt);
        CONFIG_BOOL(g_Config::bArmorSwapPants);
        CONFIG_BOOL(g_Config::bArmorSwapBoots);
        CONFIG_BOOL(g_Config::bArmorSwapGloves);
        CONFIG_BOOL(g_Config::bArmorSwapShield);
        CONFIG_BOOL(g_Config::bArmorSwapOnlyEquipped);
        // Out-of-body is session-only. Never persist it across reconnects or restarts.

        CONFIG_BOOL(g_Config::bFastReconnected);
        CONFIG_BOOL(g_Config::bFastRespawned);
		CONFIG_BOOL(g_Config::bBlockedChat);

        CONFIG_BOOL(g_Config::bShowInfo);
        file << "\n";

        // 生物列表
        file << "[EntityList]\n";
        CONFIG_STRING(g_Config::entitySearchBuf, 256);
        CONFIG_BOOL(g_Config::bEnableFilter);
        file << "\n";

        // 建筑列表
        file << "[StructureList]\n";
        CONFIG_STRING(g_Config::structureSearchBuf, 256);
        CONFIG_BOOL(g_Config::bEnableStructureFilter);
        file << "\n";

        // 物品列表
        file << "[ItemList]\n";
        CONFIG_STRING(g_Config::itemSearchBuf, 256);
        file << "\n";

        // 自瞄
        file << "[Aimbot]\n";
        CONFIG_BOOL(g_Config::bAimbotEnabled);
        CONFIG_BOOL(g_Config::bAimPlayers);
        CONFIG_BOOL(g_Config::bAimWildDinos);
        CONFIG_BOOL(g_Config::bAimTamedDinos);
        CONFIG_BOOL(g_Config::bAimBosses);
        CONFIG_BOOL(g_Config::bSpearAimPlayers);
        CONFIG_BOOL(g_Config::bSpearAimCreatures);
        CONFIG_BOOL(g_Config::bSpearAimBosses);
        CONFIG_BOOL(g_Config::bAimHead);
        CONFIG_BOOL(g_Config::bAimBody);
        CONFIG_BOOL(g_Config::bAimRemoveSpread);
        CONFIG_BOOL(g_Config::bTekPistolHoldTarget);
        CONFIG_BOOL(g_Config::bShotgunConverge);
        CONFIG_BOOL(g_Config::bTekSpearAutoBlock);
        CONFIG_BOOL(g_Config::bTekSpearAutoDeflect);
        CONFIG_INT(g_Config::TekSpearTargetCount);
        CONFIG_BOOL(g_Config::bTekSpearAutoFace);
        CONFIG_FLOAT(g_Config::TekSpearTargetRangeMeters);
        CONFIG_BOOL(g_Config::bDrawTekSpearCircle);
        CONFIG_BOOL(g_Config::bDrawTekSpearTargetLine);
        CONFIG_BOOL(g_Config::bDrawTekSpearTargetInfo);
        CONFIG_COLOR(g_Config::TekSpearTargetColor);
        CONFIG_FLOAT(g_Config::TekSpearMeleeRangeMeters);
        CONFIG_FLOAT(g_Config::TekSpearParryLeadSeconds);
        CONFIG_FLOAT(g_Config::TekSpearParryTapSeconds);
        CONFIG_FLOAT(g_Config::TekSpearProjectileRangeMeters);
        CONFIG_FLOAT(g_Config::TekSpearLookAheadSeconds);
        CONFIG_FLOAT(g_Config::AimbotMaxDistanceMeters);
        CONFIG_FLOAT(g_Config::AimbotCircleRadiusPercent);
        CONFIG_BOOL(g_Config::bDrawAimCircle);
        CONFIG_BOOL(g_Config::bDrawAimTargetLine);
        CONFIG_BOOL(g_Config::bDrawAimTargetInfo);
        CONFIG_FLOAT(g_Config::AimTargetInfoScale);
        CONFIG_FLOAT(g_Config::AimCircleThickness);
        CONFIG_FLOAT(g_Config::AimTargetLineThickness);
        CONFIG_FLOAT(g_Config::AimTargetMarkerSize);
        CONFIG_COLOR(g_Config::AimCircleColor);
        CONFIG_COLOR(g_Config::AimTargetLineColor);
        file << "\n";

        // 扳机
        file << "[Triggerbot]\n";
        CONFIG_BOOL(g_Config::bTriggerbotEnabled);
        CONFIG_FLOAT(g_Config::TriggerDelay);
        CONFIG_FLOAT(g_Config::TriggerRandomPercent);
        CONFIG_FLOAT(g_Config::TriggerHitChance);
        file << "\n";

        // 掉落物
        file << "[DroppedItems]\n";
        CONFIG_BOOL(g_Config::bDrawDroppedItems);
        CONFIG_FLOAT(g_Config::DroppedItemMaxDistance);
        CONFIG_COLOR(g_Config::DroppedItemNameColor);
        CONFIG_COLOR(g_Config::DroppedItemDistanceColor);
        CONFIG_COLOR(g_Config::DroppedItemMeatColor);
        CONFIG_COLOR(g_Config::DroppedItemCryopodColor);
        CONFIG_COLOR(g_Config::DroppedItemEggColor);
        CONFIG_COLOR(g_Config::DroppedItemPiledColor);
        CONFIG_COLOR(g_Config::DroppedItemWoodColor);
        CONFIG_COLOR(g_Config::DroppedItemThatchColor);
        CONFIG_COLOR(g_Config::DroppedItemHideColor);
        CONFIG_COLOR(g_Config::DroppedItemPeltColor);
        CONFIG_COLOR(g_Config::DroppedItemKeratinColor);
        CONFIG_COLOR(g_Config::DroppedItemChitinColor);
        CONFIG_COLOR(g_Config::DroppedItemCorruptedPolymerColor);
        CONFIG_COLOR(g_Config::DroppedItemPolymer_OrganicColor);
        CONFIG_COLOR(g_Config::DroppedItemPolymerColor);
        CONFIG_COLOR(g_Config::DroppedItemMetalColor);
        CONFIG_COLOR(g_Config::DroppedItemStoneColor);
        CONFIG_COLOR(g_Config::DroppedItemCrystalColor);
        CONFIG_COLOR(g_Config::DroppedItemGemColor);
        CONFIG_COLOR(g_Config::DroppedItemPearlColor);
        CONFIG_COLOR(g_Config::DroppedItemSpoiledMeatColor);

        // 掉落物开关
        CONFIG_BOOL(g_Config::bDroppedItemMeat);
        CONFIG_BOOL(g_Config::bDroppedItemCryopod);
        CONFIG_BOOL(g_Config::bDroppedItemEgg);
        CONFIG_BOOL(g_Config::bDroppedItemPiled);
        CONFIG_BOOL(g_Config::bDroppedItemWood);
        CONFIG_BOOL(g_Config::bDroppedItemThatch);
        CONFIG_BOOL(g_Config::bDroppedItemHide);
        CONFIG_BOOL(g_Config::bDroppedItemPelt);
        CONFIG_BOOL(g_Config::bDroppedItemKeratin);
        CONFIG_BOOL(g_Config::bDroppedItemChitin);
        CONFIG_BOOL(g_Config::bDroppedItemCorruptedPolymer);
        CONFIG_BOOL(g_Config::bDroppedItemPolymer_Organic);
        CONFIG_BOOL(g_Config::bDroppedItemPolymer);
        CONFIG_BOOL(g_Config::bDroppedItemMetal);
        CONFIG_BOOL(g_Config::bDroppedItemStone);
        CONFIG_BOOL(g_Config::bDroppedItemCrystal);
        CONFIG_BOOL(g_Config::bDroppedItemGem);
        CONFIG_BOOL(g_Config::bDroppedItemPearl);
        CONFIG_BOOL(g_Config::bDroppedItemSpoiledMeat);
        file << "\n";

        // 宝箱
        file << "[SupplyDrops]\n";
        CONFIG_BOOL(g_Config::bDrawSupplyDrops);
        CONFIG_FLOAT(g_Config::SupplyDropMaxDistance);
        file << "\n";

        // 建筑
        file << "[Structures]\n";
        CONFIG_BOOL(g_Config::bDrawStructures);
        CONFIG_BOOL(g_Config::bStructureTypeFilter);
        CONFIG_BOOL(g_Config::bStructureAutoTurret);
        CONFIG_BOOL(g_Config::bStructureHeavyTurret);
        CONFIG_BOOL(g_Config::bStructureTekTurret);
        CONFIG_BOOL(g_Config::bStructureGenerator);
        CONFIG_BOOL(g_Config::bStructureTekGenerator);
        CONFIG_BOOL(g_Config::bStructureWindTurbine);
        CONFIG_BOOL(g_Config::bStructureSupplyDrop);
        CONFIG_BOOL(g_Config::bStructureLootCrate);
        CONFIG_BOOL(g_Config::bStructureBuriedTreasure);
        CONFIG_BOOL(g_Config::bStructureOther);
        CONFIG_BOOL(g_Config::bStructureShowLootContents);
        CONFIG_BOOL(g_Config::bStructureShowOwner);
        CONFIG_BOOL(g_Config::bOnlyDrawStructuresEnemy);
        CONFIG_FLOAT(g_Config::StructureMaxDistance);
        CONFIG_COLOR(g_Config::StructureNameColor);
        CONFIG_COLOR(g_Config::StructureOwnerColor);
        CONFIG_COLOR(g_Config::StructureDistanceColor);
        CONFIG_COLOR(g_Config::StructureHealthColor1);
        CONFIG_COLOR(g_Config::StructureHealthColor2);

        // 队友建筑
        file << "\n";

        // 水源
        file << "[Water]\n";
        CONFIG_BOOL(g_Config::bDrawWater);
		CONFIG_FLOAT(g_Config::WaterMaxCount);
        CONFIG_FLOAT(g_Config::WaterMaxDistance);
        CONFIG_COLOR(g_Config::WaterNameColor);
        CONFIG_COLOR(g_Config::WaterDistanceColor);
        file << "\n";

        // 附近资源点
        file << "[Resources]\n";
        CONFIG_FLOAT(g_Config::ModelHighlightIntensity);
        CONFIG_FLOAT(g_Config::ModelHighlightOpacity);
        CONFIG_BOOL(g_Config::bResourceESPEnabled);
        CONFIG_FLOAT(g_Config::ResourceESPMaxDistance);
        CONFIG_BOOL(g_Config::bResourceModelHighlight);
        CONFIG_BOOL(g_Config::bResourceShowLabels);
        CONFIG_BOOL(g_Config::bResourceMetal);
        CONFIG_BOOL(g_Config::bResourceCrystal);
        CONFIG_BOOL(g_Config::bResourceObsidian);
        CONFIG_BOOL(g_Config::bResourceOil);
        CONFIG_BOOL(g_Config::bResourcePearl);
        CONFIG_BOOL(g_Config::bResourceElementShard);
        CONFIG_BOOL(g_Config::bAutoHarvestResources);
        CONFIG_BOOL(g_Config::bDinoHarvestRange);
        CONFIG_FLOAT(g_Config::DinoHarvestRangeMultiplier);
        CONFIG_BOOL(g_Config::bAutoHarvestTekStrider);
        CONFIG_BOOL(g_Config::bAutoHarvestApproach);
        CONFIG_BOOL(g_Config::bAutoHarvestSprint);
        CONFIG_BOOL(g_Config::bAutoHarvestSafeNavigation);
        CONFIG_BOOL(g_Config::bAutoHarvestBreakObstacles);
        CONFIG_BOOL(g_Config::bAutoHarvestExperimentalNavigation);
        CONFIG_FLOAT(g_Config::AutoHarvestSearchRadiusMeters);
        CONFIG_FLOAT(g_Config::AutoHarvestAttackDistanceMeters);
        CONFIG_FLOAT(g_Config::AutoHarvestAttackIntervalMs);
        CONFIG_FLOAT(g_Config::AutoHarvestMaxSafeDropMeters);
        CONFIG_FLOAT(g_Config::AutoHarvestTekStriderBeamRangeMeters);
        CONFIG_COLOR(g_Config::ResourceMetalColor);
        CONFIG_COLOR(g_Config::ResourceCrystalColor);
        CONFIG_COLOR(g_Config::ResourceObsidianColor);
        CONFIG_COLOR(g_Config::ResourceOilColor);
        CONFIG_COLOR(g_Config::ResourcePearlColor);
        CONFIG_COLOR(g_Config::ResourceElementShardColor);
        file << "\n";

        // 全局
        file << "[Global]\n";
        file << "g_Config::ESPSettingsVersion=2\n";
        CONFIG_BOOL(g_Config::bModelHighlightEnabled);
        CONFIG_BOOL(g_Config::bESPPlayers);
        CONFIG_BOOL(g_Config::bESPCreatures);
        CONFIG_BOOL(g_Config::bShowCreatureAggro);
        CONFIG_FLOAT(g_Config::ModelHighlightMaxDistance);
        CONFIG_FLOAT(g_Config::ESPFontPixels);
        CONFIG_FLOAT(g_Config::ESPIconPixels);
        CONFIG_FLOAT(g_Config::ESPMinFontPixels);
        CONFIG_FLOAT(g_Config::ESPBoxPaddingPixels);
        CONFIG_FLOAT(g_Config::ESPBoxThicknessPixels);
        CONFIG_BOOL(g_Config::bESPCornerBox);
        CONFIG_BOOL(g_Config::bESPPreventOverlap);
        CONFIG_INT(g_Config::ESPScaleIdx);
        // CONFIG_FLOAT(g_Config::ESPScale);

        CONFIG_BOOL(g_Config::bDrawBox);
        CONFIG_COLOR(g_Config::BoxColor);
        CONFIG_BOOL(g_Config::bDrawHealthBar);
        CONFIG_BOOL(g_Config::bDrawName);
        CONFIG_COLOR(g_Config::NameColor);
        CONFIG_BOOL(g_Config::bDrawSpecies);
        CONFIG_BOOL(g_Config::bDrawGrowth);
        CONFIG_BOOL(g_Config::bDrawTorpor);
        CONFIG_COLOR(g_Config::TorporColor);
        CONFIG_BOOL(g_Config::bDrawRagdoll);
        CONFIG_COLOR(g_Config::RagdollColor);
        CONFIG_BOOL(g_Config::bDrawDistance);
        CONFIG_COLOR(g_Config::DistanceColor);
        CONFIG_BOOL(g_Config::bDrawAimPoints);
        CONFIG_COLOR(g_Config::AimPointsColor);
        CONFIG_BOOL(g_Config::bDrawAimSkeleton);
        CONFIG_COLOR(g_Config::AimSkeletonColor);
        CONFIG_COLOR(g_Config::HealthBarColor1);
        CONFIG_COLOR(g_Config::HealthBarColor2);
        CONFIG_COLOR(g_Config::BoxColorTeam);
        CONFIG_COLOR(g_Config::NameColorTeam);
        CONFIG_COLOR(g_Config::DistanceColorTeam);
        CONFIG_COLOR(g_Config::TorporColorTeam);
        CONFIG_COLOR(g_Config::RagdollColorTeam);
        CONFIG_COLOR(g_Config::TeamHealthColor1);
        CONFIG_COLOR(g_Config::TeamHealthColor2);
        CONFIG_COLOR(g_Config::TeamStructureHealthColor1);
        CONFIG_COLOR(g_Config::TeamStructureHealthColor2);

        CONFIG_BOOL(g_Config::bDinoDistanceTextScale);
        CONFIG_FLOAT(g_Config::DinoNearTextScale);
        CONFIG_FLOAT(g_Config::DinoFarTextScale);
        CONFIG_FLOAT(g_Config::DinoTextScaleDistance);

        // OOF
        file << "[OOF]\n";
        CONFIG_BOOL(g_Config::bEnableOOF);
        CONFIG_COLOR(g_Config::OOFColor);
        CONFIG_FLOAT(g_Config::OOFRadius);
        CONFIG_FLOAT(g_Config::OOFSize);
        CONFIG_FLOAT(g_Config::OOFMaxDistance);
        CONFIG_FLOAT(g_Config::OOFBreathSpeed);
        CONFIG_FLOAT(g_Config::OOFMinAlpha);
        CONFIG_FLOAT(g_Config::OOFMaxAlpha);
        file << "\n";

        file.close();
        return true;
    }
    catch (...) {
        return false;
    }
}

bool ConfigManager::LoadConfig(const std::string& filename) {
    try {
        fs::path configPath = fs::path(m_configDir) / filename;
        std::ifstream file(configPath);
        if (!file.is_open()) return false;

        std::unordered_map<std::string, std::string> data;
        std::string line;

        while (std::getline(file, line)) {
            line = Trim(line);

            if (line.empty() || line[0] == '#' || line[0] == '[') continue;
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue;

            std::string key = Trim(line.substr(0, pos));
            std::string value = Trim(line.substr(pos + 1));

            if (!key.empty()) {
                data[key] = value;
            }
        }

        file.close();

        // 菜单功能
#define PHX_BOOL(Name, Default) g_Config::Name = Default; LOAD_BOOL(g_Config::Name);
#define PHX_FLOAT(Name, Default, Min, Max) g_Config::Name = Default; LOAD_FLOAT(g_Config::Name); g_Config::Name = PhoenixInterface::FiniteClamp(g_Config::Name, Min, Max, Default);
#define PHX_COLOR(Name, R, G, B, A) { const float Defaults[4] = {R,G,B,A}; std::copy(Defaults, Defaults+4, g_Config::Name); LOAD_COLOR(g_Config::Name); for (int I=0; I<4; ++I) g_Config::Name[I] = PhoenixInterface::FiniteClamp(g_Config::Name[I], 0, 1, Defaults[I]); }
#include "FeatureExpansion.inc"
#undef PHX_BOOL
#undef PHX_FLOAT
#undef PHX_COLOR
        if(!data.contains("g_Config::bDeinoAimJump")) {
            if(const auto It=data.find("g_Config::bDeinoPitchVertical"); It!=data.end()) {
                ReadValue(It->second,g_Config::bDeinoAimJump);
                if(!data.contains("g_Config::DeinoAimJumpSpeed"))
                    g_Config::DeinoAimJumpSpeed=std::clamp(g_Config::DeinoAirHorizontalMultiplier*30.0f,1.0f,300.0f);
            }
        }
        // Old experiment keys are ignored; reset dormant runtime fields.
#define PHX_BOOL(Name, Default) g_Config::Name = Default;
#define PHX_FLOAT(Name, Default, Min, Max) g_Config::Name = Default;
#include "DeinoLegacyRuntime.inc"
#undef PHX_BOOL
#undef PHX_FLOAT
        LOAD_COLOR(g_Config::MenuColor);
		LOAD_BOOL(g_Config::bMenuLockResize);
		LOAD_INT(g_Config::MenuScaleIdx);
        g_Config::bShowFeatureHelp = true;
        if (data.contains("HoverHelpVersion")) { LOAD_BOOL(g_Config::bShowFeatureHelp); }
        g_Config::bAutoDropResources = g_Config::bUnderwaterWeapons = false;
        g_Config::AutoDropResourceMask = 0;
        LOAD_BOOL(g_Config::bAutoDropResources);
        LOAD_INT(g_Config::AutoDropResourceMask);
        LOAD_BOOL(g_Config::bUnderwaterWeapons);
        g_Config::AutoDropResourceMask = g_Config::AutoDropResourceMask < 0 ? 0 : (g_Config::AutoDropResourceMask & 2047);
        LOAD_BOOL(g_Config::bShowWelcome);
        LOAD_BOOL(g_Config::bStaticThreatIndicators);
        auto LoadKey = [&](const std::string& Name, unsigned& Key, unsigned Fallback) {
            Key = Fallback;
            if (auto It = data.find(Name); It != data.end()) {
                int Value = 0;
                if (ReadValue(It->second, Value) && Value >= 0) Key = static_cast<unsigned>(Value);
            }
        };
        LoadKey("Shortcut.menu", PhoenixShortcuts::MenuKey, PhoenixInterface::DefaultMenuKey);
        LoadKey("Shortcut.stop", PhoenixShortcuts::StopKey, PhoenixInterface::DefaultStopKey);
        for (auto& Action : PhoenixShortcuts::Actions)
            LoadKey(std::string("Shortcut.") + Action.Id, Action.Key,
                std::string(Action.Id) == "camera" ? 'Z' : 0);
        PhoenixShortcuts::Normalize();
        g_Config::MenuScaleIdx = std::clamp(g_Config::MenuScaleIdx, 0, 4);

        // 其他功能
        LOAD_FLOAT(g_Config::FreeCameraSpeedMeters);
        LOAD_BOOL(g_Config::bAutoFeed);
        LOAD_BOOL(g_Config::bAutoGeneScannerMarker);
        LOAD_FLOAT(g_Config::AutoFeedRangeMeters);
        LOAD_FLOAT(g_Config::AutoFeedIntervalSeconds);
        LOAD_STRING(g_Config::AutoFeedDinoFilter, 256);
        LOAD_BOOL(g_Config::bWeaponFireRate);
        LOAD_FLOAT(g_Config::WeaponFireRateMultiplier);
        LOAD_BOOL(g_Config::bFireRateTekRifle);
        LOAD_BOOL(g_Config::bFireRateRifle);
        LOAD_BOOL(g_Config::bFireRateShotgun);
        LOAD_BOOL(g_Config::bFireRateSniper);
        LOAD_BOOL(g_Config::bFireRatePistol);
        LOAD_BOOL(g_Config::bFireRateBow);
        LOAD_BOOL(g_Config::bFireRateOther);
        LOAD_BOOL(g_Config::bCrossbowHoldRapidFire);
        LOAD_BOOL(g_Config::bProjectilePrediction);
        LOAD_FLOAT(g_Config::ProjectilePredictionMaxSeconds);
        LOAD_FLOAT(g_Config::ProjectilePredictionDelayMs);
        LOAD_BOOL(g_Config::bAutoLootContainers);
        LOAD_BOOL(g_Config::bAutoLootSupplyCrates);
        LOAD_BOOL(g_Config::bAutoLootBags);
        g_Config::bAutoLootBeaverDams = false;
        LOAD_BOOL(g_Config::bAutoLootBeaverDams);
        LOAD_BOOL(g_Config::bAutoLootWildEggs);
        LOAD_FLOAT(g_Config::AutoLootRangeMeters);
        LOAD_FLOAT(g_Config::AutoLootIntervalMs);
        LOAD_FLOAT(g_Config::AutoLootMaxWeightPercent);
        LOAD_BOOL(g_Config::bAutoPickupDroppedItems);
        LOAD_FLOAT(g_Config::AutoPickupRangeMeters);
        LOAD_FLOAT(g_Config::AutoPickupIntervalMs);
        LOAD_FLOAT(g_Config::AutoPickupMaxWeightPercent);
        LOAD_BOOL(g_Config::bAutoPickupUseDroppedItemFilter);
        LOAD_BOOL(g_Config::bAutoOpenGachaCrystals);
        LOAD_FLOAT(g_Config::AutoOpenGachaIntervalMs);
        LOAD_BOOL(g_Config::bAutoPullGachaCrystals);
        LOAD_FLOAT(g_Config::AutoPullGachaRangeMeters);
        g_Config::bAutoDepositGachaGear = g_Config::bAutoDepositFishingBlueprints = false;
        g_Config::AutoDepositGrinderRangeMeters = g_Config::AutoDepositVaultRangeMeters = 12;
        LOAD_BOOL(g_Config::bAutoDepositGachaGear);
        LOAD_FLOAT(g_Config::AutoDepositGrinderRangeMeters);
        LOAD_BOOL(g_Config::bAutoDepositFishingBlueprints);
        LOAD_FLOAT(g_Config::AutoDepositVaultRangeMeters);
        g_Config::AutoDepositGrinderRangeMeters = PhoenixInterface::FiniteClamp(g_Config::AutoDepositGrinderRangeMeters, 2, 30, 12);
        g_Config::AutoDepositVaultRangeMeters = PhoenixInterface::FiniteClamp(g_Config::AutoDepositVaultRangeMeters, 2, 30, 12);
        LOAD_BOOL(g_Config::bAutoFishingQTE);
        LOAD_BOOL(g_Config::bAutoFishingRecast);
        LOAD_BOOL(g_Config::bAutoEatDrink);
        LOAD_FLOAT(g_Config::AutoEatDrinkThresholdPercent);
        LOAD_BOOL(g_Config::bAutoMedicalBrew);
        LOAD_FLOAT(g_Config::MedicalBrewThresholdPercent);
        LOAD_BOOL(g_Config::bAutoEnergyBrew);
        LOAD_FLOAT(g_Config::EnergyBrewThresholdPercent);
        LOAD_BOOL(g_Config::bAutoBuffSoup);
        LOAD_BOOL(g_Config::bSoupBattleTartare);
        LOAD_BOOL(g_Config::bSoupCactusBroth);
        LOAD_BOOL(g_Config::bSoupFocalChili);
        LOAD_BOOL(g_Config::bSoupFriaCurry);
        LOAD_BOOL(g_Config::bSoupCalienSoup);
        LOAD_BOOL(g_Config::bSoupLazarusChowder);
        LOAD_BOOL(g_Config::bSoupEnduroStew);
        LOAD_BOOL(g_Config::bSoupShadowSteak);
        LOAD_BOOL(g_Config::bSoupMushroomBrew);
        LOAD_BOOL(g_Config::bAutoFireExtinguish);
        LOAD_FLOAT(g_Config::FireExtinguishIntervalSeconds);
        g_Config::FireExtinguishIntervalSeconds = PhoenixInterface::FiniteClamp(g_Config::FireExtinguishIntervalSeconds, 2.0f, 60.0f, 5.0f);
        LOAD_BOOL(g_Config::bAutoSwitchFishingRod);
        LOAD_BOOL(g_Config::bAutoRecoverFishingBait);
		LOAD_BOOL(g_Config::bPumpShotgunNoReload);
        // 秒换弹与弓秒蓄力（见 Hack/InstantReload.inl）
        LOAD_BOOL(g_Config::bInstantReload);
        LOAD_BOOL(g_Config::bInstantReloadPumpShotgun);
        LOAD_BOOL(g_Config::bInstantReloadLauncher);
        LOAD_BOOL(g_Config::bInstantReloadRifle);
        LOAD_BOOL(g_Config::bInstantReloadCrossbow);
        LOAD_BOOL(g_Config::bInstantReloadOther);
        LOAD_BOOL(g_Config::bInstantReloadProtectAmmo);
        LOAD_FLOAT(g_Config::InstantReloadRemoteIntervalMs);
        g_Config::InstantReloadRemoteIntervalMs = PhoenixInterface::FiniteClamp(
            g_Config::InstantReloadRemoteIntervalMs, 120.0f, 2000.0f, 500.0f);
        LOAD_BOOL(g_Config::bFastBowWood);
        LOAD_BOOL(g_Config::bFastBowCompound);
        LOAD_BOOL(g_Config::bFastBowTek);
        LOAD_BOOL(g_Config::bFastBowForceShot);
        LOAD_BOOL(g_Config::bInstantThrowLauncher);
        LOAD_BOOL(g_Config::bAutoRepairItems);
        LOAD_FLOAT(g_Config::AutoRepairThresholdPercent);
        LOAD_FLOAT(g_Config::AutoRepairIntervalSeconds);
        LOAD_BOOL(g_Config::bAutoRepairIncludeBackpack);
        LOAD_BOOL(g_Config::bAutoCraft);
        LOAD_FLOAT(g_Config::AutoCraftRangeMeters);
        LOAD_FLOAT(g_Config::AutoCraftIntervalSeconds);
		LOAD_BOOL(g_Config::bSuperFlyer);
        // 由于船只的角度会在开启此功能后在不经意间导致角度怪异，所以最好不要保存此配置
        // 2026/4/3 @zetsr
        // 考虑到跟一键自杀那种类型的功能不一样，保存到配置应该没有问题
        LOAD_BOOL(g_Config::bForceTurn);

        LOAD_BOOL(g_Config::bLogDamage);
        LOAD_COLOR(g_Config::LogDamageColor);

        LOAD_BOOL(g_Config::bAutomatic);
        LOAD_FLOAT(g_Config::ArmorRange);
        // 旧版“独立自动换盾”迁移：盾牌并入自动换甲的部位筛选。
        // 旧配置只开盾时，只保留副手部位，避免突然自动穿戴其他护甲。
        if (g_Config::bAutoSwapShield) {
            g_Config::bArmorSwapShield = true;
            if (!g_Config::bAutomatic) {
                g_Config::bArmorSwapHead = g_Config::bArmorSwapShirt =
                g_Config::bArmorSwapPants = g_Config::bArmorSwapBoots =
                g_Config::bArmorSwapGloves = false;
            }
            g_Config::bAutomatic = true;
        }
        g_Config::bAutoSwapShield = false;
        LOAD_BOOL(g_Config::bArmorSwapHead);
        LOAD_BOOL(g_Config::bArmorSwapShirt);
        LOAD_BOOL(g_Config::bArmorSwapPants);
        LOAD_BOOL(g_Config::bArmorSwapBoots);
        LOAD_BOOL(g_Config::bArmorSwapGloves);
        LOAD_BOOL(g_Config::bArmorSwapShield);
        LOAD_BOOL(g_Config::bArmorSwapOnlyEquipped);
        g_Config::bFreeCameraEnabled = false;

        LOAD_BOOL(g_Config::bFastReconnected);
        LOAD_BOOL(g_Config::bFastRespawned);
		LOAD_BOOL(g_Config::bBlockedChat);
        LOAD_BOOL(g_Config::bShowInfo);

        // 生物列表
        LOAD_STRING(g_Config::entitySearchBuf, 256);
        LOAD_BOOL(g_Config::bEnableFilter);

        // 建筑列表
        LOAD_STRING(g_Config::structureSearchBuf, 256);
        LOAD_BOOL(g_Config::bEnableStructureFilter);

        // 物品列表
        LOAD_STRING(g_Config::itemSearchBuf, 256);

        // 自瞄
        LOAD_BOOL(g_Config::bAimbotEnabled);
        LOAD_BOOL(g_Config::bAimPlayers);
        LOAD_BOOL(g_Config::bAimWildDinos);
        LOAD_BOOL(g_Config::bAimTamedDinos);
        LOAD_BOOL(g_Config::bAimBosses);
        LOAD_BOOL(g_Config::bSpearAimPlayers);
        LOAD_BOOL(g_Config::bSpearAimCreatures);
        LOAD_BOOL(g_Config::bSpearAimBosses);
        LOAD_BOOL(g_Config::bAimHead);
        LOAD_BOOL(g_Config::bAimBody);
        LOAD_BOOL(g_Config::bAimRemoveSpread);
        LOAD_BOOL(g_Config::bTekPistolHoldTarget);
        LOAD_BOOL(g_Config::bShotgunConverge);
        LOAD_BOOL(g_Config::bTekSpearAutoBlock);
        LOAD_BOOL(g_Config::bTekSpearAutoDeflect);
        LOAD_INT(g_Config::TekSpearTargetCount);
        LOAD_BOOL(g_Config::bTekSpearAutoFace);
        LOAD_FLOAT(g_Config::TekSpearTargetRangeMeters);
        LOAD_BOOL(g_Config::bDrawTekSpearCircle);
        LOAD_BOOL(g_Config::bDrawTekSpearTargetLine);
        LOAD_BOOL(g_Config::bDrawTekSpearTargetInfo);
        LOAD_COLOR(g_Config::TekSpearTargetColor);
        LOAD_FLOAT(g_Config::TekSpearMeleeRangeMeters);
        LOAD_FLOAT(g_Config::TekSpearParryLeadSeconds);
        LOAD_FLOAT(g_Config::TekSpearParryTapSeconds);
        g_Config::TekSpearParryTapSeconds = PhoenixInterface::FiniteClamp(g_Config::TekSpearParryTapSeconds, .03f, PhoenixInterface::MaxSpearTapSeconds, .08f);
        LOAD_FLOAT(g_Config::TekSpearProjectileRangeMeters);
        LOAD_FLOAT(g_Config::TekSpearLookAheadSeconds);
        LOAD_FLOAT(g_Config::AimbotMaxDistanceMeters);
        LOAD_FLOAT(g_Config::AimbotCircleRadiusPercent);
        LOAD_BOOL(g_Config::bDrawAimCircle);
        LOAD_BOOL(g_Config::bDrawAimTargetLine);
        LOAD_BOOL(g_Config::bDrawAimTargetInfo);
        LOAD_FLOAT(g_Config::AimTargetInfoScale);
        LOAD_FLOAT(g_Config::AimCircleThickness);
        LOAD_FLOAT(g_Config::AimTargetLineThickness);
        LOAD_FLOAT(g_Config::AimTargetMarkerSize);
        LOAD_COLOR(g_Config::AimCircleColor);
        LOAD_COLOR(g_Config::AimTargetLineColor);

        // 扳机
        LOAD_BOOL(g_Config::bTriggerbotEnabled);
        LOAD_FLOAT(g_Config::TriggerDelay);
        LOAD_FLOAT(g_Config::TriggerRandomPercent);
        LOAD_FLOAT(g_Config::TriggerHitChance);

        // 掉落物
        LOAD_BOOL(g_Config::bDrawDroppedItems);
        LOAD_FLOAT(g_Config::DroppedItemMaxDistance);
        LOAD_COLOR(g_Config::DroppedItemNameColor);
        LOAD_COLOR(g_Config::DroppedItemDistanceColor);
        LOAD_COLOR(g_Config::DroppedItemMeatColor);
        LOAD_COLOR(g_Config::DroppedItemCryopodColor);
        LOAD_COLOR(g_Config::DroppedItemEggColor);
        LOAD_COLOR(g_Config::DroppedItemPiledColor);
        LOAD_COLOR(g_Config::DroppedItemWoodColor);
        LOAD_COLOR(g_Config::DroppedItemThatchColor);
        LOAD_COLOR(g_Config::DroppedItemHideColor);
        LOAD_COLOR(g_Config::DroppedItemPeltColor);
        LOAD_COLOR(g_Config::DroppedItemKeratinColor);
        LOAD_COLOR(g_Config::DroppedItemChitinColor);
        LOAD_COLOR(g_Config::DroppedItemCorruptedPolymerColor);
        LOAD_COLOR(g_Config::DroppedItemPolymer_OrganicColor);
        LOAD_COLOR(g_Config::DroppedItemPolymerColor);
        LOAD_COLOR(g_Config::DroppedItemMetalColor);
        LOAD_COLOR(g_Config::DroppedItemStoneColor);
        LOAD_COLOR(g_Config::DroppedItemCrystalColor);
        LOAD_COLOR(g_Config::DroppedItemGemColor);
        LOAD_COLOR(g_Config::DroppedItemPearlColor);
        LOAD_COLOR(g_Config::DroppedItemSpoiledMeatColor);

        // 掉落物开关
        LOAD_BOOL(g_Config::bDroppedItemMeat);
        LOAD_BOOL(g_Config::bDroppedItemCryopod);
        LOAD_BOOL(g_Config::bDroppedItemEgg);
        LOAD_BOOL(g_Config::bDroppedItemPiled);
        LOAD_BOOL(g_Config::bDroppedItemWood);
        LOAD_BOOL(g_Config::bDroppedItemThatch);
        LOAD_BOOL(g_Config::bDroppedItemHide);
        LOAD_BOOL(g_Config::bDroppedItemPelt);
        LOAD_BOOL(g_Config::bDroppedItemKeratin);
        LOAD_BOOL(g_Config::bDroppedItemChitin);
        LOAD_BOOL(g_Config::bDroppedItemCorruptedPolymer);
        LOAD_BOOL(g_Config::bDroppedItemPolymer_Organic);
        LOAD_BOOL(g_Config::bDroppedItemPolymer);
        LOAD_BOOL(g_Config::bDroppedItemMetal);
        LOAD_BOOL(g_Config::bDroppedItemStone);
        LOAD_BOOL(g_Config::bDroppedItemCrystal);
        LOAD_BOOL(g_Config::bDroppedItemGem);
        LOAD_BOOL(g_Config::bDroppedItemPearl);
        LOAD_BOOL(g_Config::bDroppedItemSpoiledMeat);

        // 宝箱
        LOAD_BOOL(g_Config::bDrawSupplyDrops);
        LOAD_FLOAT(g_Config::SupplyDropMaxDistance);

        // 建筑
        LOAD_BOOL(g_Config::bDrawStructures);
        // Loading a pre-filter config must not inherit a restrictive selection
        // from the config that was active immediately before it.
        g_Config::bStructureTypeFilter = false;
        g_Config::bStructureAutoTurret = g_Config::bStructureHeavyTurret = g_Config::bStructureTekTurret = true;
        g_Config::bStructureGenerator = g_Config::bStructureTekGenerator = g_Config::bStructureWindTurbine = true;
        g_Config::bStructureSupplyDrop = g_Config::bStructureLootCrate = g_Config::bStructureBuriedTreasure = true;
        g_Config::bStructureOther = false;
        g_Config::bStructureShowLootContents = false;
        g_Config::bStructureShowOwner = true;
        LOAD_BOOL(g_Config::bStructureTypeFilter);
        LOAD_BOOL(g_Config::bStructureAutoTurret);
        LOAD_BOOL(g_Config::bStructureHeavyTurret);
        LOAD_BOOL(g_Config::bStructureTekTurret);
        LOAD_BOOL(g_Config::bStructureGenerator);
        LOAD_BOOL(g_Config::bStructureTekGenerator);
        LOAD_BOOL(g_Config::bStructureWindTurbine);
        LOAD_BOOL(g_Config::bStructureSupplyDrop);
        LOAD_BOOL(g_Config::bStructureLootCrate);
        LOAD_BOOL(g_Config::bStructureBuriedTreasure);
        LOAD_BOOL(g_Config::bStructureOther);
        LOAD_BOOL(g_Config::bStructureShowLootContents);
        LOAD_BOOL(g_Config::bStructureShowOwner);
        LOAD_BOOL(g_Config::bOnlyDrawStructuresEnemy);
        LOAD_FLOAT(g_Config::StructureMaxDistance);
        LOAD_COLOR(g_Config::StructureNameColor);
        LOAD_COLOR(g_Config::StructureOwnerColor);
        LOAD_COLOR(g_Config::StructureDistanceColor);
        LOAD_COLOR(g_Config::StructureHealthColor1);
        LOAD_COLOR(g_Config::StructureHealthColor2);

        // 队友建筑
        LOAD_BOOL(g_Config::bTeamDrawStructures);
        LOAD_FLOAT(g_Config::TeamStructureMaxDistance);
        LOAD_COLOR(g_Config::TeamStructureHealthColor1);
        LOAD_COLOR(g_Config::TeamStructureHealthColor2);

        // 水源
        LOAD_BOOL(g_Config::bDrawWater);
		LOAD_FLOAT(g_Config::WaterMaxCount);
        LOAD_FLOAT(g_Config::WaterMaxDistance);
        LOAD_COLOR(g_Config::WaterNameColor);
        LOAD_COLOR(g_Config::WaterDistanceColor);

        // 附近资源点
        LOAD_FLOAT(g_Config::ModelHighlightIntensity);
        LOAD_FLOAT(g_Config::ModelHighlightOpacity);
        LOAD_BOOL(g_Config::bResourceESPEnabled);
        LOAD_FLOAT(g_Config::ResourceESPMaxDistance);
        LOAD_BOOL(g_Config::bResourceModelHighlight);
        LOAD_BOOL(g_Config::bResourceShowLabels);
        LOAD_BOOL(g_Config::bResourceMetal);
        LOAD_BOOL(g_Config::bResourceCrystal);
        LOAD_BOOL(g_Config::bResourceObsidian);
        LOAD_BOOL(g_Config::bResourceOil);
        LOAD_BOOL(g_Config::bResourcePearl);
        LOAD_BOOL(g_Config::bResourceElementShard);
        LOAD_BOOL(g_Config::bAutoHarvestResources);
        g_Config::bDinoHarvestRange = false;
        g_Config::DinoHarvestRangeMultiplier = 2.f;
        LOAD_BOOL(g_Config::bDinoHarvestRange);
        LOAD_FLOAT(g_Config::DinoHarvestRangeMultiplier);
        LOAD_BOOL(g_Config::bAutoHarvestTekStrider);
        // Preserve configs written before the Tek Strider mode was expanded
        // from Element Shards to every selected resource type.
        if (!data.contains("g_Config::bAutoHarvestTekStrider")) {
            const auto LegacyTekStriderMode =
                data.find("g_Config::bAutoHarvestTekStriderElementShard");
            if (LegacyTekStriderMode != data.end()) {
                ReadValue(LegacyTekStriderMode->second,
                    g_Config::bAutoHarvestTekStrider);
            }
        }
        LOAD_BOOL(g_Config::bAutoHarvestApproach);
        LOAD_BOOL(g_Config::bAutoHarvestSprint);
        LOAD_BOOL(g_Config::bAutoHarvestSafeNavigation);
        LOAD_BOOL(g_Config::bAutoHarvestBreakObstacles);
        LOAD_BOOL(g_Config::bAutoHarvestExperimentalNavigation);
        LOAD_FLOAT(g_Config::AutoHarvestSearchRadiusMeters);
        LOAD_FLOAT(g_Config::AutoHarvestAttackDistanceMeters);
        LOAD_FLOAT(g_Config::AutoHarvestAttackIntervalMs);
        LOAD_FLOAT(g_Config::AutoHarvestMaxSafeDropMeters);
        LOAD_FLOAT(g_Config::AutoHarvestTekStriderBeamRangeMeters);
        g_Config::AutoHarvestSearchRadiusMeters = PhoenixInterface::FiniteClamp(g_Config::AutoHarvestSearchRadiusMeters, 5.f, PhoenixInterface::MaxHarvestSearchMeters, 30.f);
        g_Config::AutoHarvestAttackDistanceMeters = PhoenixInterface::FiniteClamp(g_Config::AutoHarvestAttackDistanceMeters, 1.5f, PhoenixInterface::MaxHarvestApproachMeters, 4.5f);
        g_Config::AutoHarvestTekStriderBeamRangeMeters = PhoenixInterface::FiniteClamp(g_Config::AutoHarvestTekStriderBeamRangeMeters, 10.f, PhoenixInterface::MaxHarvestBeamMeters, 35.f);
        g_Config::AutoHarvestAttackIntervalMs = PhoenixInterface::FiniteClamp(g_Config::AutoHarvestAttackIntervalMs, 200.f, PhoenixInterface::MaxHarvestAttackIntervalMs, 850.f);
        g_Config::AutoHarvestMaxSafeDropMeters = PhoenixInterface::FiniteClamp(g_Config::AutoHarvestMaxSafeDropMeters, 0.5f, PhoenixInterface::MaxHarvestSafeDropMeters, 2.5f);
        g_Config::DinoHarvestRangeMultiplier = PhoenixInterface::FiniteClamp(g_Config::DinoHarvestRangeMultiplier, 1.f, PhoenixInterface::MaxDinoHarvestRangeMultiplier, 2.f);
        LOAD_COLOR(g_Config::ResourceMetalColor);
        LOAD_COLOR(g_Config::ResourceCrystalColor);
        LOAD_COLOR(g_Config::ResourceObsidianColor);
        LOAD_COLOR(g_Config::ResourceOilColor);
        LOAD_COLOR(g_Config::ResourcePearlColor);
        LOAD_COLOR(g_Config::ResourceElementShardColor);

        // 全局
        LOAD_BOOL(g_Config::bESPEnabled);
        LOAD_FLOAT(g_Config::ESPFontPixels);
        LOAD_FLOAT(g_Config::ESPIconPixels);
        LOAD_FLOAT(g_Config::ESPMinFontPixels);
        LOAD_FLOAT(g_Config::ESPBoxPaddingPixels);
        LOAD_FLOAT(g_Config::ESPBoxThicknessPixels);
        LOAD_BOOL(g_Config::bESPCornerBox);
        LOAD_BOOL(g_Config::bESPPreventOverlap);
        LOAD_INT(g_Config::ESPScaleIdx);
        // LOAD_FLOAT(g_Config::ESPScale);

        LOAD_BOOL(g_Config::bDrawBox);
        LOAD_COLOR(g_Config::BoxColor);
        LOAD_BOOL(g_Config::bDrawHealthBar);
        LOAD_BOOL(g_Config::bDrawName);
        LOAD_COLOR(g_Config::NameColor);
        LOAD_BOOL(g_Config::bDrawSpecies);
        LOAD_BOOL(g_Config::bDrawGrowth);
        LOAD_BOOL(g_Config::bDrawTorpor);
        LOAD_COLOR(g_Config::TorporColor);
        LOAD_BOOL(g_Config::bDrawRagdoll);
        LOAD_COLOR(g_Config::RagdollColor);
        LOAD_BOOL(g_Config::bDrawDistance);
        LOAD_COLOR(g_Config::DistanceColor);
        LOAD_BOOL(g_Config::bDrawAimPoints);
        LOAD_COLOR(g_Config::AimPointsColor);
        LOAD_BOOL(g_Config::bDrawAimSkeleton);
        LOAD_COLOR(g_Config::AimSkeletonColor);
        LOAD_COLOR(g_Config::HealthBarColor1);
        LOAD_COLOR(g_Config::HealthBarColor2);

        // 生物
        LOAD_BOOL(g_Config::bDinoDrawBox);
        LOAD_COLOR(g_Config::DinoBoxColor);
        LOAD_BOOL(g_Config::bDinoDrawHealthBar);
        LOAD_BOOL(g_Config::bDinoDrawName);
        LOAD_COLOR(g_Config::DinoNameColor);
        LOAD_BOOL(g_Config::bDinoDrawTorpor);
        LOAD_COLOR(g_Config::DinoTorporColor);
        LOAD_BOOL(g_Config::bDinoDrawRagdoll);
        LOAD_COLOR(g_Config::DinoRagdollColor);
        LOAD_BOOL(g_Config::bDinoDrawDistance);
        LOAD_COLOR(g_Config::DinoDistanceColor);
        LOAD_BOOL(g_Config::bDinoDistanceTextScale);
        LOAD_FLOAT(g_Config::DinoNearTextScale);
        LOAD_FLOAT(g_Config::DinoFarTextScale);
        LOAD_FLOAT(g_Config::DinoTextScaleDistance);
        LOAD_BOOL(g_Config::bDinoModelHighlight);
        LOAD_BOOL(g_Config::bDinoModelOnly);
        LOAD_FLOAT(g_Config::DinoModelHighlightMaxDistance);

        // 队友
        LOAD_BOOL(g_Config::bESPTeamEnabled);

        LOAD_BOOL(g_Config::bDrawBoxTeam);
        LOAD_COLOR(g_Config::BoxColorTeam);
        LOAD_BOOL(g_Config::bDrawHealthBarTeam);
        LOAD_BOOL(g_Config::bDrawNameTeam);
        LOAD_COLOR(g_Config::NameColorTeam);
        LOAD_BOOL(g_Config::bDrawSpeciesTeam);
        LOAD_BOOL(g_Config::bDrawGrowthTeam);
        LOAD_BOOL(g_Config::bDrawTorporTeam);
        LOAD_COLOR(g_Config::TorporColorTeam);
        LOAD_BOOL(g_Config::bDrawRagdollTeam);
        LOAD_COLOR(g_Config::RagdollColorTeam);
        LOAD_BOOL(g_Config::bDrawDistanceTeam);
        LOAD_COLOR(g_Config::DistanceColorTeam);
        LOAD_COLOR(g_Config::TeamHealthColor1);
        LOAD_COLOR(g_Config::TeamHealthColor2);

        // 队友生物
        LOAD_BOOL(g_Config::bTeamDinoDrawBox);
        LOAD_COLOR(g_Config::TeamDinoBoxColor);
        LOAD_BOOL(g_Config::bTeamDinoDrawHealthBar);
        LOAD_BOOL(g_Config::bTeamDinoDrawName);
        LOAD_COLOR(g_Config::TeamDinoNameColor);
        LOAD_BOOL(g_Config::bTeamDinoDrawTorpor);
        LOAD_COLOR(g_Config::TeamDinoTorporColor);
        LOAD_BOOL(g_Config::bTeamDinoDrawRagdoll);
        LOAD_COLOR(g_Config::TeamDinoRagdollColor);
        LOAD_BOOL(g_Config::bTeamDinoDrawDistance);
        LOAD_COLOR(g_Config::TeamDinoDistanceColor);
        LOAD_BOOL(g_Config::bTeamDinoModelHighlight);
        LOAD_BOOL(g_Config::bTeamDinoModelOnly);

        // Unified settings are authoritative. Legacy branch switches/colors are
        // read above only to migrate old files; new saves omit those keys.
        if (!data.contains("g_Config::ESPSettingsVersion") &&
            (data.contains("g_Config::bESPEnabled") || data.contains("g_Config::bESPTeamEnabled") ||
             data.contains("g_Config::bDinoModelHighlight") || data.contains("g_Config::bTeamDinoModelHighlight"))) {
            const auto OldBool = [&](const std::string& Key) {
                bool Value = false;
                if (const auto It = data.find("g_Config::" + Key); It != data.end()) ReadValue(It->second, Value);
                return Value;
            };
            g_Config::bModelHighlightEnabled = OldBool("bESPEnabled") || OldBool("bESPTeamEnabled") ||
                OldBool("bDinoModelHighlight") || OldBool("bTeamDinoModelHighlight");
            g_Config::bESPPlayers = g_Config::bESPCreatures = true;
            g_Config::ModelHighlightMaxDistance = g_Config::DinoModelHighlightMaxDistance;
            const auto MergeFeature = [&](bool& Enabled, float* EnemyColor, float* TeamColor, const char* Feature, const char* ColorName) {
                const std::string Keys[] = {"bDraw" + std::string(Feature), "bDinoDraw" + std::string(Feature),
                    "bDraw" + std::string(Feature) + "Team", "bTeamDinoDraw" + std::string(Feature)};
                const std::string Colors[] = {ColorName, "Dino" + std::string(ColorName),
                    std::string(ColorName) + "Team", "TeamDino" + std::string(ColorName)};
                Enabled = false;
                bool HasColor[2] = {false, false};
                for (int I = 0; I < 4; ++I) {
                    if (!OldBool(Keys[I])) continue;
                    const int Alliance = I < 2 ? 0 : 1;
                    float* Color = Alliance == 0 ? EnemyColor : TeamColor;
                    if (!HasColor[Alliance] && Color) {
                        const auto It = data.find("g_Config::" + Colors[I]);
                        if (It != data.end()) {
                            ReadColorArray(It->second, Color);
                            HasColor[Alliance] = true;
                        }
                    }
                    Enabled = true;
                }
            };
            MergeFeature(g_Config::bDrawBox, g_Config::BoxColor, g_Config::BoxColorTeam, "Box", "BoxColor");
            MergeFeature(g_Config::bDrawName, g_Config::NameColor, g_Config::NameColorTeam, "Name", "NameColor");
            MergeFeature(g_Config::bDrawDistance, g_Config::DistanceColor, g_Config::DistanceColorTeam, "Distance", "DistanceColor");
            MergeFeature(g_Config::bDrawTorpor, g_Config::TorporColor, g_Config::TorporColorTeam, "Torpor", "TorporColor");
            MergeFeature(g_Config::bDrawRagdoll, g_Config::RagdollColor, g_Config::RagdollColorTeam, "Ragdoll", "RagdollColor");
            MergeFeature(g_Config::bDrawHealthBar, nullptr, nullptr, "HealthBar", "");
            if (!OldBool("bDrawStructures") && OldBool("bTeamDrawStructures")) {
                g_Config::StructureMaxDistance = g_Config::TeamStructureMaxDistance;
            }
            g_Config::bDrawStructures = OldBool("bDrawStructures") || OldBool("bTeamDrawStructures");
        }
        LOAD_BOOL(g_Config::bModelHighlightEnabled);
        LOAD_BOOL(g_Config::bESPPlayers);
        LOAD_BOOL(g_Config::bESPCreatures);
        LOAD_BOOL(g_Config::bShowCreatureAggro);
        LOAD_FLOAT(g_Config::ModelHighlightMaxDistance);

        // OOF
        LOAD_BOOL(g_Config::bEnableOOF);
        LOAD_COLOR(g_Config::OOFColor);
        LOAD_FLOAT(g_Config::OOFRadius);
        LOAD_FLOAT(g_Config::OOFSize);
        LOAD_FLOAT(g_Config::OOFMaxDistance);
        LOAD_FLOAT(g_Config::OOFBreathSpeed);
        LOAD_FLOAT(g_Config::OOFMinAlpha);
        LOAD_FLOAT(g_Config::OOFMaxAlpha);
        using PhoenixInterface::FiniteClamp;
        g_Config::AimbotMaxDistanceMeters = FiniteClamp(g_Config::AimbotMaxDistanceMeters, 10, PhoenixInterface::MaxAimDistance, 300);
        g_Config::ESPFontPixels = FiniteClamp(g_Config::ESPFontPixels, 12, PhoenixInterface::MaxFontPixels, 16);
        g_Config::ESPIconPixels = FiniteClamp(g_Config::ESPIconPixels, 10, 40, 18);
        g_Config::ESPMinFontPixels = FiniteClamp(g_Config::ESPMinFontPixels, 12, g_Config::ESPFontPixels, 13);
        g_Config::FreeCameraSpeedMeters = FiniteClamp(g_Config::FreeCameraSpeedMeters, 1, PhoenixInterface::MaxCameraSpeed, 15);

        return true;
    }
    catch (...) {
        return false;
    }
}

template<typename T>
void ConfigManager::WriteValue(std::ofstream& file, const std::string& key, const T& value) {
    file << key << "=" << value << "\n";
}

void ConfigManager::WriteColorArray(std::ofstream& file, const std::string& key, const float* color) {
    file << key << "=" << color[0] << "," << color[1] << "," << color[2] << "," << color[3] << "\n";
}

void ConfigManager::WriteCharArray(std::ofstream& file, const std::string& key, const char* str, size_t maxLen) {
    std::string safe_str;
    for (size_t i = 0; i < maxLen && str[i] != '\0'; ++i) {
        char c = str[i];
        if (c == '\n') safe_str += "\\n";
        else if (c == '\r') safe_str += "\\r";
        else if (c == '\\') safe_str += "\\\\";
        else safe_str += c;
    }
    file << key << "=" << safe_str << "\n";
}

bool ConfigManager::ReadValue(const std::string& value, bool& out) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "true" || lower == "1") {
        out = true;
        return true;
    }
    else if (lower == "false" || lower == "0") {
        out = false;
        return true;
    }
    return false;
}

bool ConfigManager::ReadValue(const std::string& value, float& out) {
    try {
        out = std::stof(value);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool ConfigManager::ReadValue(const std::string& value, int& out) {
    try {
        out = std::stoi(value);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool ConfigManager::ReadValue(const std::string& value, char* out, size_t maxLen) {
    if (value.length() >= maxLen) return false;

    size_t outIdx = 0;
    for (size_t i = 0; i < value.length() && outIdx < maxLen - 1; ++i) {
        if (value[i] == '\\' && i + 1 < value.length()) {
            if (value[i + 1] == 'n') {
                out[outIdx++] = '\n';
                ++i;
            }
            else if (value[i + 1] == 'r') {
                out[outIdx++] = '\r';
                ++i;
            }
            else if (value[i + 1] == '\\') {
                out[outIdx++] = '\\';
                ++i;
            }
            else {
                out[outIdx++] = value[i];
            }
        }
        else {
            out[outIdx++] = value[i];
        }
    }
    out[outIdx] = '\0';
    return true;
}

bool ConfigManager::ReadColorArray(const std::string& value, float* color) {
    try {
        std::stringstream ss(value);
        std::string token;
        int idx = 0;

        while (std::getline(ss, token, ',') && idx < 4) {
            color[idx++] = std::stof(Trim(token));
        }

        return idx == 4;
    }
    catch (...) {
        return false;
    }
}

std::string ConfigManager::Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}
