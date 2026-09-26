#include <API/ARK/Ark.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <stdexcept>
#include <vector>

namespace {
constexpr wchar_t kTickName[] = L"Phoenix.MekBeamTiming";
constexpr wchar_t kMekPath[] = L"/Game/Extinction/Dinos/Mek/Mek_Character_BP.Mek_Character_BP";
struct Settings { bool Enabled = false; float Attack = 0.5f; double Cooldown = 4.0; float DamageTick = 0.05f; } Settings;
struct Saved {
    TWeakObjectPtr<APrimalDinoCharacter> Mek;
    float OriginalAttack = 0, AppliedAttack = 0;
    float OriginalRider = 0, AppliedRider = 0;
    double OriginalCooldown = 0, AppliedCooldown = 0;
};
std::vector<Saved> Patched;
UClass* MekClass = nullptr;
UFunction* PatchedBeamScript = nullptr;
int DamageTickOffset = -1;
float Accumulator = 0;
bool Faulted = false;
int ModuleAnchor = 0;

bool HasName(UClass* Class, const char* Name) {
    return Class && Class->NamePrivateField() == FName(Name, EFindName::FNAME_Find);
}
void RestoreDamageTick() {
    if (!PatchedBeamScript || DamageTickOffset < 0) return;
    auto& Script = PatchedBeamScript->ScriptField();
    if (Script.Num() == 8482 && DamageTickOffset + 4 <= Script.Num()) {
        const float Applied = Settings.DamageTick;
        if (std::memcmp(Script.GetData() + DamageTickOffset, &Applied, sizeof(Applied)) == 0) {
            const float Original = 0.1f;
            std::memcpy(Script.GetData() + DamageTickOffset, &Original, sizeof(Original));
        }
    }
    PatchedBeamScript = nullptr;
    DamageTickOffset = -1;
}
void PatchDamageTick(UClass* ProjectileClass) {
    if (PatchedBeamScript || Settings.DamageTick == 0.1f) return;
    if (!HasName(ProjectileClass, "ProjDinozordMEKLaserBeam_C"))
        throw std::runtime_error("Beam projectile class changed");
    auto* Function = ProjectileClass->FindFunctionByName(
        FName("ExecuteUbergraph_ProjDinozordMEKLaserBeam", EFindName::FNAME_Find), EIncludeSuperFlag::IncludeSuper);
    if (!Function) throw std::runtime_error("Beam graph unavailable");
    auto& Script = Function->ScriptField();
    if (Script.Num() != 8482 || !Script.GetData()) throw std::runtime_error("Beam graph version changed");
    // In the verified build, the timer's named callback is ClearTargets and
    // its immediately following EX_FloatConst is the 0.1 s hit reset period.
    constexpr unsigned char Signature[] = {
        'C','l','e','a','r','T','a','r','g','e','t','s',0,0x1e,0xcd,0xcc,0xcc,0x3d
    };
    int Found = -1;
    for (int Index = 0; Index + int(sizeof(Signature)) <= Script.Num(); ++Index) {
        if (std::memcmp(Script.GetData() + Index, Signature, sizeof(Signature)) != 0) continue;
        if (Found >= 0) throw std::runtime_error("Ambiguous beam hit reset timer");
        Found = Index + 14;
    }
    if (Found < 0) throw std::runtime_error("Beam hit reset timer changed");
    std::memcpy(Script.GetData() + Found, &Settings.DamageTick, sizeof(Settings.DamageTick));
    PatchedBeamScript = Function;
    DamageTickOffset = Found;
    Log::GetLog()->info("MEK beam hit reset timer 0.1 -> {}", Settings.DamageTick);
}
std::filesystem::path Directory() {
    HMODULE Module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ModuleAnchor), &Module)) throw std::runtime_error("Cannot find plugin module");
    wchar_t Path[32768]{};
    const DWORD Length = GetModuleFileNameW(Module, Path, 32768);
    if (!Length || Length >= 32768) throw std::runtime_error("Cannot find plugin path");
    return std::filesystem::path(Path).parent_path();
}
bool WearingZord(APrimalDinoCharacter* Mek) {
    auto* Function = Mek->ClassField()->FindFunctionByName(FName("IsUsingZordSkin", EFindName::FNAME_Find), EIncludeSuperFlag::IncludeSuper);
    if (!Function) return false;
    const auto Bytes = *GetNativePointerField<unsigned short*>(Function, "UFunction.ParmsSize");
    if (Bytes < 1 || Bytes > 32) throw std::runtime_error("IsUsingZordSkin parameter layout changed");
    alignas(16) unsigned char Frame[32]{};
    Mek->ProcessEvent(Function, Frame);
    return Frame[0] != 0;
}
double& HeatCooldown(APrimalDinoCharacter* Mek) {
    auto* Class = Mek->ClassField();
    auto* Field = Class->FindPropertyByName(FName("HeatReductionCooldown", EFindName::FNAME_Find));
    if (!Field || Field->ElementSizeField() != sizeof(double) || Field->ArrayDimField() != 1 ||
        Field->Offset_InternalField() < 0 || Field->Offset_InternalField() > Class->PropertiesSizeField() - int(sizeof(double)))
        throw std::runtime_error("HeatReductionCooldown schema changed");
    auto* Kind = *GetNativePointerField<void**>(Field, "FField.ClassPrivate");
    if (!Kind || *GetNativePointerField<FName*>(Kind, "FFieldClass.Name") != FName("DoubleProperty", EFindName::FNAME_Find))
        throw std::runtime_error("HeatReductionCooldown is not double");
    return *reinterpret_cast<double*>(reinterpret_cast<unsigned char*>(Mek) + Field->Offset_InternalField());
}
FDinoAttackInfo* Beam(APrimalDinoCharacter* Mek) {
    auto& Attacks = Mek->AttackInfosField();
    if (Attacks.Num() != 4) return nullptr;
    // AsaApi's FDinoAttackInfo is a reflection wrapper (sizeof == 1), so
    // TArray::operator[] must not be used for element address arithmetic.
    auto* Struct = FDinoAttackInfo::StaticStruct();
    if (!Struct) return nullptr;
    const int Stride = Struct->PropertiesSizeField();
    if (Stride < 0x100 || Stride > 0x1000 || !Attacks.GetData())
        throw std::runtime_error("DinoAttackInfo array schema changed");
    auto* BeamAttack = reinterpret_cast<FDinoAttackInfo*>(
        reinterpret_cast<unsigned char*>(Attacks.GetData()) + 3 * Stride);
    if (!HasName(BeamAttack->ProjectileClassField().uClass, "ProjDinozordMEKLaserBeam_C") ||
        !HasName(BeamAttack->AttackStateTypeClassField().uClass, "MEKLaserBeamAttackStateRanged_C")) return nullptr;
    return BeamAttack;
}
void Restore(Saved& Entry) {
    auto* Mek = Entry.Mek.Get();
    if (!Mek || Mek->bActorIsBeingDestroyed().Get()) return;
    if (auto* BeamAttack = Beam(Mek)) {
        if (BeamAttack->AttackIntervalField() == Entry.AppliedAttack) BeamAttack->AttackIntervalField() = Entry.OriginalAttack;
        if (BeamAttack->RiderAttackIntervalField() == Entry.AppliedRider) BeamAttack->RiderAttackIntervalField() = Entry.OriginalRider;
    }
    auto& Cooldown = HeatCooldown(Mek);
    if (Cooldown == Entry.AppliedCooldown) Cooldown = Entry.OriginalCooldown;
}
void RestoreAll() {
    for (auto& Entry : Patched) {
        try { Restore(Entry); } catch (const std::exception& Error) { Log::GetLog()->error("MEK restore: {}", Error.what()); }
    }
    Patched.clear();
}
void Apply(APrimalDinoCharacter* Mek) {
    auto* Attack = Beam(Mek);
    if (!Attack) return;
    PatchDamageTick(Attack->ProjectileClassField().uClass);
    auto& Cooldown = HeatCooldown(Mek);
    if (!std::isfinite(Attack->AttackIntervalField()) || !std::isfinite(Attack->RiderAttackIntervalField()) ||
        !std::isfinite(Cooldown)) throw std::runtime_error("Nonfinite MEK timing baseline");
    auto It = std::find_if(Patched.begin(), Patched.end(), [Mek](Saved& Entry) { return Entry.Mek.Get() == Mek; });
    if (It == Patched.end()) {
        Saved Entry;
        Entry.Mek = GetWeakReference(Mek);
        Entry.OriginalAttack = Entry.AppliedAttack = Attack->AttackIntervalField();
        Entry.OriginalRider = Entry.AppliedRider = Attack->RiderAttackIntervalField();
        Entry.OriginalCooldown = Entry.AppliedCooldown = Cooldown;
        Patched.push_back(Entry);
        It = std::prev(Patched.end());
    }
    if (Attack->AttackIntervalField() != It->AppliedAttack) It->OriginalAttack = Attack->AttackIntervalField();
    if (Attack->RiderAttackIntervalField() != It->AppliedRider) It->OriginalRider = Attack->RiderAttackIntervalField();
    if (Cooldown != It->AppliedCooldown) It->OriginalCooldown = Cooldown;
    Attack->AttackIntervalField() = It->AppliedAttack = Settings.Attack;
    Attack->RiderAttackIntervalField() = It->AppliedRider = Settings.Attack;
    Cooldown = It->AppliedCooldown = Settings.Cooldown;
}
void Tick(float Delta) {
    if (Faulted || !Settings.Enabled || !std::isfinite(Delta)) return;
    Accumulator += Delta;
    if (Accumulator < 0.5f) return;
    Accumulator = 0;
    try {
        auto* World = AsaApi::GetApiUtils().GetWorld();
        if (!World) return;
        if (!MekClass) MekClass = UVictoryCore::BPLoadClass(FString(kMekPath));
        if (!HasName(MekClass, "Mek_Character_BP_C")) return;
        TSubclassOf<AActor> Type;
        Type.uClass = MekClass;
        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsOfClass(World, Type, &Found);
        std::vector<APrimalDinoCharacter*> Wanted;
        for (auto* Actor : Found) {
            auto* Mek = static_cast<APrimalDinoCharacter*>(Actor);
            if (!Mek || Mek->bActorIsBeingDestroyed().Get() || Mek->IsDeadOrDying() || !WearingZord(Mek)) continue;
            Wanted.push_back(Mek);
            Apply(Mek);
        }
        for (auto It = Patched.begin(); It != Patched.end();) {
            auto* Mek = It->Mek.Get();
            if (std::find(Wanted.begin(), Wanted.end(), Mek) == Wanted.end()) {
                Restore(*It);
                It = Patched.erase(It);
            } else ++It;
        }
    } catch (const std::exception& Error) {
        Faulted = true;
        RestoreAll();
        RestoreDamageTick();
        Log::GetLog()->error("MEK beam timing disabled: {}", Error.what());
    }
}
}

extern "C" __declspec(dllexport) void Plugin_Init() noexcept(false) {
    Log::Get().Init("MekBeamTiming");
    std::ifstream Input(Directory() / L"config.json");
    if (!Input) throw std::runtime_error("MekBeamTiming config.json missing");
    nlohmann::json Config;
    Input >> Config;
    Settings.Enabled = Config.value("enabled", false);
    Settings.Attack = Config.value("attack_interval_seconds", 0.5f);
    Settings.Cooldown = Config.value("heat_reduction_cooldown_seconds", 4.0);
    Settings.DamageTick = Config.value("beam_hit_reset_interval_seconds", 0.05f);
    if (!std::isfinite(Settings.Attack) || Settings.Attack < 0.1f || Settings.Attack > 2.f ||
        !std::isfinite(Settings.Cooldown) || Settings.Cooldown < 0 || Settings.Cooldown > 12 ||
        !std::isfinite(Settings.DamageTick) || Settings.DamageTick < 0.03f || Settings.DamageTick > 0.1f)
        throw std::runtime_error("MekBeamTiming config values outside verified bounds");
    if (Settings.Enabled) AsaApi::GetCommands().AddOnTickCallback(kTickName, &Tick);
    Log::GetLog()->info("MEK beam timing enabled={} attack={} cooldown={} hit_reset={}", Settings.Enabled, Settings.Attack, Settings.Cooldown, Settings.DamageTick);
}

extern "C" __declspec(dllexport) void Plugin_Unload() {
    if (Settings.Enabled) AsaApi::GetCommands().RemoveOnTickCallback(kTickName);
    RestoreAll();
    RestoreDamageTick();
}
