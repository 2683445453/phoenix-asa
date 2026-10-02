#include "../src/internal/Legit/Aimbot/ProjectilePrediction.h"
#include "../src/internal/Legit/Aimbot/TekBowRules.h"
#include "../src/internal/ESP/ESPLayout.h"
#include "../src/internal/Config/Configs.h"
#include "../src/internal/Hack/FireRateRules.h"
#include "../src/internal/Hack/AutoLootRules.h"
#include "../src/internal/Hack/CameraControls.h"
#include "../src/internal/Hack/WorldUtilityRules.h"
#include "../src/internal/Hack/GachaOpenScheduler.h"
#include "../src/internal/ESP/StructureESP.h"
#include "../src/internal/Legit/Aimbot/AimSelection.h"
#include "../src/internal/Config/ConfigManager.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>

using namespace g_Aimbot;
static void CheckImpact(BallisticVector Origin, BallisticVector Target, BallisticVector Velocity,
    double Speed, double Gravity, double Horizon, double Delay = 0) {
    const auto Result = PredictProjectile(Origin, Target, Velocity, Speed, Gravity, Horizon, Delay);
    assert(Result && Result->Time > 0 && Result->Time <= Horizon);
    const auto Actual = Origin + Result->Direction * (Speed * Result->Time) +
        BallisticVector{0, 0, Gravity * Result->Time * Result->Time * 0.5};
    assert((Actual - (Target + Velocity * (Delay + Result->Time))).Length() < 0.001);
    assert(std::abs(Result->Direction.Length() - 1) < 1e-10);
}

int main() {
    g_Hack::GachaOpenScheduler Crystals;
    std::vector<g_Hack::GachaStack> CrystalBag;
    for (uint64_t ID = 1; ID <= 100; ++ID) CrystalBag.push_back({ID, 1});
    Crystals.Observe(CrystalBag, 1000);
    const auto FirstCrystals = Crystals.Plan(1000, 50);
    assert(FirstCrystals.size() == 32 && Crystals.Pending() == 32);
    assert(Crystals.Plan(1049, 50).empty());
    Crystals.Observe(CrystalBag, 1050); // Replication lag must not resend the first crystal.
    const auto SecondCrystals = Crystals.Plan(1050, 50);
    assert(SecondCrystals.size() == 32 && Crystals.Pending() == 64);
    for (auto ID : SecondCrystals) assert(std::find(FirstCrystals.begin(), FirstCrystals.end(), ID) == FirstCrystals.end());
    assert(Crystals.Plan(1100, 50).empty()); // Bounded in-flight requests.
    CrystalBag.erase(CrystalBag.begin(), CrystalBag.begin() + 64);
    Crystals.Observe(CrystalBag, 1150);
    assert(Crystals.Remaining() == 36 && Crystals.Pending() == 0);
    const auto ThirdCrystals = Crystals.Plan(1150, 50);
    assert(ThirdCrystals.size() == 32);
    std::erase_if(CrystalBag, [&](const auto& Stack) {
        return std::find(ThirdCrystals.begin(), ThirdCrystals.end(), Stack.ID) != ThirdCrystals.end();
    });
    Crystals.Observe(CrystalBag, 1200);
    assert(Crystals.Plan(1200, 50).size() == 4);
    Crystals.Observe({}, 1250);
    assert(Crystals.Remaining() == 0 && Crystals.Pending() == 0 && Crystals.Plan(1250, 50).empty());
    Crystals.Observe({{123, 10}, {123, 10}, {124, -1}}, 2000);
    assert(Crystals.Plan(2000, 50).size() == 10 && Crystals.Remaining() == 10);
    Crystals.Observe({{123, 6}}, 2100);
    assert(Crystals.Pending() == 6 && Crystals.Plan(2100, 50).empty());
    Crystals.Observe({{123, 6}, {999, 1}}, 4100);
    const auto RetryCrystals = Crystals.Plan(4100, 50);
    assert(RetryCrystals.size() == 2); // One probe for a stalled stack; new stacks still proceed.
    Crystals.Unsent(999);
    assert(Crystals.Pending() == 1); // A vanished/replaced item did not generate a request.
    Crystals.Observe({{123, 5}}, 4200);
    assert(Crystals.Plan(4200, 50).size() == 5); // Progress restores batching for the remainder.
    Crystals.Reset();
    assert(Crystals.Pending() == 0 && Crystals.Remaining() == 0); // Death, world or inventory change.
    Crystals.Observe({{1, 1}}, 5000);
    assert(Crystals.Plan(5000, NAN).size() == 1);
    Crystals.Observe({{2, 1}}, 5001);
    assert(Crystals.Plan(5049, NAN).empty() && Crystals.Plan(5050, NAN).size() == 1);
    assert(g_Hack::IsObeliskTerminalIdentity("TributeTerminal_Blue_C PrimalInventoryBP_TributeTerminal_C"));
    assert(g_Hack::IsObeliskTerminalIdentity("红色方尖碑"));
    assert(!g_Hack::IsObeliskTerminalIdentity("TekTransmitter_C PrimalInventoryBP_TributeTerminal_C"));
    assert(!g_Hack::IsObeliskTerminalIdentity("SupplyCrate_Level60_C TributeTerminal"));
    assert(!g_Hack::IsObeliskTerminalIdentity("CityTerminal_C TributeTerminal"));
    assert(!g_Hack::IsObeliskTerminalIdentity("StorageBox_C"));
    assert(g_Hack::RemoteViewRange(200000) > 200000);
    assert(g_Hack::RemoteViewRange(NAN) == 0 && g_Hack::RemoteViewRange(-1) == 0);
    g_Hack::RemotePanelSession Panel;
    Panel.Open(1000);
    assert(Panel.Keep(1001, false, false, false, false)); // Native registration is asynchronous.
    assert(Panel.Keep(2900, false, false, false, false));
    assert(Panel.Keep(2950, true, true, false, false));
    assert(Panel.Keep(10000, true, true, false, false)); // No arbitrary timeout after attachment.
    assert(!Panel.Keep(10001, false, false, false, false)); // User closed the owned UI.
    Panel.Open(20000);
    assert(!Panel.Keep(22000, false, false, false, false)); // Failed registration is released.
    assert(!Panel.Keep(20001, false, true, false, true)); // Another UI must never be retained/closed.
    assert(!Panel.Keep(20001, true, true, true, false)); // Native close is respected, never reopened.
    assert(!g_Hack::CameraControls::ConsumeFlightKey('1', true, false)); // Hotbar starts native construction.
    assert(g_Hack::CameraControls::ConsumeFlightKey('W', true, true)); // Construction cannot move the real pawn.
    assert(!g_Hack::CameraControls::ConsumeFlightKey('W', false, true));
    assert(!g_Hack::CameraControls::ConsumeFlightKey('E', true, true)); // Native build controls reach the game.
    assert(g_Hack::CameraControls::ConsumeFlightKey('E', true, false));
    g_Hack::CameraControls::DoubleTap Tap;
    assert(!Tap.Press(1000, false));
    assert(!Tap.Press(1010, true));
    assert(!Tap.Press(1020, false)); // Held key is not a second tap.
    Tap.Release();
    assert(Tap.Press(1200, false));
    Tap.Release();
    assert(!Tap.Press(1250, false)); // Third tap starts a new pair.
    Tap.Release();
    assert(!Tap.Press(2000, false)); // Expired pair never toggles.
    Tap.Release();
    assert(Tap.Press(2100, false));
    Tap.Reset();
    assert(!Tap.Press(2200, false)); // Focus/character changes clear the pair.
    Tap.Release();
    assert(!Tap.Press(2000, false)); // A backwards clock does not toggle.
    const auto ForwardStep = g_Hack::CameraControls::Step(0, 0, 1, 0, 0, 10, 0.1, false);
    assert(std::abs(ForwardStep.X - 100) < 1e-6 && ForwardStep.Y == 0 && ForwardStep.Z == 0);
    const auto RightStep = g_Hack::CameraControls::Step(90, 0, 1, 0, 0, 10, 0.1, false);
    assert(std::abs(RightStep.X) < 1e-6 && std::abs(RightStep.Y - 100) < 1e-6);
    const auto DiagonalStep = g_Hack::CameraControls::Step(0, 0, 1, 1, 1, 10, 0.1, false);
    assert(std::abs(std::sqrt(DiagonalStep.X * DiagonalStep.X + DiagonalStep.Y * DiagonalStep.Y +
        DiagonalStep.Z * DiagonalStep.Z) - 100) < 1e-6);
    const auto SlowFrame = g_Hack::CameraControls::Step(0, 0, 1, 0, 0, 10, 10, true);
    assert(SlowFrame.X == 300); // Focus stalls cannot throw the camera kilometres away.
    assert(g_Hack::CameraControls::Step(0, 0, 1, 0, 0, NAN, 0.1, false).X == 0);
    assert(g_Hack::CameraControls::Step(0, 0, 1, 0, 0, 10, -1, false).X == 0);
    g_Hack::NoteRequestGate NoteGate;
    assert(!NoteGate.Begin());
    assert(NoteGate.Request() && !NoteGate.Request()); // Coalesce clicks before the game tick.
    assert(NoteGate.Begin() && !NoteGate.Begin());
    assert(!NoteGate.Request()); // Reject clicks even during the final batch's dispatch.
    NoteGate.Reset();
    assert(!NoteGate.Begin()); // No late click survives completion.
    assert(NoteGate.Request());
    NoteGate.Reset(); // Cancellation also consumes a queued round.
    assert(!NoteGate.Begin() && NoteGate.Request() && NoteGate.Begin());
    NoteGate.Reset();
    g_Hack::NoteRequestBatch Notes;
    assert(!Notes.Pending() && Notes.NextBatch(0).empty());
    Notes.Start({10, 3, 1, 3, -1, 5, 0, 9, 8, 7, 6, 4, 2, -99}, 1000);
    assert(Notes.Total() == 11 && Notes.Sent() == 0 && Notes.NextBatch(999).empty());
    assert((Notes.NextBatch(1000) == std::vector<int>{0, 1, 2, 3})); // Sorted, unique, nonnegative.
    assert(Notes.Sent() == 4 && Notes.Pending());
    assert(Notes.NextBatch(1000).empty()); // Repeated calls in one tick cannot bypass the cap.
    assert(Notes.NextBatch(1099).empty() && Notes.Sent() == 4);
    assert((Notes.NextBatch(1100) == std::vector<int>{4, 5, 6, 7}));
    assert(Notes.NextBatch(1199).empty());
    assert((Notes.NextBatch(1200) == std::vector<int>{8, 9, 10})); // The tail needs no padding/repeats.
    assert(!Notes.Pending() && Notes.Sent() == Notes.Total() && Notes.NextBatch(10000).empty());
    Notes.Start({10, 11, 12, 13, 14, 15, 16, 17, 18}, 11000);
    assert(Notes.NextBatch(11000).size() == 4);
    assert((Notes.NextBatch(20000) == std::vector<int>{14, 15, 16, 17})); // Low FPS never catches up.
    assert(Notes.NextBatch(20000).empty() && Notes.NextBatch(20099).empty());
    assert((Notes.NextBatch(20100) == std::vector<int>{18}));
    assert(Notes.NextBatch(20200).empty()); // One round never resends an ID.
    Notes.Start({20, 21, 22, 23, 24}, 21000);
    assert(Notes.NextBatch(21000).size() == 4);
    Notes.Cancel();
    assert(!Notes.Pending() && Notes.Sent() == 0 && Notes.Total() == 0 && Notes.NextBatch(21000).empty());
    Notes.Start({21, 20, 21}, 21001); // Explicit restart resets both progress and the deadline.
    assert((Notes.NextBatch(21001) == std::vector<int>{20, 21}));
    assert(!Notes.Pending());
    Notes.Start({-1, -2, -1}, 23000);
    assert(Notes.Total() == 0 && Notes.Sent() == 0 && !Notes.Pending() && Notes.NextBatch(23000).empty());
    Notes.Start({}, 24000);
    assert(Notes.Total() == 0 && Notes.NextBatch(24000).empty());
    using StructureType = g_ESP::Structures::Type;
    const std::pair<const char*, StructureType> StructureCases[] = {
        {"PrimalItemStructure_Turret_C", StructureType::AutoTurret},
        {"StorageBox_Turret_C", StructureType::AutoTurret},
        {"Auto Turret", StructureType::AutoTurret},
        {"自动炮台", StructureType::AutoTurret},
        {"PrimalItemStructure_HeavyTurret_C", StructureType::HeavyTurret},
        {"StorageBox_Turret_Heavy_C", StructureType::HeavyTurret},
        {"Heavy Auto Turret", StructureType::HeavyTurret},
        {"重型自动炮台", StructureType::HeavyTurret},
        {"PrimalItemStructure_TurretTek_C", StructureType::TekTurret},
        {"StorageBox_TurretTek_C", StructureType::TekTurret},
        {"TEK TURRET", StructureType::TekTurret},
        {"泰克炮塔", StructureType::TekTurret},
        {"PrimalItemStructure_PowerGenerator_C", StructureType::Generator},
        {"ElectricGenerator_C", StructureType::Generator},
        {"Electrical Generator", StructureType::Generator},
        {"发电机", StructureType::Generator},
        {"PrimalItemStructure_TekGenerator_C", StructureType::TekGenerator},
        {"StorageBox_TekGenerator_C", StructureType::TekGenerator},
        {"Tek Generator", StructureType::TekGenerator},
        {"泰克发电机", StructureType::TekGenerator},
        {"PrimalItemStructure_WindTurbine_C", StructureType::WindTurbine},
        {"WindTurbine_C", StructureType::WindTurbine},
        {"Wind Turbine", StructureType::WindTurbine},
        {"风力发电机", StructureType::WindTurbine},
        {"SupplyCrate_Level25_C", StructureType::SupplyDrop},
        {"SupplyCrate_Cave_QualityTier2_C", StructureType::LootCrate},
        {"SupplyCrate_OceanInstant_C", StructureType::LootCrate},
        {"SupplyCrate_BuriedTreasureBase_Ascendant_C", StructureType::BuriedTreasure},
        {"TreasureCache_ASA_C", StructureType::BuriedTreasure},
        {"SupplyCrate_Chest_Treasure_JacksonL_C", StructureType::BuriedTreasure},
        {"BeaverDam_C", StructureType::Other},
        {"SupplyCrateBaseBP_Instantaneous_DenLogs_Child2_C", StructureType::Other},
        {"StorageBox_C", StructureType::Other},
        {"PrimalStructureTurret", StructureType::Other},
        {"PlantSpeciesX_Turret_C", StructureType::Other},
        {"Ballista Turret", StructureType::Other},
        {"Rocket Turret", StructureType::Other},
        {"木质地基", StructureType::Other}
    };
    for (const auto& [Identity, Expected] : StructureCases)
        assert(g_ESP::Structures::Classify(Identity) == Expected);
    // Every checkbox combination: no selection hides all; the disabled filter
    // admits ordinary buildings, and auto/heavy/Tek never imply one another.
    for (unsigned Mask = 0; Mask < 1024; ++Mask) {
        g_ESP::Structures::Filter Filter;
        for (unsigned I = 0; I < Filter.Selected.size(); ++I) Filter.Selected[I] = (Mask & (1u << I)) != 0;
        for (const auto& [Identity, Expected] : StructureCases) {
            assert(Filter.Allows(g_ESP::Structures::Classify(Identity)));
            Filter.Enabled = true;
            assert(Filter.Allows(g_ESP::Structures::Classify(Identity)) == ((Mask & (1u << static_cast<unsigned>(Expected))) != 0));
            Filter.Enabled = false;
        }
    }
    // Mount possession uses a different pawn from the survivor. Only that
    // survivor's ridden mount qualifies; stale/foreign/spectator pawns do not.
    assert(g_Hack::AutoLootWildEggAllowed(true, "PrimalItemConsumable_Egg_Wyvern_Fertilized_Fire_C"));
    assert(g_Hack::AutoLootWildEggAllowed(true, "PrimalItemConsumable_Egg_Deinonychus_Fertilized_C"));
    assert(!g_Hack::AutoLootWildEggAllowed(false, "PrimalItemConsumable_Egg_Wyvern_Fertilized_C"));
    assert(!g_Hack::AutoLootWildEggAllowed(true, "PrimalItemConsumable_Egg_Raptor_Fertilized_C"));
    assert(!g_Hack::AutoLootWildEggAllowed(true, "PrimalItemArmor_Wyvern_C"));
    assert(g_Hack::AutoLootBeaverDamAllowed("BeaverDam_C"));
    assert(g_Hack::AutoLootBeaverDamAllowed("SupplyCrateBaseBP_Instantaneous_DenLogs_Child2_C"));
    assert(!g_Hack::AutoLootBeaverDamAllowed("SupplyCrate_Level25_C"));
    assert(g_Hack::AutoLootSpecialContainerAllowed("ArtifactCrate_5_C"));
    assert(g_Hack::AutoLootSpecialContainerAllowed("ArtifactContainer_C"));
    assert(g_Hack::AutoLootSpecialContainerAllowed("SupplyCrate_BuriedTreasure_ShoulderDragon_C"));
    assert(g_Hack::AutoLootSpecialContainerAllowed("TreasureChest_C"));
    assert(!g_Hack::AutoLootSpecialContainerAllowed("StorageBox_Large_C"));
    assert(!g_Hack::AutoLootSpecialContainerAllowed("PrimalItemArtifactGeneric_C"));
    int Survivor = 1, Dino = 2, Stranger = 3, Spectator = 4;
    assert(g_Hack::AutoLootControlsHuman(&Survivor, &Survivor, nullptr, nullptr));
    assert(g_Hack::AutoLootControlsHuman(&Dino, &Survivor, &Dino, &Survivor));
    assert(!g_Hack::AutoLootControlsHuman(&Dino, &Survivor, &Dino, &Stranger));
    assert(!g_Hack::AutoLootControlsHuman(&Dino, &Survivor, &Dino, nullptr));
    assert(!g_Hack::AutoLootControlsHuman(&Spectator, &Survivor, &Dino, &Survivor));
    assert(!g_Hack::AutoLootControlsHuman(nullptr, &Survivor, nullptr, nullptr));
    assert(!g_Hack::AutoLootControlsHuman(&Dino, nullptr, &Dino, nullptr));
    assert(g_Hack::AutoLootControlsHuman(&Dino, &Survivor, &Dino, nullptr, &Dino));
    assert(!g_Hack::AutoLootControlsHuman(&Dino, &Survivor, &Dino, nullptr, &Stranger));
    assert(!g_Hack::AutoLootControlsHuman(&Dino, &Survivor, &Dino, &Stranger, &Dino));
    assert(!g_Hack::AutoLootControlsHuman(&Spectator, &Survivor, &Dino, nullptr, &Dino));
    int NativeViewCalls = 0;
    auto RejectNativeView = [&] { ++NativeViewCalls; return false; };
    assert(g_Hack::AutoLootCanRequestContainer(true, RejectNativeView));
    assert(NativeViewCalls == 0); // A mounted UI veto must not prevent the server request.
    assert(!g_Hack::AutoLootCanRequestContainer(false, RejectNativeView));
    assert(NativeViewCalls == 1);
    assert(g_Hack::AutoLootCanRequestContainer(false, [] { return true; }));
    assert(!g_Hack::AutoLootInventoryUIBlocks(true, false, &Dino, &Dino));
    assert(g_Hack::AutoLootInventoryUIBlocks(true, true, &Dino, &Dino));
    assert(g_Hack::AutoLootInventoryUIBlocks(true, false, &Stranger, &Dino));
    assert(g_Hack::AutoLootInventoryUIBlocks(true, false, nullptr, nullptr));
    assert(!g_Hack::AutoLootInventoryUIBlocks(false, false, nullptr, nullptr));
    assert(!g_Hack::AutoLootInventoryViewBusy(&Dino, &Dino, nullptr, false));
    assert(!g_Hack::AutoLootInventoryViewBusy(&Dino, nullptr, &Dino, true));
    assert(g_Hack::AutoLootInventoryViewBusy(&Dino, nullptr, &Dino, false));
    assert(g_Hack::AutoLootInventoryViewBusy(&Stranger, nullptr, &Dino, true));
    assert(!g_Hack::AutoLootInventoryViewBusy(nullptr, &Dino, &Dino, true));
    assert(g_Hack::AutoLootAccessRange(10, 500) == 15);
    assert(g_Hack::AutoLootAccessRange(10, 0) == 10);
    assert(g_Hack::AutoLootAccessRange(10, NAN) == 10);
    assert(g_Hack::AutoLootAccessRange(10, -500) == 10);
    assert(g_Hack::AutoLootWithinRange(1000, 10));
    assert(!g_Hack::AutoLootWithinRange(1000.1, 10));
    assert(!g_Hack::AutoLootWithinRange(NAN, 10));
    assert(!g_Hack::AutoLootWithinRange(-1, 10));
    assert(!g_Hack::AutoLootWithinRange(100, NAN));
    // A loaded inventory is transferred on the next 100 ms observation, even
    // with a 3 s scan setting. Sync refreshes never duplicate the transfer RPC.
    using LootAction = g_Hack::AutoLootTransferAction;
    using LootSession = g_Hack::AutoLootTransferSession;
    const auto LootTime = [](int Ms) { return LootSession::Clock::time_point(std::chrono::milliseconds(Ms)); };
    LootSession Sync;
    assert(Sync.Step(LootTime(0), 3) == LootAction::Wait);
    Sync.Begin(LootTime(0), 3000);
    assert(Sync.Step(LootTime(0), 0) == LootAction::Wait);
    assert(Sync.Step(LootTime(99), 3) == LootAction::Wait);
    assert(Sync.Step(LootTime(100), 3) == LootAction::Transfer);
    assert(Sync.Step(LootTime(200), 3) == LootAction::Wait);
    assert(Sync.Step(LootTime(1100), 3) == LootAction::Refresh);
    assert(Sync.Step(LootTime(1200), 2) == LootAction::Complete);
    assert(Sync.Step(LootTime(1300), 2) == LootAction::Wait);
    Sync.Begin(LootTime(0), 750);
    assert(Sync.Step(LootTime(0), 0) == LootAction::Wait);
    assert(Sync.Step(LootTime(750), 0) == LootAction::Refresh);
    assert(Sync.Step(LootTime(800), 0) == LootAction::Wait);
    assert(Sync.Step(LootTime(1500), 0) == LootAction::Refresh);
    assert(Sync.Step(LootTime(1600), 5) == LootAction::Transfer);
    assert(Sync.Step(LootTime(2350), 5) == LootAction::Refresh);
    assert(Sync.Step(LootTime(4600), 5) == LootAction::Timeout);
    assert(Sync.Step(LootTime(5000), 5) == LootAction::Wait);
    // A stalled initial sync expires independently of the transfer confirmation.
    Sync.Begin(LootTime(0), 10);
    assert(Sync.Step(LootTime(249), 0) == LootAction::Wait);
    assert(Sync.Step(LootTime(349), 0) == LootAction::Refresh);
    assert(Sync.Step(LootTime(6000), 0) == LootAction::Timeout);
    Sync.Begin(LootTime(0), 750);
    assert(Sync.Step(LootTime(6000), 1) == LootAction::Transfer); // Late sync wins over timeout.
    assert(Sync.Step(LootTime(9000), 0) == LootAction::Complete); // Confirmation wins over timeout.
    Sync.Begin(LootTime(0), 750);
    assert(Sync.Step(LootTime(0), 10001) == LootAction::Wait);
    assert(Sync.Step(LootTime(100), -1) == LootAction::Wait);
    assert(g_Hack::AutoLootFailureDelayMs(0) == 500);
    assert(g_Hack::AutoLootFailureDelayMs(1) == 500);
    assert(g_Hack::AutoLootFailureDelayMs(2) == 1000);
    assert(g_Hack::AutoLootFailureDelayMs(3) == 2000);
    assert(g_Hack::AutoLootFailureDelayMs(4) == 4000);
    assert(g_Hack::AutoLootFailureDelayMs(1000) == 4000);
    CheckImpact({}, {1000, 0, 0}, {}, 2000, 0, 3);
    CheckImpact({}, {3000, 0, 0}, {0, 600, 0}, 5000, -980, 3);
    CheckImpact({100, -400, 20}, {3000, 500, 500}, {-700, 250, -100}, 6000, -980, 3, 0.12);
    CheckImpact({}, {1000, 0, 2500}, {0, 0, 200}, 5000, -980, 3);
    CheckImpact({0, 0, 2000}, {}, {0, 600, 0}, 5000, -980, 3);
    CheckImpact({10000000, -10000000, 50000}, {10001000, -9999000, 50000}, {0, 200, 0}, 3000, -490, 3);
    auto Slow = PredictProjectile({}, {1200, 0, 0}, {0, 500, 0}, 1800, -980, 3);
    auto Fast = PredictProjectile({}, {1200, 0, 0}, {0, 500, 0}, 6000, -980, 3);
    assert(Slow && Fast && Slow->Time > Fast->Time && Slow->Impact.Y > Fast->Impact.Y);
    assert(!PredictProjectile({}, {1000, 0, 0}, {3000, 0, 0}, 1000, 0, 5));
    assert(!PredictProjectile({}, {100000, 0, 0}, {}, 1000, -980, 5));
    assert(!PredictProjectile({}, {1000, 0, 0}, {}, 0, -980, 5));
    assert(!PredictProjectile({}, {1000, 0, 0}, {}, 1000, -980, -1));
    assert(!PredictProjectile({}, {1000, 0, 0}, {}, 1000, NAN, 5));
    assert(!PredictProjectile({}, {NAN, 0, 0}, {}, 1000, 0, 5));
    assert(!PredictProjectile({}, {1000, 0, 0}, {}, 1000, 0, 5, -1));
    // Regression: at 150% DPI raw ESP pixels were 1.5x farther from the
    // top-left than the correctly converted magic-bullet target marker.
    for (double Dpi : {1.0, 1.25, 1.5, 2.0}) {
        for (auto Size : {std::pair{1920.0,1080.0}, std::pair{2560.0,1440.0}, std::pair{3440.0,1440.0}}) {
            const auto Space = AimCanvasSpace::FromViewport(Size.first, Size.second, Size.first / Dpi, Size.second / Dpi);
            for (auto Position : {std::pair{0.1,0.1}, std::pair{0.5,0.5}, std::pair{0.9,0.9}}) {
                const auto Marker = Space.ToCanvas(Size.first * Position.first, Size.second * Position.second);
                const auto Top = Space.ToCanvas(Size.first * Position.first - 30, Size.second * Position.second - 70);
                const auto Bottom = Space.ToCanvas(Size.first * Position.first + 30, Size.second * Position.second + 70);
                assert(Top.X < Marker.X && Top.Y < Marker.Y && Bottom.X > Marker.X && Bottom.Y > Marker.Y);
                assert(std::abs((Top.X + Bottom.X) * 0.5 - Marker.X) < 1e-7);
                assert(std::abs(Marker.X * Dpi - Size.first * Position.first) < 1e-7);
            }
        }
    }
    assert(AimCanvasSpace::FromViewport(0, 1080, 1920, 1080).ScaleX == 0);
    // A dense group must remain attached to its own target. Try the four
    // adjacent slots as a whole and suppress it when every slot is occupied.
    using namespace g_ESP::Layout;
    const double C = std::sqrt(0.5);
    const auto Corners = TightBounds({100, 20, 50}, {1, 1, 1}, {0, 0, std::sin(3.141592653589793 / 8), std::cos(3.141592653589793 / 8)}, {120*C, 120*C, 50});
    assert(Corners);
    double Width = 0;
    for (const auto& V : *Corners) Width = std::max(Width, std::abs((-V.X + V.Y) * C));
    assert(std::abs(Width - 20) < 1e-6); // Projected cross-section, instead of world AABB width 120.
    assert(!TightBounds({100, 20, 50}, {1, 1, 1}, {0, 0, 0, 1}, {200, 20, 50}));
    assert(!TightBounds({100, 20, 50}, {1, 1, 1}, {0, 0, 0, 0}, {100, 20, 50}));
    assert(!TightBounds({NAN, 20, 50}, {1, 1, 1}, {0, 0, 0, 1}, {100, 20, 50}));
    const Rect Box{400, 300, 70, 140};
    std::vector<Rect> Occupied;
    for (int Expected = 0; Expected < 4; ++Expected) {
        auto P = Place(Box, 180, 68, 1280, 720, 7, Occupied, true);
        assert(P && P->Slot == Expected && P->Bounds.Inside(1280, 720, 2));
        for (const auto& Other : Occupied) assert(!P->Bounds.Overlaps(Other, 3));
        Occupied.push_back(P->Bounds);
    }
    assert(!Place(Box, 180, 68, 1280, 720, 7, Occupied, true));
    assert(Place(Box, 180, 68, 1280, 720, 7, Occupied, false));
    assert(Place(Box, 180, 68, 1280, 720, 7, {}, true, 2)->Slot == 2);
    assert(Place({400, 2, 70, 140}, 180, 68, 1280, 720, 7, {}, true)->Slot == 1);
    assert(Place({3, 300, 70, 140}, 180, 68, 1280, 720, 7, {}, true)->Slot == 2);
    assert(!Place({NAN, 0, 70, 140}, 180, 68, 1280, 720, 7, {}, true));
    assert(!Place(Box, 180, 68, 0, 720, 7, {}, true));
    assert(FontPixels(16, 0.25, 13) == 13);
    assert(FontPixels(16, 3, 13) == 48);
    assert(FontPixels(12, 0.25, NAN) == 12);
    assert(FontPixels(NAN, NAN, NAN) == 16);
    assert(FontPixels(16, 0.25, 32) == 16);
    for (float Requested : {0.25f, 1.0f, 2.0f, 10.0f, 20.0f, 100.0f, 200.0f, float(NAN)})
        assert(g_Hack::EffectiveFireRateMultiplier(false, Requested) == g_Hack::EffectiveFireRateMultiplier(true, Requested));
    assert(g_Hack::EffectiveFireRateMultiplier(true, 2.0f) == 2.0f);
    assert(g_Hack::EffectiveFireRateMultiplier(true, 20.0f) == 20.0f);
    assert(g_Hack::EffectiveFireRateMultiplier(true, 100.0f) == 100.0f);
    assert(g_Hack::EffectiveFireRateMultiplier(true, 200.0f) == 100.0f);
    assert(g_Hack::EffectiveFireRateMultiplier(true, NAN) == 1.0f);
    // Tek bow ammo selection and the explosive post-spawn speed override.
    for (int Ammo : {0, 1, 2, 3}) {
        auto Half = TekBowArrowSpeed(Ammo, 4, 0.5, 1, 2000, 5200, 2000, 2400);
        auto Full = TekBowArrowSpeed(Ammo, 4, 2, 1, 2000, 5200, 2000, 2400);
        assert(Half && *Half == (Ammo == 1 ? 2200 : 3600));
        assert(Full && *Full == (Ammo == 1 ? 2400 : 5200));
        CheckImpact({}, {1000, 0, 0}, {0, 300, 0}, *Half, -980, 3);
    }
    assert(!TekBowArrowSpeed(4, 4, 1, 1, 2000, 5200, 2000, 2400));
    assert(!TekBowArrowSpeed(-1, 4, 1, 1, 2000, 5200, 2000, 2400));
    assert(!TekBowArrowSpeed(1, 4, 1, 0, 2000, 5200, 2000, 2400));
    assert(!TekBowArrowSpeed(1, 4, NAN, 1, 2000, 5200, 2000, 2400));
    assert(!TekBowArrowSpeed(1, 4, 1, 1, 2000, 5200, NAN, 2400));
    auto Directory = std::filesystem::temp_directory_path() / "asa-gameplay-extension-tests";
    std::filesystem::create_directories(Directory);
    auto& Manager = ConfigManager::Get(); Manager.Initialize(Directory.string());
    g_Config::bWeaponFireRate = g_Config::bProjectilePrediction = true;
    g_Config::bCrossbowHoldRapidFire = true;
    g_Config::bAutoLootContainers = true;
    g_Config::WeaponFireRateMultiplier = 3.5f;
    g_Config::ProjectilePredictionMaxSeconds = 4.5f; g_Config::ProjectilePredictionDelayMs = 125;
    g_Config::AutoLootRangeMeters = 12;
    g_Config::AutoLootIntervalMs = 1250; g_Config::AutoLootMaxWeightPercent = 75;
    g_Config::bAutoLootSupplyCrates = false; g_Config::bAutoLootBags = true; g_Config::bAutoLootWildEggs = false;
    g_Config::bAutoLootBeaverDams = true;
    g_Config::ESPFontPixels = 22; g_Config::ESPMinFontPixels = 15;
    g_Config::FreeCameraSpeedMeters = 22;
    g_Config::ESPBoxPaddingPixels = 3.5f; g_Config::ESPBoxThicknessPixels = 2;
    g_Config::bESPCornerBox = false; g_Config::bESPPreventOverlap = true;
    assert(Manager.SaveConfig("extensions.ini"));
    // Old configuration files remain readable after retiring these settings.
    {
        std::ofstream Legacy(Directory / "extensions.ini", std::ios::app);
        Legacy << "g_Config::bPlayerMoveSpeed=true\n"
               << "g_Config::PlayerMoveSpeedMultiplier=5\n"
               << "g_Config::bAutoFeroxLevelStack=true\n"
               << "g_Config::AutoFeroxStage1Percent=70\n"
               << "g_Config::AutoFeroxElixirDelaySeconds=0.5\n"
               << "g_Config::AutoImprintRangeMeters=30\n"
               << "g_Config::bAutoGrindHelicoprionInventory=true\n"
               << "g_Config::bExpandPlayerInventory=true\n"
               << "g_Config::PlayerInventorySlotLimit=5000\n"
               << "g_Config::bShowLostColonyCooldowns=true\n"
               << "g_Config::bUnlockExplorerNotes=true\n"
               << "g_Config::bOutBodyChecked=true\n"
               << "g_Config::bSkyArrow=true\n"
               << "g_Config::SkyArrowHeightMeters=100\n";
    }
    g_Config::bWeaponFireRate = g_Config::bProjectilePrediction = false;
    g_Config::bCrossbowHoldRapidFire = false;
    g_Config::bAutoLootContainers = false;
    g_Config::WeaponFireRateMultiplier = 1;
    g_Config::ProjectilePredictionMaxSeconds = g_Config::ProjectilePredictionDelayMs = 0;
    g_Config::AutoLootRangeMeters = 0;
    g_Config::AutoLootIntervalMs = g_Config::AutoLootMaxWeightPercent = 0;
    g_Config::bAutoLootSupplyCrates = true; g_Config::bAutoLootBags = false; g_Config::bAutoLootWildEggs = true;
    g_Config::bAutoLootBeaverDams = false;
    g_Config::ESPFontPixels = g_Config::ESPMinFontPixels = 0;
    g_Config::ESPBoxPaddingPixels = g_Config::ESPBoxThicknessPixels = 0;
    g_Config::bESPCornerBox = true; g_Config::bESPPreventOverlap = false;
    g_Config::FreeCameraSpeedMeters = 1;
    g_Config::bFreeCameraEnabled = true;
    assert(Manager.LoadConfig("extensions.ini"));
    assert(g_Config::bWeaponFireRate && g_Config::bProjectilePrediction);
    assert(g_Config::bCrossbowHoldRapidFire);
    assert(g_Config::FreeCameraSpeedMeters == 22 && !g_Config::bFreeCameraEnabled);
    assert(g_Config::bAutoLootContainers);
    assert(g_Config::WeaponFireRateMultiplier == 3.5f);
    assert(g_Config::ProjectilePredictionMaxSeconds == 4.5f && g_Config::ProjectilePredictionDelayMs == 125);
    assert(g_Config::AutoLootRangeMeters == 12);
    assert(g_Config::AutoLootIntervalMs == 1250 && g_Config::AutoLootMaxWeightPercent == 75);
    assert(!g_Config::bAutoLootSupplyCrates && g_Config::bAutoLootBags && !g_Config::bAutoLootWildEggs);
    assert(g_Config::bAutoLootBeaverDams);
    assert(g_Config::ESPFontPixels == 22 && g_Config::ESPMinFontPixels == 15);
    assert(g_Config::ESPBoxPaddingPixels == 3.5f && g_Config::ESPBoxThicknessPixels == 2);
    assert(!g_Config::bESPCornerBox && g_Config::bESPPreventOverlap);
    bool* StructureSettings[] = {&g_Config::bStructureAutoTurret, &g_Config::bStructureHeavyTurret,
        &g_Config::bStructureTekTurret, &g_Config::bStructureGenerator, &g_Config::bStructureTekGenerator,
        &g_Config::bStructureWindTurbine, &g_Config::bStructureSupplyDrop,
        &g_Config::bStructureLootCrate, &g_Config::bStructureBuriedTreasure,
        &g_Config::bStructureOther, &g_Config::bStructureShowOwner, &g_Config::bStructureShowLootContents};
    for (int Pattern : {0, 1}) {
        g_Config::bStructureTypeFilter = Pattern != 0;
        for (int I = 0; I < 12; ++I) *StructureSettings[I] = (I % 2) == Pattern;
        assert(Manager.SaveConfig("structures.ini"));
        g_Config::bStructureTypeFilter = Pattern == 0;
        for (auto* Setting : StructureSettings) *Setting = !*Setting;
        assert(Manager.LoadConfig("structures.ini"));
        assert(g_Config::bStructureTypeFilter == (Pattern != 0));
        for (int I = 0; I < 12; ++I) assert(*StructureSettings[I] == ((I % 2) == Pattern));
    }
    {
        std::ofstream Legacy(Directory / "structures-legacy.ini");
        Legacy << "[Structures]\ng_Config::bDrawStructures=true\n";
    }
    assert(Manager.LoadConfig("structures-legacy.ini"));
    assert(g_Config::bDrawStructures && !g_Config::bStructureTypeFilter);
    for (int I = 0; I < 9; ++I) assert(*StructureSettings[I]);
    assert(!g_Config::bStructureOther && g_Config::bStructureShowOwner && !g_Config::bStructureShowLootContents);
    std::filesystem::remove(Directory / "structures.ini");
    std::filesystem::remove(Directory / "structures-legacy.ini");
    assert(Manager.SaveConfig("extensions.ini"));
    {
        std::ifstream Saved(Directory / "extensions.ini");
        const std::string Text{std::istreambuf_iterator<char>(Saved), std::istreambuf_iterator<char>()};
        for (const auto* Retired : {"bPlayerMoveSpeed", "PlayerMoveSpeedMultiplier", "bSkyArrow", "SkyArrowHeightMeters",
            "AutoImprintRangeMeters", "bAutoGrindHelicoprionInventory", "bExpandPlayerInventory",
            "PlayerInventorySlotLimit", "bShowLostColonyCooldowns", "bUnlockExplorerNotes", "bOutBodyChecked", "bFreeCameraEnabled",
            "AutoFeroxLevelStack", "AutoFeroxStage", "AutoFeroxElixirDelaySeconds"})
            assert(Text.find(Retired) == std::string::npos);
        // Imprint was reintroduced by FeatureExpansion; its old range setting
        // remains retired, but the current switch must survive configuration saves.
        assert(Text.find("g_Config::bAutoImprint=") != std::string::npos);
    }
    std::filesystem::remove(Directory / "extensions.ini");
    std::cout << "Gameplay, structures, mounted loot, free-camera input/movement, note request batching and retired configuration regression checks passed\n";
}
