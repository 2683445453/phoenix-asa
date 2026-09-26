#pragma once
#include <cctype>
#include <string>

namespace g_Hack::TransferRules {
    enum class Destination { Grinder, Vault };
    struct Item {
        int Type = 0, Quantity = 0;
        bool Blueprint = false, Engram = false, Equipped = false, Hotbar = false;
        bool Skin = false, Removable = false, PreventGrinding = false;
    };
    inline bool Eligible(const Item& Value, Destination Target) {
        if (Value.Quantity <= 0 || !Value.Removable || Value.Engram || Value.Equipped ||
            Value.Hotbar || Value.Skin) return false;
        if (Target == Destination::Vault) return Value.Blueprint;
        return !Value.Blueprint && !Value.PreventGrinding &&
            (Value.Type == 1 || Value.Type == 2 || Value.Type == 4);
    }
    inline bool IsGrinderClass(std::string Name) {
        for (char& C : Name) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
        // Native industrial grinder and the QoL+ blueprint confirmed in its
        // source tree. Called for each ancestor, never for a renamed display label.
        return Name == "industrialgrinder_c" || Name == "storagebox_industrialgrinder_c" || Name == "bp_grinderplus_c";
    }
    inline bool IsVaultClass(std::string Name) {
        for (char& C : Name) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
        return Name == "storagebox_huge_c" || Name == "storagebox_huge_plus_c" ||
            Name == "bp_vaultplus_c";
    }
}
