// Offline regression for the soup-buff and fire-extinguish rules.
// Mirrors the runtime matching: normalized (lowercase, alnum-only) class chains.
#include "../src/internal/Hack/BuffSoupRules.h"
#include "../src/internal/Hack/FireExtinguishRules.h"
#include <cstdio>
#include <string>
#include <string_view>

namespace {
    int Failures = 0;
    void Check(bool Condition, const char* What) {
        if (!Condition) { ++Failures; std::printf("FAIL: %s\n", What); }
    }
    std::string ChainOf(const char* ClassName) { return g_Hack::NormalizeChain(ClassName); }
    // Runtime soup items derive from the BuffingSoup base; join like the class chain does.
    std::string SoupItemChain(const char* ClassName) {
        return ChainOf(ClassName) + " " + ChainOf("PrimalItemConsumableEatable_BuffingSoup_C") + " " +
            ChainOf("PrimalItemConsumableEatable_C");
    }
}

int main() {
    using g_Hack::SoupKind;

    // Normalization keeps the tokens lowercase and strips separators.
    Check(ChainOf("PrimalItemConsumable_Soup_BattleTartare_C").find("battletartare") != std::string::npos,
        "normalize lowercase battle tartare item");
    Check(ChainOf("PrimalItemConsumableEatable_BuffingSoup_C").find("buffingsoup") != std::string::npos,
        "normalize buffing soup base");

    // Buffing-soup family detection.
    Check(g_Hack::IsBuffingSoupChain(SoupItemChain("PrimalItemConsumable_Soup_FocalChili_C")),
        "focal chili is a buffing soup");
    Check(!g_Hack::IsBuffingSoupChain(ChainOf("PrimalItemConsumableEatable_WaterContainer_C")),
        "water container is not a soup");
    Check(!g_Hack::IsBuffingSoupChain(ChainOf("PrimalItemConsumable_BerrySoup_C")),
        "berry soup is not a buffing soup");

    // Soup classification for every supported type (live-verified class names).
    struct Sample { const char* Name; SoupKind Kind; };
    const Sample Samples[] = {
        {"PrimalItemConsumable_Soup_BattleTartare_C", SoupKind::BattleTartare},
        {"PrimalItemConsumable_CactusBuffSoup_C", SoupKind::CactusBroth},
        {"PrimalItemConsumable_Soup_CactusBroth_C", SoupKind::CactusBroth}, // 旧名别名
        {"PrimalItemConsumable_Soup_FocalChili_C", SoupKind::FocalChili},
        {"PrimalItemConsumable_Soup_FriaCurry_C", SoupKind::FriaCurry},
        {"PrimalItemConsumable_Soup_CalienSoup_C", SoupKind::CalienSoup},
        {"PrimalItemConsumable_Soup_LazarusChowder_C", SoupKind::LazarusChowder},
        {"PrimalItemConsumable_Soup_EnduroStew_C", SoupKind::EnduroStew},
        {"PrimalItemConsumable_Soup_ShadowSteak_C", SoupKind::ShadowSteak},
        {"PrimalItemConsumable_Soup_MushroomSoup_C", SoupKind::MushroomBrew},
    };
    for (const auto& S : Samples)
        Check(g_Hack::ClassifySoupChain(SoupItemChain(S.Name)) == S.Kind, S.Name);
    Check(g_Hack::ClassifySoupChain(ChainOf("PrimalItemConsumable_Soup_Unknown_C")) == SoupKind::Count,
        "unknown soup returns sentinel");
    Check(g_Hack::ClassifySoupChain(ChainOf("PrimalItemConsumable_CactusSap_C")) == SoupKind::Count,
        "cactus sap is not classified as cactus broth");

    // Buff tokens: exact buff classes count, unrelated buffs do not.
    Check(g_Hack::ChainContainsAnyToken(ChainOf("Buff_BattleTartare_C"),
        g_Hack::SoupBuffTokens(SoupKind::BattleTartare)), "battle tartare buff active");
    Check(!g_Hack::ChainContainsAnyToken(ChainOf("Buff_FocalChili_C"),
        g_Hack::SoupBuffTokens(SoupKind::BattleTartare)), "focal chili buff is not battle tartare");
    Check(g_Hack::ChainContainsAnyToken(ChainOf("Buff_CactusSoup_C"),
        g_Hack::SoupBuffTokens(SoupKind::CactusBroth)), "cactus broth buff active (live class name)");
    Check(g_Hack::ChainContainsAnyToken(ChainOf("Buff_PoisonResistance_C"),
        g_Hack::SoupBuffTokens(SoupKind::MushroomBrew)), "mushroom brew poison-resist alias");

    // Burning detection: fire-related buffs without the bare "fire" token.
    Check(g_Hack::IsBurningBuffChain(ChainOf("Buff_Burning_C")), "burning buff detected");
    Check(g_Hack::IsBurningBuffChain(ChainOf("Buff_Ignition_C")), "ignition buff detected");
    Check(g_Hack::IsBurningBuffChain(ChainOf("Buff_OnFire_C")), "on-fire buff detected");
    Check(!g_Hack::IsBurningBuffChain(ChainOf("Buff_FriaCurry_C")), "curry buff is not burning");
    Check(!g_Hack::IsBurningBuffChain(ChainOf("Buff_FireLionRoar_C")), "no bare fire token false positive");

    // Antifire grenade item matching.
    Check(g_Hack::IsAntifireGrenadeChain(ChainOf("PrimalItem_AntifireGrenade_C")), "antifire grenade matched");
    Check(!g_Hack::IsAntifireGrenadeChain(ChainOf("PrimalItem_WeaponGrenade_C")), "plain grenade is not antifire");
    Check(!g_Hack::IsAntifireGrenadeChain(ChainOf("PrimalItemConsumable_Soup_FocalChili_C")), "soup is not antifire grenade");

    // 投掷物瞬间扔出的物品识别：手雷类通过，榴弹发射器等排除。
    for (const char* Throwable : {"PrimalItem_WeaponGrenade_C", "PrimalItem_AntifireGrenade_C",
        "PrimalItem_GasGrenade_C", "PrimalItem_PoisonGrenade_C", "PrimalItem_TekGrenade_C",
        "PrimalItem_WeaponClusterGrenade_C", "PrimalItem_PlantSpeciesZ_Grenade_C"})
        Check(g_Hack::IsThrowableGrenadeChain(ChainOf(Throwable)), Throwable);
    Check(!g_Hack::IsThrowableGrenadeChain(ChainOf("PrimalItem_WeaponTekGrenadeLauncher_C")),
        "grenade launcher is not a throwable");
    Check(!g_Hack::IsThrowableGrenadeChain(ChainOf("PrimalItemConsumable_CactusBuffSoup_C")),
        "soup is not a throwable");

    if (Failures) { std::printf("%d check(s) failed.\n", Failures); return 1; }
    std::printf("buff_soup: all rule checks passed.\n");
    return 0;
}
