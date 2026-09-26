#pragma once
#include <cmath>
#include <string_view>

namespace g_Hack {
    inline bool AutoLootBeaverDamAllowed(std::string_view Name) {
        return Name.find("BeaverDam") != std::string_view::npos ||
            Name.find("beaverdam") != std::string_view::npos ||
            Name.find("DenLogs") != std::string_view::npos ||
            Name.find("denlogs") != std::string_view::npos;
    }
    inline bool AutoLootWildEggAllowed(bool Wild, std::string_view Name) {
        return Wild && Name.starts_with("PrimalItemConsumable_Egg_") &&
            (Name.find("Wyvern") != std::string_view::npos || Name.find("Deinonychus") != std::string_view::npos);
    }

    inline bool AutoLootControlsHuman(const void* Pawn, const void* Human,
        const void* Mount, const void* Rider) {
        return Pawn && Human && (Pawn == Human || (Pawn == Mount && Rider == Human));
    }

    inline bool AutoLootWithinRange(double DistanceCm, float RangeMeters) {
        return std::isfinite(DistanceCm) && DistanceCm >= 0 &&
            std::isfinite(RangeMeters) && RangeMeters > 0 && DistanceCm <= RangeMeters * 100.0;
    }
}
