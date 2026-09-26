#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace g_ESP::Structures {
    enum class Type { AutoTurret, HeavyTurret, TekTurret, Generator, TekGenerator, WindTurbine,
        SupplyDrop, LootCrate, BuriedTreasure, Other };

    // Prefer blueprint/item class identities. Display names are only a fallback
    // for structures whose blueprint identifiers are not part of the base game.
    inline Type Classify(std::string_view Identity) {
        std::string Key;
        Key.reserve(Identity.size());
        for (unsigned char Ch : Identity) {
            if (Ch >= 'A' && Ch <= 'Z') Ch += 'a' - 'A';
            if ((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9') || Ch >= 0x80)
                Key.push_back(static_cast<char>(Ch));
        }
        const auto Has = [&](std::string_view Part) { return Key.find(Part) != std::string::npos; };
        if (Has("buriedtreasure") || Has("treasurecache") || Has("treasurechest") ||
            Has("chesttreasure") ||
            Has("埋藏宝箱") || Has("藏宝箱")) return Type::BuriedTreasure;
        if (Has("beaverdam") || Has("denlogs")) return Type::Other;
        if (Has("supplycrate") || Has("lootcrate") || Has("空投") || Has("补给箱") || Has("補給箱")) {
            if (Has("cave") || Has("ocean") || Has("deepsea") || Has("underwater") ||
                Has("lootcrate") || Has("洞穴") || Has("海底")) return Type::LootCrate;
            return Type::SupplyDrop;
        }
        if (Has("tekturret") || Has("turrettek") || Has("泰克炮") || Has("泰克砲")) return Type::TekTurret;
        if (Has("heavyturret") || Has("turretheavy") || Has("heavyautoturret") ||
            Has("重型自动炮") || Has("重型自動砲") || Has("重型自動炮")) return Type::HeavyTurret;
        if (Has("tekgenerator") || Has("generatortek") || Has("泰克发电机") || Has("泰克發電機")) return Type::TekGenerator;
        if (Has("windturbine") || Has("windgenerator") || Has("风力发电机") || Has("風力發電機")) return Type::WindTurbine;
        if (Has("electricgenerator") || Has("electricalgenerator") || Has("powergenerator") ||
            Key == "generator" || Key == "发电机" || Key == "發電機") return Type::Generator;
        if (Has("autoturret") || Key == "storageboxturretc" || Key == "primalitemstructureturretc" ||
            Has("自动炮") || Has("自動砲") || Has("自動炮")) return Type::AutoTurret;
        return Type::Other;
    }

    struct Filter {
        bool Enabled = false;
        std::array<bool, 10> Selected{true, true, true, true, true, true,
            true, true, true, false};
        bool Allows(Type Kind) const {
            const auto Index = static_cast<std::size_t>(Kind);
            return !Enabled || (Index < Selected.size() && Selected[Index]);
        }
    };
}
