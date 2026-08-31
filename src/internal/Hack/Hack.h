#pragma once

#include <string>
#include <vector>

namespace g_Hack {
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

    struct SkillCooldownView {
        std::string SkillName;
        float RemainingSeconds = 0.0f;
        float DurationSeconds = 0.0f;
        bool MatchedLostColony = false;
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
    void UnlockExplorerNotes(SDK::UWorld* World);
    void AutoFeed(SDK::UWorld* World);
    std::string GetAutoFeedStatus();
    void RequestEnableNearbyDaeodonHealing();
    void ProcessEnableNearbyDaeodonHealingRequest(SDK::UWorld* World);
    std::string GetDaeodonHealingStatus();
    void AutoOpenGachaCrystals(SDK::UWorld* World);
    std::string GetAutoPullGachaStatus();
    void AutoFishingQTE(SDK::UWorld* World);
    std::string GetFishingQTEStatus();
    void AutoEatDrink(SDK::UWorld* World);
    std::string GetAutoEatDrinkStatus();
    void AutoUseBrews(SDK::UWorld* World);
    std::string GetAutoBrewStatus();
    void MaintainFishingRod(SDK::UWorld* World);
    std::string GetFishingRodMaintenanceStatus();
    void TekChestJetNoCooldown(SDK::UWorld* World);
    std::string GetTekChestJetStatus();
    void TekGloveDashNoCooldown(SDK::UWorld* World);
    std::string GetTekGloveDashStatus();
    void PumpShotgunNoReload(SDK::UWorld* World);
    void AutoCraft(SDK::UWorld* World);
    std::vector<AutoCraftStationView> GetAutoCraftCatalog(SDK::UWorld* World);
    void SelectAutoCraftStation(int InventoryObjectIndex);
    void SelectAutoCraftRecipe(int ItemObjectIndex);
    int GetSelectedAutoCraftStation();
    int GetSelectedAutoCraftRecipe();
    std::string GetAutoCraftStatus();
    void AutoImprint(SDK::UWorld* World);
    std::string GetAutoImprintStatus();
    void AutoGrindHelicoprionInventory(SDK::UWorld* World);
    std::string GetAutoGrindHelicoprionStatus();
    void ExpandPlayerInventory(SDK::UWorld* World);
    void DinoAttackSpeed(SDK::UWorld* World);
    void DragontopiaSkillTuning(SDK::UWorld* World);
    DlcSkillDataView GetDlcSkillDataView();
    std::vector<SkillCooldownView> GetLostColonyCooldowns(SDK::UWorld* World);
    void RequestSkillModifierDiagnostics();
    void ProcessSkillModifierDiagnostics(SDK::UWorld* World);
    SkillModifierDiagnosticsView GetSkillModifierDiagnostics();
    std::string BuildSkillModifierDiagnosticsReport();
    void TastyBaitSkillOverride(SDK::UWorld* World);
    std::string GetTastyBaitOverrideStatus();
    void AutoHarvestResources(SDK::UWorld* World);
    std::string GetAutoHarvestStatus();
    void SuperFlyer(SDK::UWorld* World);
    void ForceTurn(SDK::UMovementComponent* rcx, float DeltaTime);
    void DamageLog(SDK::AActor* _this, float DamageAmount, SDK::FDamageEvent* DamageEvent, SDK::AController* Instigator, SDK::AActor* DamageCauser);
    void AutoSwapBrokenEquipment();
    void RequestTekStriderRigChange(int HeadRigIndex, int BodyRigIndex);
    void ProcessTekStriderRigRequest(SDK::UWorld* World);
    std::string GetTekStriderRigStatus();
    void OutBody();
}
