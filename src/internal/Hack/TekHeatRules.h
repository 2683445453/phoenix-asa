#pragma once
#include <cstdint>
#include <cstring>
#include <span>
namespace PhoenixTek {
    // Verified Blueprint BPFireWeapon bodies contain only AddHeat(), or
    // AddHeat(true) for the sniper. Refuse any body with other gameplay work.
    inline bool IsHeatOnlyFireEvent(std::span<const uint8_t> Code, int32_t HeatName, bool Sniper) {
        if (Code.size() != (Sniper ? 18u : 17u) || Code[0] != 0x1b) return false;
        int32_t Name = 0; std::memcpy(&Name, Code.data() + 1, sizeof(Name));
        if (Name != HeatName) return false;
        for (int I = 5; I < 13; ++I) if (Code[I]) return false;
        if (Sniper && Code[13] != 0x27) return false;
        const auto End = Sniper ? 14u : 13u;
        return Code[End] == 0x16 && Code[End+1] == 0x04 && Code[End+2] == 0x0b && Code[End+3] == 0x53;
    }
}
