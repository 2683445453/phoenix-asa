// Exercise the production dispatcher without connecting to or invoking a game.
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../src/internal/Hack/NoteExperiment.h"
#include "../src/internal/Hack/WorldUtilityRules.h"

static uint64_t NoteTestClock = 1000;
static uint64_t NoteTestNow() { return NoteTestClock; }
#define GetTickCount64 NoteTestNow

namespace SDK {
struct UClass; struct UFunction;
struct UObject {
    UClass* Class = nullptr;
    bool Live = true;
    int Serial = 1, Index = 0;
    std::string Name;
    std::function<void(UFunction*, void*)> Event;
    std::string GetName() const { return Name; }
    bool IsA(UClass* Type) const { return Class == Type; }
    void ProcessEvent(UFunction* Function, void* Parameters) { if (Event) Event(Function, Parameters); }
};
struct FName { std::string Text; std::string ToString() const { return Text; } };
struct FFieldClass { FName Name; };
struct FProperty { FFieldClass* ClassPrivate = nullptr; int Offset = 0, ElementSize = 0, ArrayDim = 1; };
struct UStruct : UObject { int Size = 0; std::map<std::string, FProperty*> Fields; };
struct UFunction : UStruct {};
struct UClass : UStruct {
    std::map<std::string, UFunction*> Functions;
    UFunction* GetFunction(const char*, const char* Key) {
        const auto I = Functions.find(Key); return I == Functions.end() ? nullptr : I->second;
    }
};
template<class T> struct TArray {
    T* Data = nullptr; int Count = 0, Capacity = 0;
    int Num() const { return Count; }
    bool IsValid() const { return Data && Count >= 0 && Count <= Capacity; }
    T* GetDataPtr() const { return Data; }
    T& operator[](int I) const { return Data[I]; }
};
static_assert(sizeof(TArray<UObject*>) == 16);
struct FString {
    std::string Text;
    bool IsValid() const { return true; }
    int Num() const { return static_cast<int>(Text.size()); }
    std::string ToString() const { return Text; }
};
struct FExplorerNoteEntry { int ExplorerNoteType = 0; bool bIsGenesis1 = false, bPreventXPBuff = false; FString ExplorerNoteDescription; };
struct UPrimalCharacterStatusComponent : UObject { float ReplicatedExperiencePoints = 100; };
inline UClass BuffClass;
struct APrimalBuff : UObject {
    bool bDeactivated = false;
    float DeactivateAfterTime = 600;
    static UClass* StaticClass() { return &BuffClass; }
};
struct AShooterCharacter : UObject {
    bool Dead = false;
    UPrimalCharacterStatusComponent* MyCharacterStatusComponent = nullptr;
    bool IsDead() const { return Dead; }
};
struct AShooterPlayerController : UObject {
    AShooterCharacter* Human = nullptr;
    UObject* PlayerState = nullptr;
    std::vector<int> Calls;
    std::function<void(int)> OnRPC;
    AShooterCharacter* GetPlayerCharacter() const { return Human; }
    void ServerUnlockPerMapExplorerNote(int I, bool Avoid, bool Save) {
        assert(!Avoid && Save); Calls.push_back(I); if (OnRPC) OnRPC(I);
    }
};
struct UWorld : UObject {};
struct UPrimalGameData : UObject {
    TArray<FExplorerNoteEntry> ExplorerNoteEntries;
    static inline UPrimalGameData* Current = nullptr;
    static inline std::set<int> Unlocked;
    static UPrimalGameData* BPGetGameData() { return Current; }
    static bool LocalIsPerMapExplorerNoteUnlocked(int I) { return Unlocked.contains(I); }
};
namespace Params {
struct PrimalCharacter_GetAllBuffs { TArray<APrimalBuff*> AllBuffs; };
struct PrimalGameData_GetExplorerNoteName { int NoteIndex = 0; FName ReturnValue; };
struct PrimalBuff_GetTimeLeft { float ReturnValue = 0; };
struct PrimalBuff_GetRemainingTime { float DeactivateAfterTimeOverride = 0, ReturnValue = 0; };
struct ShooterPlayerController_ServerUnlockPerMapExplorerNote { int ExplorerNoteIndex = 0; bool bAvoidBuff = false, SavePlayerData = false; };
}
}

namespace g_Hack {
SDK::AShooterPlayerController* NoteTestPC = nullptr;
bool NoteTestRawAvailable = false;
std::vector<SDK::UObject*> NoteTestRaw;
bool IsUsableObject(SDK::UObject* O) { return O && O->Live; }
struct ExtensionObject {
    SDK::UObject* Object = nullptr; int Serial = 0;
    ExtensionObject() = default;
    explicit ExtensionObject(SDK::UObject* O) : Object(O), Serial(O ? O->Serial : 0) {}
    SDK::UObject* Get() const { return IsUsableObject(Object) && Object->Serial == Serial ? Object : nullptr; }
    uintptr_t Key() const { return reinterpret_cast<uintptr_t>(Get()); }
};
SDK::AShooterPlayerController* GetLocalShooterController(SDK::UWorld*) { return NoteTestPC; }
SDK::FProperty* FindReflectedProperty(SDK::UStruct* Owner, const char* Key) {
    const auto I = Owner->Fields.find(Key); return I == Owner->Fields.end() ? nullptr : I->second;
}
bool EqualsNameIgnoreCase(const std::string& A, const char* B) { return _stricmp(A.c_str(), B) == 0; }
bool ReadReflectedObjectArray(SDK::UObject*, const char*, SDK::UObject*** Out, int* Count) {
    if (!NoteTestRawAvailable) return false;
    *Out = NoteTestRaw.data(); *Count = static_cast<int>(NoteTestRaw.size()); return true;
}
#include "../src/internal/Hack/NoteExperiment.inl"
}

struct NoteFixture {
    SDK::UWorld World, OtherWorld;
    SDK::UClass PCClass, HumanClass, DataClass;
    SDK::UFunction RPC, GetBuffs, TimeLeft, Remaining;
    SDK::FFieldClass ArrayType{{"ArrayProperty"}}, FloatType{{"FloatProperty"}};
    SDK::FProperty AllBuffsProperty, TimeReturn, Override, RemainingReturn;
    SDK::AShooterPlayerController PC;
    SDK::AShooterCharacter Human;
    SDK::UPrimalCharacterStatusComponent Status;
    SDK::UObject PlayerState;
    SDK::UPrimalGameData Data;
    SDK::APrimalBuff Buff;
    std::vector<SDK::FExplorerNoteEntry> Entries;
    std::vector<SDK::APrimalBuff*> Buffs;
    bool ReturnEmpty = false;
    int TimerCalls = 0, NativeBuffCalls = 0;

    NoteFixture() : Entries(12) {
        using namespace g_Hack;
        SetNoteExperimentOpen(false); ResetNoteExperiment();
        NoteExperiment.View = {}; PublishNoteExperiment();
        NoteExperimentBuffScratch = {};
        NoteTestClock += 10000;
        NoteTestPC = &PC; SDK::UPrimalGameData::Current = &Data;
        SDK::UPrimalGameData::Unlocked.clear();
        for (int I = 0; I < static_cast<int>(Entries.size()); ++I) {
            Entries[I].ExplorerNoteDescription.Text = "Note " + std::to_string(I);
            SDK::UPrimalGameData::Unlocked.insert(I);
        }
        SDK::UPrimalGameData::Unlocked.erase(1);
        for (int I : {0, 1, 2}) { Entries[I].ExplorerNoteType = 16; Entries[I].bIsGenesis1 = true; }
        Entries[2].bPreventXPBuff = Entries[4].bPreventXPBuff = true;
        Data.Class = &DataClass; Data.ExplorerNoteEntries = {Entries.data(), 12, 12};
        PC.Class = &PCClass; PC.Human = &Human; PC.PlayerState = &PlayerState; Human.Class = &HumanClass;
        Human.MyCharacterStatusComponent = &Status;
        RPC.Size = sizeof(SDK::Params::ShooterPlayerController_ServerUnlockPerMapExplorerNote);
        PCClass.Functions["ServerUnlockPerMapExplorerNote"] = &RPC;
        GetBuffs.Size = sizeof(SDK::Params::PrimalCharacter_GetAllBuffs);
        AllBuffsProperty = {&ArrayType, 0, 16, 1}; GetBuffs.Fields["AllBuffs"] = &AllBuffsProperty;
        HumanClass.Functions["GetAllBuffs"] = &GetBuffs;
        TimeLeft.Size = sizeof(SDK::Params::PrimalBuff_GetTimeLeft);
        TimeReturn = {&FloatType, 0, 4, 1}; TimeLeft.Fields["ReturnValue"] = &TimeReturn;
        Remaining.Size = sizeof(SDK::Params::PrimalBuff_GetRemainingTime);
        Override = {&FloatType, 0, 4, 1}; RemainingReturn = {&FloatType, 4, 4, 1};
        Remaining.Fields["DeactivateAfterTimeOverride"] = &Override;
        Remaining.Fields["ReturnValue"] = &RemainingReturn;
        SDK::BuffClass = {}; SDK::BuffClass.Name = "Buff_ExplorerNoteXP_C";
        SDK::BuffClass.Functions["GetTimeLeft"] = &TimeLeft;
        SDK::BuffClass.Functions["GetRemainingTime"] = &Remaining;
        Buff.Class = &SDK::BuffClass; Buff.Index = 71; Buffs = {&Buff};
        Buff.Event = [&](SDK::UFunction* F, void* P) {
            ++TimerCalls;
            if (F == &TimeLeft) static_cast<SDK::Params::PrimalBuff_GetTimeLeft*>(P)->ReturnValue = 499.0f;
            else { assert(F == &Remaining); auto* R = static_cast<SDK::Params::PrimalBuff_GetRemainingTime*>(P);
                assert(R->DeactivateAfterTimeOverride == -1.0f); R->ReturnValue = 498.0f; }
        };
        Human.Event = [&](SDK::UFunction* F, void* P) {
            assert(F == &GetBuffs); ++NativeBuffCalls;
            auto* R = static_cast<SDK::Params::PrimalCharacter_GetAllBuffs*>(P);
            R->AllBuffs = ReturnEmpty ? SDK::TArray<SDK::APrimalBuff*>{} : SDK::TArray<SDK::APrimalBuff*>{Buffs.data(), static_cast<int>(Buffs.size()), static_cast<int>(Buffs.size())};
        };
        NoteTestRawAvailable = false; NoteTestRaw.clear();
        SetNoteExperimentOpen(true); Tick();
        assert(GetNoteExperimentView().Ready);
    }
    ~NoteFixture() {
        g_Hack::SetNoteExperimentOpen(false); g_Hack::ResetNoteExperiment();
        g_Hack::NoteTestPC = nullptr; SDK::UPrimalGameData::Current = nullptr;
    }
    void Tick(uint64_t Advance = 0, bool Busy = false, SDK::UWorld* Which = nullptr) {
        NoteTestClock += Advance; g_Hack::NoteExperimentTick(Which ? Which : &World, Busy);
    }
    void Start(g_Hack::NoteExperimentScope Scope, int Index = -1) {
        g_Hack::RequestNoteExperiment(Scope, Index); Tick();
    }
};

int main() {
    using namespace g_Hack;
    {
        NoteFixture F;
        F.Start(NoteExperimentScope::Selected, 0);
        auto V = GetNoteExperimentView();
        assert((F.PC.Calls == std::vector<int>{0}) && V.Attempts[0].UnlockedBefore);
        assert(V.Running && V.Observing && !V.Attempts[0].HasAfter);
        RequestNoteExperiment(NoteExperimentScope::Selected, 1); F.Tick(4999);
        assert(F.PC.Calls.size() == 1 && GetNoteExperimentView().Running);
        F.Tick(1); // Next sample must be due before finalization; a slow sample schedules from now.
        if (GetNoteExperimentView().Running) F.Tick(249);
        V = GetNoteExperimentView();
        assert(!V.Running && V.Attempts[0].HasAfter && V.Attempts[0].UnlockedAfter);
        assert(V.Before.HasExperience && V.After.HasExperience && V.Before.Experience == V.After.Experience);
        const auto LogPath = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(V.LogPath.data()), V.LogPath.size()));
        assert(std::filesystem::exists(LogPath));
        std::ifstream Log(LogPath);
        const std::string Text((std::istreambuf_iterator<char>(Log)), {});
        assert(Text.find("client_observation_only") != std::string::npos);
        assert(Text.find("sent elapsed_ms=0 id=0 unlocked_before=1") != std::string::npos);
        assert(Text.find("has_after=1") != std::string::npos);
        F.Start(NoteExperimentScope::Selected, 0); assert(F.PC.Calls.size() == 2); // Explicit second round re-sends the same unlocked ID.
    }
    {
        NoteFixture F; F.Start(NoteExperimentScope::Selected, 4);
        assert((F.PC.Calls == std::vector<int>{4})); // Single-note controls may test a no-XP entry.
    }
    {
        NoteFixture F; F.Start(NoteExperimentScope::TidesOfFortune);
        assert((F.PC.Calls == std::vector<int>{0, 1})); // Both groups exclude no-XP entries; Selected can test them.
    }
    {
        NoteFixture F; F.Start(NoteExperimentScope::AllXPNotes);
        assert((F.PC.Calls == std::vector<int>{0, 1, 3, 5}));
        F.Tick(); F.Tick(99); assert(F.PC.Calls.size() == 4);
        F.Tick(1); assert(F.PC.Calls.size() == 8);
        F.Tick(5000); assert(F.PC.Calls.size() == 10 && GetNoteExperimentView().Observing);
        F.Tick(4999); assert(GetNoteExperimentView().Running);
        F.Tick(250); assert(!GetNoteExperimentView().Running);
        assert(std::find(F.PC.Calls.begin(), F.PC.Calls.end(), 2) == F.PC.Calls.end());
        assert(std::find(F.PC.Calls.begin(), F.PC.Calls.end(), 4) == F.PC.Calls.end());
    }
    {
        NoteFixture F;
        F.PC.OnRPC = [&](int) { RequestNoteExperiment(NoteExperimentScope::Selected, 4); };
        F.Start(NoteExperimentScope::Selected, 0); F.Tick(5000); F.Tick();
        assert(F.PC.Calls.size() == 1); // A click from inside final native dispatch cannot queue another round.
    }
    {
        NoteFixture F;
        F.PC.OnRPC = [&](int) { CancelNoteExperiment(); };
        F.Start(NoteExperimentScope::AllXPNotes); F.Tick(1000);
        assert(F.PC.Calls.size() == 1 && !GetNoteExperimentView().Running);
    }
    {
        NoteFixture F; F.Start(NoteExperimentScope::AllXPNotes);
        SetNoteExperimentOpen(false); F.Tick(100); assert(F.PC.Calls.size() == 4 && !GetNoteExperimentView().Running);
        RequestNoteExperiment(NoteExperimentScope::Selected, 0); F.Tick(); assert(F.PC.Calls.size() == 4);
    }
    {
        NoteFixture F; F.Start(NoteExperimentScope::AllXPNotes); ++F.Human.Serial; F.Tick(100);
        const auto V = GetNoteExperimentView();
        assert(F.PC.Calls.size() == 4 && !V.Running && !V.After.Valid);
        for (const auto& A : V.Attempts) assert(!A.HasAfter);
    }
    {
        NoteFixture F; RequestNoteExperiment(NoteExperimentScope::Selected, 0); F.Tick(0, false, &F.OtherWorld);
        assert(F.PC.Calls.empty()); // A queued click must not cross world/catalog generation.
    }
    {
        NoteFixture F; RequestNoteExperiment(NoteExperimentScope::Selected, 0); RefreshNoteExperiment(); F.Tick();
        assert(F.PC.Calls.empty());
        RequestNoteExperiment(NoteExperimentScope::Selected, 0); F.Tick(0, true); assert(F.PC.Calls.empty());
        F.Start(NoteExperimentScope::AllXPNotes); F.Tick(100, true);
        assert(F.PC.Calls.size() == 4 && !GetNoteExperimentView().Running);
    }
    {
        NoteFixture F;
        const auto DisplayedGeneration = GetNoteExperimentView().CatalogGeneration;
        RefreshNoteExperiment(); F.Tick();
        RequestNoteExperiment(NoteExperimentScope::Selected, 0, DisplayedGeneration);
        F.Tick();
        assert(F.PC.Calls.empty()); // UI snapshot predates a newer published catalog.
        RequestNoteExperiment(NoteExperimentScope::Selected, 0, GetNoteExperimentView().CatalogGeneration);
        F.Tick();
        assert(F.PC.Calls.size() == 1);
    }
    {
        NoteFixture F; auto S = CaptureNoteExperiment(&F.Human);
        assert(S.Valid && S.HasBuffs && S.HasExperience && S.Buffs.size() == 1);
        assert(S.Buffs[0].HasRemainingTime && S.Buffs[0].RemainingSeconds == 499);
        SDK::BuffClass.Functions.erase("GetTimeLeft");
        S = CaptureNoteExperiment(&F.Human); assert(S.Buffs[0].HasRemainingTime && S.Buffs[0].RemainingSeconds == 498);
        F.RemainingReturn.Offset = 0;
        S = CaptureNoteExperiment(&F.Human); assert(!S.Buffs[0].HasRemainingTime);
        F.RemainingReturn.Offset = 4; F.RemainingReturn.ArrayDim = 2;
        S = CaptureNoteExperiment(&F.Human); assert(!S.Buffs[0].HasRemainingTime);
        F.Status.ReplicatedExperiencePoints = std::numeric_limits<float>::quiet_NaN();
        F.HumanClass.Functions.erase("GetAllBuffs");
        S = CaptureNoteExperiment(&F.Human); assert(S.Valid && !S.HasExperience && !S.HasBuffs && S.Buffs.empty());
        F.HumanClass.Functions["GetAllBuffs"] = &F.GetBuffs; F.ReturnEmpty = true;
        S = CaptureNoteExperiment(&F.Human); assert(S.HasBuffs && S.Buffs.empty()); // Known empty differs from failed sampling.
        F.ReturnEmpty = false; F.Buff.bDeactivated = true;
        S = CaptureNoteExperiment(&F.Human); assert(S.HasBuffs && S.Buffs.empty());
        F.Buff.bDeactivated = false; F.Buff.DeactivateAfterTime = 0;
        S = CaptureNoteExperiment(&F.Human); assert(S.Buffs.size() == 1 && !S.Buffs[0].HasRemainingTime);
        F.Human.Dead = true; assert(!CaptureNoteExperiment(&F.Human).Valid);
    }
    {
        NoteFixture F; ++F.RPC.Size; F.Start(NoteExperimentScope::Selected, 0);
        assert(F.PC.Calls.empty() && !GetNoteExperimentView().Running);
        F.RPC.Size -= 1; F.Start(NoteExperimentScope::Selected, 999);
        assert(F.PC.Calls.empty() && !GetNoteExperimentView().Running);
    }
    {
        NoteFixture F; F.PC.PlayerState = nullptr;
        F.Start(NoteExperimentScope::Selected, 0);
        assert(F.PC.Calls.empty() && !GetNoteExperimentView().Ready);
    }
    std::cout << "Note experiment production dispatcher: repeat requests, scope controls, 4/100 ms batching, observation, cancellation, session isolation, non-reentry, missing telemetry and RPC ABI checks passed.\n";
}
