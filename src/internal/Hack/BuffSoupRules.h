#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace g_Hack {
    // 自动汤类增益的纯规则：类型识别、buff 检测令牌。不依赖 SDK 与配置，
    // 便于离线单测；调用方负责构造归一化（小写、去非字母数字）的类名链。
    enum class SoupKind {
        BattleTartare = 0,
        CactusBroth,
        FocalChili,
        FriaCurry,
        CalienSoup,
        LazarusChowder,
        EnduroStew,
        ShadowSteak,
        MushroomBrew,
        Count,
    };

    inline const char* SoupDisplayName(SoupKind Kind) {
        switch (Kind) {
            case SoupKind::BattleTartare: return "战斗牛排";
            case SoupKind::CactusBroth: return "仙人掌汤";
            case SoupKind::FocalChili: return "绯红辣椒";
            case SoupKind::FriaCurry: return "菲拉咖喱";
            case SoupKind::CalienSoup: return "卡琳汤";
            case SoupKind::LazarusChowder: return "拉撒路杂烩";
            case SoupKind::EnduroStew: return "耐力炖锅";
            case SoupKind::ShadowSteak: return "影子牛排";
            case SoupKind::MushroomBrew: return "蘑菇汤";
            default: return "未知汤类";
        }
    }

    // 物品类名中的主令牌（小写）。物品类如 PrimalItemConsumable_Soup_BattleTartare_C。
    // 实机核对：仙人掌汤的物品类名是 PrimalItemConsumable_CactusBuffSoup_C。
    inline const char* SoupItemToken(SoupKind Kind) {
        switch (Kind) {
            case SoupKind::BattleTartare: return "battletartare";
            case SoupKind::CactusBroth: return "cactusbuffsoup";
            case SoupKind::FocalChili: return "focalchili";
            case SoupKind::FriaCurry: return "friacurry";
            case SoupKind::CalienSoup: return "caliensoup";
            case SoupKind::LazarusChowder: return "lazaruschowder";
            case SoupKind::EnduroStew: return "endurostew";
            case SoupKind::ShadowSteak: return "shadowsteak";
            case SoupKind::MushroomBrew: return "mushroombrew";
            default: return "";
        }
    }

    // 物品类名可接受的令牌（含别名/旧名）。
    inline std::vector<std::string_view> SoupItemTokens(SoupKind Kind) {
        switch (Kind) {
            case SoupKind::CactusBroth: return {"cactusbuffsoup", "cactusbroth"};
            case SoupKind::MushroomBrew: return {"mushroombrew", "mushroomsoup"};
            default: return {SoupItemToken(Kind)};
        }
    }

    // Buff 类名中可接受的令牌（可能包含别名）。Buff 类如 Buff_BattleTartare_C。
    inline std::vector<std::string_view> SoupBuffTokens(SoupKind Kind) {
        switch (Kind) {
            case SoupKind::BattleTartare: return {"battletartare"};
            case SoupKind::CactusBroth: return {"cactusbroth", "cactussoup"};
            case SoupKind::FocalChili: return {"focalchili"};
            case SoupKind::FriaCurry: return {"friacurry"};
            case SoupKind::CalienSoup: return {"caliensoup"};
            case SoupKind::LazarusChowder: return {"lazaruschowder"};
            case SoupKind::EnduroStew: return {"endurostew"};
            case SoupKind::ShadowSteak: return {"shadowsteak"};
            case SoupKind::MushroomBrew: return {"mushroombrew", "poisonresist"};
            default: return {};
        }
    }

    inline bool ChainContainsAnyToken(std::string_view NormalizedClassChain,
        const std::vector<std::string_view>& Tokens) {
        for (const auto Token : Tokens)
            if (!Token.empty() && NormalizedClassChain.find(Token) != std::string_view::npos) return true;
        return false;
    }

    // 是否属于 buffing soup 物品基类链（PrimalItemConsumableEatable_BuffingSoup_C）。
    inline bool IsBuffingSoupChain(std::string_view NormalizedClassChain) {
        return NormalizedClassChain.find("buffingsoup") != std::string_view::npos;
    }

    // 按物品类名链识别汤类；未识别返回 Count 之外的哨兵值，调用方自行忽略。
    inline SoupKind ClassifySoupChain(std::string_view NormalizedClassChain) {
        for (int K = 0; K < static_cast<int>(SoupKind::Count); ++K) {
            const auto Kind = static_cast<SoupKind>(K);
            if (ChainContainsAnyToken(NormalizedClassChain, SoupItemTokens(Kind))) return Kind;
        }
        return SoupKind::Count;
    }

    // 单个类名/对象名的归一化：小写并去掉非字母数字（与运行时类名链一致）。
    inline std::string NormalizeChain(std::string_view Raw) {
        std::string Out;
        Out.reserve(Raw.size());
        for (const char C : Raw) {
            const unsigned char U = static_cast<unsigned char>(C);
            if ((U >= 'a' && U <= 'z') || (U >= '0' && U <= '9')) Out += C;
            else if (U >= 'A' && U <= 'Z') Out += static_cast<char>(U - 'A' + 'a');
            else Out += ' ';
        }
        return Out;
    }
}
