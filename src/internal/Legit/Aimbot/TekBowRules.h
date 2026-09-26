#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace g_Aimbot {
    inline std::optional<double> TekBowArrowSpeed(int AmmoIndex, int AmmoCount, double Pulling,
        double FullPull, double Minimum, double Maximum, double ExplosiveMin, double ExplosiveMax) {
        if (AmmoIndex < 0 || AmmoIndex >= AmmoCount || AmmoCount <= 0 || AmmoCount > 32 ||
            !std::isfinite(Pulling) || !std::isfinite(FullPull) || FullPull <= 0) return {};
        // Verified BPOnSpawnedProjectile: ammo index 1 overrides both initial
        // speed and max speed with these two blueprint fields after spawning.
        if (AmmoIndex == 1) { Minimum = ExplosiveMin; Maximum = ExplosiveMax; }
        if (!std::isfinite(Minimum) || !std::isfinite(Maximum) || Minimum <= 1 || Maximum < Minimum) return {};
        return Minimum + (Maximum - Minimum) * std::clamp(Pulling / FullPull, 0.0, 1.0);
    }
}
