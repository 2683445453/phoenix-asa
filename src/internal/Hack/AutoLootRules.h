#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>

namespace g_Hack {
    enum class AutoLootTransferAction { Wait, Refresh, Transfer, Complete, Timeout };

    struct AutoLootTransferSession {
        using Clock = std::chrono::steady_clock;
        Clock::time_point Started{}, Transferred{}, NextPoll{}, NextRefresh{};
        std::chrono::milliseconds RefreshEvery{750};
        bool Active = false, TransferSent = false;
        int BeforeCount = 0;

        void Begin(Clock::time_point Now, int IntervalMs) {
            *this = {};
            Active = true;
            Started = NextPoll = Now;
            RefreshEvery = std::chrono::milliseconds(std::clamp(IntervalMs, 250, 1000));
            NextRefresh = Now + RefreshEvery;
        }
        bool Ready(Clock::time_point Now) const { return Active && Now >= NextPoll; }
        AutoLootTransferAction Step(Clock::time_point Now, int ItemCount) {
            if (!Ready(Now)) return AutoLootTransferAction::Wait;
            NextPoll = Now + std::chrono::milliseconds(100);
            if (TransferSent && ItemCount >= 0 && ItemCount < BeforeCount) {
                Active = false;
                return AutoLootTransferAction::Complete;
            }
            if (!TransferSent && ItemCount > 0 && ItemCount <= 10000) {
                BeforeCount = ItemCount;
                TransferSent = true;
                Transferred = Now;
                NextRefresh = Now + RefreshEvery;
                return AutoLootTransferAction::Transfer;
            }
            const bool Expired = TransferSent ? Now - Transferred >= std::chrono::seconds(3)
                : Now - Started >= std::chrono::seconds(6);
            if (Expired) {
                Active = false;
                return AutoLootTransferAction::Timeout;
            }
            if (Now >= NextRefresh) {
                NextRefresh = Now + RefreshEvery;
                return AutoLootTransferAction::Refresh;
            }
            return AutoLootTransferAction::Wait;
        }
    };

    inline int AutoLootFailureDelayMs(unsigned Failures) {
        return 500 << std::min(Failures > 0 ? Failures - 1 : 0u, 3u);
    }

    inline bool AutoLootSpecialContainerAllowed(std::string_view Name) {
        return Name.find("ArtifactCrate") != std::string_view::npos ||
            Name.find("ArtifactContainer") != std::string_view::npos ||
            Name.find("BuriedTreasure") != std::string_view::npos ||
            Name.find("TreasureChest") != std::string_view::npos ||
            Name.find("TreasureCache") != std::string_view::npos;
    }

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
        const void* Mount, const void* Rider, const void* HumanMount = nullptr) {
        // During possession replication the mount's rider can briefly be empty.
        // Accept the survivor's validated reciprocal reference, never a conflicting rider.
        return Pawn && Human && (Pawn == Human || (Pawn == Mount &&
            (Rider == Human || (!Rider && HumanMount == Mount))));
    }

    template<class NativeViewCheck>
    inline bool AutoLootCanRequestContainer(bool Mounted, NativeViewCheck Check) {
        // The mounted UI precheck can reject before any RPC is sent. As with
        // mounted egg pickup, leave actual access validation to the native server path.
        return Mounted || Check();
    }

    inline bool AutoLootInventoryUIBlocks(bool InventoryUI, bool MouseCursor,
        const void* Viewing, const void* AutomaticTarget) {
        return MouseCursor || (InventoryUI &&
            (!Viewing || !AutomaticTarget || Viewing != AutomaticTarget));
    }

    inline bool AutoLootInventoryViewBusy(const void* Viewing, const void* Target,
        const void* ClosingTarget, bool ClosePending) {
        return Viewing && Viewing != Target && (!ClosePending || Viewing != ClosingTarget);
    }

    inline float AutoLootAccessRange(float SearchRangeMeters, double RiderOffsetCm) {
        // Search from the possessed pawn, but native inventory code may measure
        // from the survivor seated above it. Cover both origins for large mounts.
        return SearchRangeMeters + (std::isfinite(RiderOffsetCm) && RiderOffsetCm > 0 &&
            RiderOffsetCm <= 10000 ? static_cast<float>(RiderOffsetCm * .01) : 0.0f);
    }

    inline bool AutoLootWithinRange(double DistanceCm, float RangeMeters) {
        return std::isfinite(DistanceCm) && DistanceCm >= 0 &&
            std::isfinite(RangeMeters) && RangeMeters > 0 && DistanceCm <= RangeMeters * 100.0;
    }
}
