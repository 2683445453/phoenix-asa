#pragma once
#include <cstdint>
#include <string_view>

namespace BackpackRules {
    enum Resource : uint32_t { Metal=1, Stone=2, Berries=4, Mushrooms=8, Crystal=16,
        Fiber=32, Thatch=64, Wood=128, Ambergris=256, Flint=512, Obsidian=1024 };
    inline constexpr uint32_t All = 2047;
    // Match native classes (or their subclasses), never localized names or a
    // loose "metal" substring that would also discard ingots and equipment.
    inline uint32_t Classify(std::string_view Name) {
        if (Name == "PrimalItemResource_Metal_C") return Metal;
        if (Name == "PrimalItemResource_Stone_C") return Stone;
        if (Name.starts_with("PrimalItemConsumable_Berry_") && Name.ends_with("_C")) return Berries;
        if (Name == "PrimalItemResource_RareMushroom_C" ||
            (Name.starts_with("PrimalItemConsumable_Mushroom_") && Name.ends_with("_C"))) return Mushrooms;
        if (Name == "PrimalItemResource_Crystal_C") return Crystal;
        if (Name == "PrimalItemResource_Fibers_C") return Fiber;
        if (Name == "PrimalItemResource_Thatch_C") return Thatch;
        if (Name == "PrimalItemResource_Wood_C" || Name == "PrimalItemResource_FungalWood_C") return Wood;
        if (Name == "PrimalItemResource_Ambergris_C") return Ambergris;
        if (Name == "PrimalItemResource_Flint_C") return Flint;
        if (Name == "PrimalItemResource_Obsidian_C") return Obsidian;
        return 0;
    }
    inline bool CanDrop(uint32_t Category, uint32_t Selected, int Quantity,
        bool Blueprint, bool Engram, bool Equipped, bool Hotbar, bool Removable) {
        return (Category & Selected & All) != 0 && Quantity > 0 && !Blueprint && !Engram &&
            !Equipped && !Hotbar && Removable;
    }
    struct OwnedFlag {
        bool Original=false, Applied=false, Owned=false;
        bool Apply(bool Current, bool Desired) {
            if (!Owned || Current != Applied) Original = Current;
            Applied=Desired; Owned=true; return Desired;
        }
        bool Restore(bool Current) { const bool Result=Owned && Current==Applied ? Original : Current; Owned=false; return Result; }
    };
}
