#pragma once
#include <string>
namespace SDK { class UCanvas; }

namespace g_TekSpear {
    void Tick();
    void DrawOverlay(SDK::UCanvas* Canvas);
    // Reset runs on the game thread; disconnect callbacks only request it.
    bool Reset();
    void RequestReset();
    void RequestRetarget();
    bool IsDefending(); // Queried by automatic fire on the same game thread.
    std::string GetStatus();
    std::string GetProjectileStatus();
}
