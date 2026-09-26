// Exercise the production shortcut runtime with real SDK layouts. Only class
// lookup and ProcessEvent are replaced; no game executable or injection is used.
#include "../src/internal/Menu/ShortcutRuntime.h"
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
    SDK::UClass ShooterClass{}, MenuClass{}, HUDClass{}, PawnClass{}, DerivedClass{};
    SDK::UFunction Queries[4]{};
    bool Available[4]{true,true,true,true}, Values[4]{};
    bool ClassesReady = false;
    int Lookups = 0, Calls = 0;
    const char* Names[]{"IsViewingInventoryUI", "IsMoveInputIgnored", "IsLookInputIgnored", "ChatWindowHasFocus"};
    void Process(const SDK::UObject*, SDK::UFunction* Function, void* Parameters) {
        assert(Function);
        const auto Index = Function - Queries;
        assert(Index >= 0 && Index < 4 && Available[Index]);
        ++Calls;
        *static_cast<bool*>(Parameters) = Values[Index];
    }
}
namespace SDK {
    UClass* BasicFilesImplUtils::FindClassByName(const std::string& Name, bool) {
        if (!ClassesReady) return nullptr;
        if (Name == "ShooterPlayerController") return &ShooterClass;
        if (Name == "ShooterHUD") return &HUDClass;
        assert(false); return nullptr;
    }
    bool UObject::IsA(const UClass* Type) const {
        for (const UStruct* Current = Class; Current; Current = Current->SuperStruct)
            if (Current == Type) return true;
        return false;
    }
    UFunction* UClass::GetFunction(const char* Owner, const char* Name) const {
        ++Lookups;
        // A main-menu controller must never reach an in-game UI query.
        assert(this != &MenuClass);
        for (int I = 0; I < 4; ++I) if (std::strcmp(Name, Names[I]) == 0) {
            assert(std::strcmp(Owner, I == 3 ? "ShooterHUD" : I == 0 ? "ShooterPlayerController" : "Controller") == 0);
            return Available[I] ? &Queries[I] : nullptr;
        }
        assert(false); return nullptr;
    }
}

int main() {
    using namespace PhoenixShortcuts;
    void* VTable[SDK::Offsets::ProcessEventIdx + 1]{};
    VTable[SDK::Offsets::ProcessEventIdx] = reinterpret_cast<void*>(&Process);
    static SDK::AShooterPlayerController Player{};
    static SDK::APlayerController MainMenu{};
    static SDK::AShooterHUD HUD{};
    static SDK::APawn Pawn{};
    DerivedClass.SuperStruct = &ShooterClass;
    Player.Class = &DerivedClass; Player.Pawn = &Pawn; Player.MyHUD = &HUD; Player.VTable = VTable;
    HUD.Class = &HUDClass; HUD.VTable = VTable; Pawn.Class = &PawnClass;
    MainMenu.Class = &MenuClass; MainMenu.Pawn = &Pawn; MainMenu.MyHUD = &HUD;
    assert(!GameplayInputReady(&Player, false, false)); // native class not loaded yet
    ClassesReady = true;
    for (auto& Action : Actions) Action.Key = 0;
    assert(!UpdateGameplay(&Player, false, false) && !GameplayReady && Lookups == 0);
    Actions[0].Key = 'K'; *Actions[0].Enabled = false;
    assert(!GameplayInputReady(nullptr, false, false));
    assert(!GameplayInputReady(&MainMenu, false, false));
    assert(Lookups == 0 && Calls == 0);
    Player.Pawn = nullptr; assert(!GameplayInputReady(&Player, false, false)); Player.Pawn = &Pawn;
    Player.MyHUD = nullptr; assert(!GameplayInputReady(&Player, false, false)); Player.MyHUD = &HUD;
    HUD.Class = &MenuClass; assert(!GameplayInputReady(&Player, false, false)); HUD.Class = &HUDClass;
    Player.Class = nullptr; assert(!GameplayInputReady(&Player, false, false)); Player.Class = &DerivedClass;
    assert(!GameplayInputReady(&Player, true, false));
    Player.bShowMouseCursor = true; assert(!GameplayInputReady(&Player, false, false)); Player.bShowMouseCursor = false;
    assert(Lookups == 0 && Calls == 0);
    assert(GameplayInputReady(&Player, false, false) && Calls == 4);
    for (int I = 0; I < 4; ++I) {
        Available[I] = false;
        assert(!GameplayInputReady(&Player, false, false)); // missing reflection never dispatched
        Available[I] = true; Values[I] = true;
        PendingToggles.store(1);
        assert(!UpdateGameplay(&Player, false, false) && !GameplayReady);
        assert(!*Actions[0].Enabled && PendingToggles == 0); // text/UI transition discards requests
        Values[I] = false;
    }
    Values[1] = Values[2] = true;
    assert(GameplayInputReady(&Player, false, true)); // free-camera input suppression is intentional
    Values[1] = Values[2] = false;
    PendingToggles.store(1); assert(!UpdateGameplay(&Player, false, false));
    assert(GameplayReady && *Actions[0].Enabled);
    PendingToggles.store(1); assert(!UpdateGameplay(&MainMenu, false, false));
    assert(!GameplayReady && PendingToggles == 0 && *Actions[0].Enabled);
    assert(!UpdateGameplay(&Player, false, false) && *Actions[0].Enabled); // no delayed toggle on rejoin
    RequestStop(); PendingToggles.store(1); assert(UpdateGameplay(&MainMenu, true, false));
    assert(!*Actions[0].Enabled && !StopRequested && PendingToggles == 0);
    assert(!UpdateGameplay(&Player, false, false) && !*Actions[0].Enabled);
    std::cout << "Shortcut runtime: main menu, loading/travel, missing functions, text/UI blocking, gameplay and stop priority passed.\n";
}
