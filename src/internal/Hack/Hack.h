#pragma once

#include <string>
#include <vector>

namespace g_Hack {
    using NativeGameplayEvent = void(__fastcall*)(SDK::UObject*, SDK::UFunction*, void*);
    bool HandlePlacementExperiment(SDK::UObject* Object, SDK::UFunction* Function, void* Args, NativeGameplayEvent Original);
    void PlacementExperimentTick(SDK::UWorld* World);
    void ResetPlacementExperiment();
    std::string GetFloatingStructureStatus();
    bool SuppressTekHeatEvent(SDK::UObject* Object, SDK::UFunction* Function);
    void FeatureExpansionTick(SDK::UWorld* World);
    void MaintainMountedFullRotationCamera(SDK::AShooterPlayerCameraManager* Camera);
    void ResetFeatureExpansion();
    void ResetInstantThrowables();
    // 秒换弹（Hack/InstantReload.inl）
    void ResetInstantReload();
    bool ShouldForceFullChargeShot(SDK::AShooterWeapon* Weapon);
    std::string GetInstantReloadStatus();
    std::string GetInstantChargeStatus();
    std::string GetTekLauncherSlotStatus();
    bool IsHusbandryInventoryActive();
    std::string GetExpansionCombatStatus();
    std::string GetGargantarClientStatus();
    std::string GetNativeMountStatus();
    void TraceDeinoActionEvent(SDK::UObject* Object,SDK::UFunction* Function,void* Parameters,bool After);
    bool SuppressDeinoFlightInput(SDK::UObject* Object,SDK::UFunction* Function);
    std::string GetDeinoFlightStatus();
    std::string GetDeinoHoverFacingStatus();
    std::string GetDeinoAirLatchStatus();
    std::string GetDeinoGlideProbeStatus();
    std::string GetDeinoGlideProbePath();
    void RequestSpiderZiplineFlip(bool DirectServerEvent);
    void RequestSpiderZiplineLocalEvent();
    void SpiderZiplineExperimentTick(SDK::UWorld* World);
    void ResetSpiderZiplineExperiment();
    std::string GetSpiderZiplineExperimentStatus();
    void NativeMountPhysicsHook(SDK::UMovementComponent* Movement);
    std::string GetHusbandryStatus();
    void RequestAllocateLevel(int Stat);
    bool CanAllocateLevel();
    void GameplayExtensionsTick(SDK::UWorld* World);
    void ResetGameplayExtensions();
    std::string GetWeaponFireRateStatus();
    std::string GetAutoLootStatus();
    struct SkillModifierView {
        std::string ModifierName;
        int Rank = 0;
        float ConfiguredValue = 0.0f;
        float EffectiveValue = 0.0f;
        int StackingType = -1;
        bool HasEffectiveValue = false;
    };

    struct SkillDataEntryView {
        std::string InternalName;
        std::string DisplayName;
        std::string Description;
        int UnlockedRank = 0;
        int TotalRanks = 0;
        int GrantedBuffRanks = 0;
        int CooldownRanks = 0;
        bool IsActiveSkill = false;
        bool IsBaitFragranceCandidate = false;
        std::vector<SkillModifierView> Modifiers;
    };

    struct SkillModifierDiagnosticsView {
        std::string Status = "尚未扫描";
        std::string ProviderName;
        int UnlockedSkillCount = 0;
        int SkillsWithData = 0;
        int SkillsWithModifiers = 0;
        int BaitFragranceCandidates = 0;
        bool Pending = false;
        bool LayoutCompatible = false;
        bool HasLocalAuthority = false;
        std::vector<SkillDataEntryView> Skills;
    };

    struct DlcSkillDataView {
        int DragontopiaTrees = 0;
        int DragontopiaTables = 0;
        int DragontopiaRows = 0;
        int DragontopiaDamageModifiers = 0;
        int DragontopiaProjectileDamageModifiers = 0;
        int DragontopiaModifiedValues = 0;
        int LostColonyTrees = 0;
        int LostColonyTables = 0;
        int LostColonyRows = 0;
        bool HasLocalAuthority = false;
        std::vector<std::string> DragontopiaDamageDetails;
    };

    struct AutoCraftRecipeView {
        int ItemObjectIndex = -1;
        std::string Name;
        bool IsBlueprint = false;
    };

    struct AutoCraftStationView {
        int StationObjectIndex = -1;
        int InventoryObjectIndex = -1;
        std::string Name;
        float DistanceMeters = 0.0f;
        std::vector<AutoCraftRecipeView> Recipes;
    };

    void DumpServerInfo();
    void Suicide(SDK::UWorld* World);
    void RequestNoteXPBuff();
    void CancelNoteXPBuff();
    std::string GetNoteXPBuffStatus();
    void RequestOpenUploadTerminal();
    std::string GetUploadTerminalStatus();
    bool IsUploadTerminalActive();
    void TickNativeFreeCamera();
    void TekEquipmentTick(SDK::UWorld* World);
    void ResetTekEquipment();
    std::string GetTekEquipmentStatus();
    void WorldUtilitiesTick(SDK::UWorld* World);
    void ResetWorldUtilities();
    void AutoFeed(SDK::UWorld* World);
    std::string GetAutoFeedStatus();
    void RequestEnableNearbyDaeodonHealing();
    void ProcessEnableNearbyDaeodonHealingRequest(SDK::UWorld* World);
    std::string GetDaeodonHealingStatus();
    void AutoOpenGachaCrystals(SDK::UWorld* World);
    std::string GetAutoPullGachaStatus();
    void BackpackToolsTick(SDK::UWorld* World);
    void ResetBackpackTools();
    void UpdateSessionSnapshot(SDK::UWorld* World);
    std::string GetAutoDropStatus();
    std::string GetUnderwaterWeaponStatus();
    void AutoDepositItems(SDK::UWorld* World);
    void ResetAutoDeposit();
    bool IsAutomaticDepositActive();
    std::string GetGrinderDepositStatus();
    std::string GetBlueprintDepositStatus();
    void AutoFishingQTE(SDK::UWorld* World);
    std::string GetFishingQTEStatus();
    void AutoEatDrink(SDK::UWorld* World);
    std::string GetAutoEatDrinkStatus();
    void AutoUseBrews(SDK::UWorld* World);
    std::string GetAutoBrewStatus();
    void AutoBuffSoup(SDK::UWorld* World);
    std::string GetAutoBuffSoupStatus();
    void AutoFireExtinguish(SDK::UWorld* World);
    std::string GetFireExtinguishStatus();
    void AutoPickupDroppedItems(SDK::UWorld* World);
    std::string GetAutoPickupStatus();
    void AutoRepairItems(SDK::UWorld* World);
    std::string GetAutoRepairStatus();
    void MaintainFishingRod(SDK::UWorld* World);
    std::string GetFishingRodMaintenanceStatus();
    void PumpShotgunNoReload(SDK::UWorld* World);
    void AutoCraft(SDK::UWorld* World);
    std::vector<AutoCraftStationView> GetAutoCraftCatalog(SDK::UWorld* World);
    void SelectAutoCraftStation(int InventoryObjectIndex);
    void SelectAutoCraftRecipe(int ItemObjectIndex);
    int GetSelectedAutoCraftStation();
    int GetSelectedAutoCraftRecipe();
    std::string GetAutoCraftStatus();
    void DinoAttackSpeed(SDK::UWorld* World);
    void DragontopiaSkillTuning(SDK::UWorld* World);
    DlcSkillDataView GetDlcSkillDataView();
    void RequestSkillModifierDiagnostics();
    void ProcessSkillModifierDiagnostics(SDK::UWorld* World);
    SkillModifierDiagnosticsView GetSkillModifierDiagnostics();
    std::string BuildSkillModifierDiagnosticsReport();
    void TastyBaitSkillOverride(SDK::UWorld* World);
    std::string GetTastyBaitOverrideStatus();
    void AutoHarvestResources(SDK::UWorld* World);
    void MaintainAutoHarvestCameraAim(
        SDK::AShooterPlayerCameraManager* CameraManager);
    bool ResetAutoHarvestForUnload();
    std::string GetAutoHarvestStatus();
    void SuperFlyer(SDK::UWorld* World);
    void ForceTurn(SDK::UMovementComponent* rcx, float DeltaTime);
    void DamageLog(SDK::AActor* _this, float DamageAmount, SDK::FDamageEvent* DamageEvent, SDK::AController* Instigator, SDK::AActor* DamageCauser);
    void AutoSwapBrokenEquipment();
    std::string GetArmorSwapStatus();
    bool HandleFreeCameraKey(unsigned int Key, bool Pressed, bool Repeated);
    bool IsFreeCameraActive();
    bool IsFreeCameraBuilding();
    bool GetFreeCameraBuildView(SDK::UObject* Caller, SDK::APrimalCharacter* Character,
        SDK::FVector& Location, SDK::FVector& Direction);
    std::string GetFreeCameraStatus();
    void PrepareFreeCameraUpdate(SDK::AShooterPlayerCameraManager* Camera);
    void UpdateFreeCamera(SDK::AShooterPlayerCameraManager* Camera, float DeltaTime);
    void ResetFreeCamera();
    void UpdateDinoHarvestRange(SDK::APrimalDinoCharacter* Dino, bool Enabled, float Multiplier, uint64_t NowMs);
    void ResetDinoHarvestRange();
    bool GetDinoHarvestReachMeters(SDK::APrimalDinoCharacter* Dino, float& OutMeters);
    std::string GetDinoHarvestRangeStatus();
}
