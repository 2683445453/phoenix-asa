#pragma once
#include "FeatureShortcuts.h"
#include "../../external/CppSDK/SDK.hpp"

namespace PhoenixShortcuts {
    // Run only from the viewport callback on the game thread. Actor ticks may
    // run on workers; neither those workers nor WndProc may call UI ProcessEvent.
    inline bool BlockingInputQuery(SDK::UObject* Object, const char* Owner, const char* Name) {
        if (!Object || !Object->Class) return true;
        SDK::UFunction* Function = Object->Class->GetFunction(Owner, Name);
        // Generated SDK wrappers dereference a missing UFunction. Treat an
        // unavailable inventory/chat/input query as blocked instead.
        if (!Function) return true;
        struct { bool ReturnValue = true; } Parameters;
        Object->ProcessEvent(Function, &Parameters);
        return Parameters.ReturnValue;
    }

    inline bool GameplayInputReady(SDK::APlayerController* LocalPC, bool MenuOpen, bool FreeCamera) {
        if (MenuOpen || !LocalPC || !LocalPC->Class) return false;
        const auto* ControllerClass = SDK::AShooterPlayerController::StaticClass();
        if (!ControllerClass || !LocalPC->IsA(ControllerClass)) return false;
        // MainMenu uses a different controller. During travel/respawn the
        // controller can exist before its pawn and HUD are ready.
        if (!LocalPC->Pawn || !LocalPC->Pawn->Class || LocalPC->bShowMouseCursor) return false;
        auto* HUD = LocalPC->MyHUD;
        const auto* HUDClass = SDK::AShooterHUD::StaticClass();
        if (!HUD || !HUD->Class || !HUDClass || !HUD->IsA(HUDClass)) return false;
        if (BlockingInputQuery(LocalPC, "ShooterPlayerController", "IsViewingInventoryUI")) return false;
        if (!FreeCamera &&
            (BlockingInputQuery(LocalPC, "Controller", "IsMoveInputIgnored") ||
             BlockingInputQuery(LocalPC, "Controller", "IsLookInputIgnored"))) return false;
        return !BlockingInputQuery(HUD, "ShooterHUD", "ChatWindowHasFocus");
    }

    // Returns a stop acknowledgement so the caller can reset game-side state
    // on this same thread. Stops always win over queued toggles, even in menus.
    inline bool UpdateGameplay(SDK::APlayerController* LocalPC, bool MenuOpen, bool FreeCamera) {
        bool HasBindings = false;
        for (const auto& Action : Actions) HasBindings |= Action.Key != 0;
        const bool Ready = HasBindings && GameplayInputReady(LocalPC, MenuOpen, FreeCamera);
        GameplayReady.store(Ready);
        const unsigned Toggles = PendingToggles.exchange(0);
        if (StopRequested.exchange(false)) {
            StopAll();
            return true;
        }
        // Discard stale requests during travel or while entering a text field.
        if (Ready) for (size_t I = 0; I < std::size(Actions); ++I)
            if (Actions[I].Key && (Toggles & (1u << I))) *Actions[I].Enabled = !*Actions[I].Enabled;
        return false;
    }
}
