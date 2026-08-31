// ESP.cpp
#include "../../external/CppSDK/SDK.hpp"
#include "../../external/Shadow-Gui/include/Shadow.h"
#include "ESP.h"
#include "../Config/Configs.h"
#include "../Util/Util.h"
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <locale>
#include <codecvt>
#include <limits>
#include <array>

/*
namespace {
    // 辅助绘制实心矩形的方法，利用Canvas的默认贴图
    void DrawFilledRect(SDK::UCanvas* Canvas, const SDK::FVector2D& Pos, const SDK::FVector2D& Size, const SDK::FLinearColor& Color) {
        if (!Canvas || !Canvas->DefaultTexture) return;
        Canvas->K2_DrawTexture(Canvas->DefaultTexture, Pos, Size, SDK::FVector2D{ 0, 0 }, SDK::FVector2D{ 1, 1 }, Color, SDK::EBlendMode::BLEND_Translucent, 0.0f, SDK::FVector2D{ 0, 0 });
    }
}
*/

namespace g_ESP {
    struct LabelRect {
        float minX;
        float minY;
        float maxX;
        float maxY;
    };

    static std::vector<LabelRect> s_occupiedTopLabels;

    static bool ReserveTopLabel(const LabelRect& Candidate) {
        constexpr float Margin = 3.0f;
        for (const LabelRect& Occupied : s_occupiedTopLabels) {
            const bool Separated = Candidate.maxX + Margin < Occupied.minX ||
                Candidate.minX - Margin > Occupied.maxX ||
                Candidate.maxY + Margin < Occupied.minY ||
                Candidate.minY - Margin > Occupied.maxY;
            if (!Separated) return false;
        }
        s_occupiedTopLabels.push_back(Candidate);
        return true;
    }

    void BeginLabelFrame() {
        s_occupiedTopLabels.clear();
    }

    RelationType GetRelation(SDK::APrimalCharacter* TargetChar, SDK::APrimalCharacter* LocalChar) {
        if (!TargetChar || !LocalChar) return RelationType::Enemy;

        int targetTeam = TargetChar->TargetingTeam;
        int localTeam = LocalChar->TargetingTeam;

        if (targetTeam != 0 && targetTeam == localTeam) {
            return RelationType::Team;
        }

        // During possession/replication transitions a character can briefly
        // expose team 0. TribeName remains replicated and is a safe fallback.
        if (targetTeam == 0 || localTeam == 0) {
            const std::string targetTribe = TargetChar->TribeName.ToString();
            const std::string localTribe = LocalChar->TribeName.ToString();
            if (!targetTribe.empty() && targetTribe != "None" &&
                targetTribe == localTribe) {
                return RelationType::Team;
            }
        }

        return RelationType::Enemy;
    }

    void BarManager::AddBar(SDK::UCanvas* Canvas, BoxRect rect, float currentValue, float maxValue, Shadow::Color color, BarPos pos, BarOrientation orientation, float a) {
        if (!rect.valid || maxValue <= 0 || !Canvas) return;

        Shadow::ShadowDrawList* dl = Shadow::GetBackgroundDrawList();
        if (!dl) return;

        float percentage = std::clamp(currentValue / maxValue, 0.0f, 1.0f);

        Shadow::Color bgColor = { 0.0f, 0.0f, 0.0f, a * 0.7f };
        color.a = a;

        if (pos == BarPos::Left && orientation == BarOrientation::Vertical) {
            float boxHeight = rect.bottomRight.y - rect.topLeft.y;
            float barWidth = 2.5f;
            float barMargin = 4.0f + leftOffset;

            Shadow::Vec2 barBgTop = { rect.topLeft.x - barMargin - barWidth, rect.topLeft.y };
            Shadow::Vec2 barBgBottom = { rect.topLeft.x - barMargin, rect.bottomRight.y };

            Shadow::Vec2 bgPos = { barBgTop.x - 1.0f, barBgTop.y - 1.0f };
            Shadow::Vec2 bgSize = { (barBgBottom.x + 1.0f) - bgPos.x, (barBgBottom.y + 1.0f) - bgPos.y };
            dl->AddRectFilled(bgPos, bgSize, bgColor);

            if (currentValue > 0) {
                float dynamicHeight = boxHeight * percentage;
                Shadow::Vec2 fgPos = { barBgTop.x, barBgBottom.y - dynamicHeight };
                Shadow::Vec2 fgSize = { barWidth, dynamicHeight };
                dl->AddRectFilled(fgPos, fgSize, color);
            }

            leftOffset += barWidth + 2.0f;
        }
        else if (pos == BarPos::Right && orientation == BarOrientation::Vertical) {
            float boxHeight = rect.bottomRight.y - rect.topLeft.y;
            float barWidth = 2.5f;
            float barMargin = 4.0f + rightOffset;

            Shadow::Vec2 barBgTop = { rect.bottomRight.x + barMargin, rect.topLeft.y };
            Shadow::Vec2 barBgBottom = { rect.bottomRight.x + barMargin + barWidth, rect.bottomRight.y };

            Shadow::Vec2 bgPos = { barBgTop.x - 1.0f, barBgTop.y - 1.0f };
            Shadow::Vec2 bgSize = { (barBgBottom.x + 1.0f) - bgPos.x, (barBgBottom.y + 1.0f) - bgPos.y };
            dl->AddRectFilled(bgPos, bgSize, bgColor);

            if (currentValue > 0) {
                float dynamicHeight = boxHeight * percentage;
                Shadow::Vec2 fgPos = { barBgTop.x, barBgBottom.y - dynamicHeight };
                Shadow::Vec2 fgSize = { barWidth, dynamicHeight };
                dl->AddRectFilled(fgPos, fgSize, color);
            }

            rightOffset += barWidth + 2.0f;
        }
        else if (pos == BarPos::Top && orientation == BarOrientation::Horizontal) {
            float boxWidth = rect.bottomRight.x - rect.topLeft.x;
            float barHeight = 2.5f;
            float barMargin = 4.0f + topOffset;

            Shadow::Vec2 barBgLeft = { rect.topLeft.x, rect.topLeft.y - barMargin - barHeight };
            Shadow::Vec2 barBgRight = { rect.bottomRight.x, rect.topLeft.y - barMargin };

            Shadow::Vec2 bgPos = { barBgLeft.x - 1.0f, barBgLeft.y - 1.0f };
            Shadow::Vec2 bgSize = { (barBgRight.x + 1.0f) - bgPos.x, (barBgRight.y + 1.0f) - bgPos.y };
            dl->AddRectFilled(bgPos, bgSize, bgColor);

            if (currentValue > 0) {
                float dynamicWidth = boxWidth * percentage;
                Shadow::Vec2 fgPos = { barBgLeft.x, barBgLeft.y };
                Shadow::Vec2 fgSize = { dynamicWidth, barHeight };
                dl->AddRectFilled(fgPos, fgSize, color);
            }

            topOffset += barHeight + 2.0f;
        }
        else if (pos == BarPos::Bottom && orientation == BarOrientation::Horizontal) {
            float boxWidth = rect.bottomRight.x - rect.topLeft.x;
            float barHeight = 2.5f;
            float barMargin = 4.0f + bottomOffset;

            Shadow::Vec2 barBgLeft = { rect.topLeft.x, rect.bottomRight.y + barMargin };
            Shadow::Vec2 barBgRight = { rect.bottomRight.x, rect.bottomRight.y + barMargin + barHeight };

            Shadow::Vec2 bgPos = { barBgLeft.x - 1.0f, barBgLeft.y - 1.0f };
            Shadow::Vec2 bgSize = { (barBgRight.x + 1.0f) - bgPos.x, (barBgRight.y + 1.0f) - bgPos.y };
            dl->AddRectFilled(bgPos, bgSize, bgColor);

            if (currentValue > 0) {
                float dynamicWidth = boxWidth * percentage;
                Shadow::Vec2 fgPos = { barBgLeft.x, barBgLeft.y };
                Shadow::Vec2 fgSize = { dynamicWidth, barHeight };
                dl->AddRectFilled(fgPos, fgSize, color);
            }

            bottomOffset += barHeight + 2.0f;
        }
    }

    void FlagManager::AddFlag(SDK::UCanvas* Canvas, BoxRect rect, const std::string& text, Shadow::Color color, FlagPos pos, float alphaMult, const BarManager* barMgr) {
        if (!rect.valid || text.empty() || !Canvas) return;

        Shadow::ShadowDrawList* dl = Shadow::GetBackgroundDrawList();
        if (!dl) return;

        static SDK::UFont* SansationBold18 = g_Util::GetSansationBold18();
        static SDK::UFont* OpenSansRegular12 = g_Util::GetOpenSansRegular12();

        if (!SansationBold18 || !OpenSansRegular12) return;
        SDK::UFont* currentFont = (pos == FlagPos::Top) ? SansationBold18 : OpenSansRegular12;

        // 获取精确宽高
        Shadow::PushFont(currentFont, g_Config::ESPScale);
        Shadow::Vec2 textSize = Shadow::MeasureTextSize(text);
        Shadow::PopFont();

        Shadow::Vec2 drawPos;
        bool labelAboveBox = false;
        bool anchoredLabel = false;

        // 【关键修复2】：补全全部四个方向的 barOffset（解决文字和血条重叠的问题）
        if (pos == FlagPos::Right) {
            float barOffset = barMgr ? barMgr->GetRightOffset() : 0.0f;
            drawPos = { (float)(rect.bottomRight.x + 5.0f + barOffset), (float)(rect.topLeft.y + rightY) };
            rightY += textSize.y + 1.0f;
        }
        else if (pos == FlagPos::Left) {
            float barOffset = barMgr ? barMgr->GetLeftOffset() : 0.0f;
            drawPos = { (float)(rect.topLeft.x - 6.0f - barOffset - textSize.x), (float)(rect.topLeft.y + leftY) };
            leftY += textSize.y + 1.0f;
        }
        else if (pos == FlagPos::Top) {
            float barOffset = barMgr ? barMgr->GetTopOffset() : 0.0f;
            float centerX = (rect.topLeft.x + rect.bottomRight.x) * 0.5f;
            drawPos = { (float)(centerX - textSize.x * 0.5f), (float)(rect.topLeft.y - barOffset - topY - textSize.y - 3.0f) };
            Shadow::ShadowIO& IO = Shadow::GetIO();
            drawPos.x = std::clamp(drawPos.x, 2.0f,
                std::max(2.0f, IO.DisplaySize.x - textSize.x - 2.0f));
            if (drawPos.y < 2.0f) {
                drawPos.y = rect.bottomRight.y + barOffset + topY + 3.0f;
            }
            else {
                labelAboveBox = true;
            }
            anchoredLabel = true;
            topY += textSize.y + 1.0f;

            const LabelRect LabelBounds{
                drawPos.x - 3.0f,
                drawPos.y - 1.0f,
                drawPos.x + textSize.x + 3.0f,
                drawPos.y + textSize.y + 1.0f
            };
            // Keep the label attached to its target. If a farther target would
            // overlap an already reserved label, omit it instead of moving it
            // to an ambiguous free-floating position.
            if (!ReserveTopLabel(LabelBounds)) return;
        }
        else if (pos == FlagPos::Bottom) {
            const bool firstBottomLabel = bottomY <= 0.0f;
            float barOffset = barMgr ? barMgr->GetBottomOffset() : 0.0f;
            float centerX = (rect.topLeft.x + rect.bottomRight.x) * 0.5f;
            drawPos = { (float)(centerX - textSize.x * 0.5f), (float)(rect.bottomRight.y + barOffset + bottomY + 3.0f) };
            if (firstBottomLabel) {
                Shadow::ShadowIO& IO = Shadow::GetIO();
                drawPos.x = std::clamp(drawPos.x, 2.0f,
                    std::max(2.0f, IO.DisplaySize.x - textSize.x - 2.0f));
                const LabelRect LabelBounds{
                    drawPos.x - 3.0f,
                    drawPos.y - 1.0f,
                    drawPos.x + textSize.x + 3.0f,
                    drawPos.y + textSize.y + 1.0f
                };
                if (!ReserveTopLabel(LabelBounds)) return;
                anchoredLabel = true;
            }
            bottomY += textSize.y + 1.0f;
        }

        color.a *= alphaMult;
        Shadow::Color shadowCol = { 0.0f, 0.0f, 0.0f, color.a };

        if (anchoredLabel) {
            const float boxCenterX = (rect.topLeft.x + rect.bottomRight.x) * 0.5f;
            const float labelCenterX = drawPos.x + textSize.x * 0.5f;
            const float boxAnchorY = labelAboveBox ? rect.topLeft.y : rect.bottomRight.y;
            const float labelAnchorY = labelAboveBox
                ? drawPos.y + textSize.y + 1.0f : drawPos.y - 1.0f;
            Shadow::Color leaderShadow{ 0.0f, 0.0f, 0.0f, color.a * 0.9f };
            Shadow::Color leaderColor = color;
            leaderColor.a *= 0.8f;
            dl->AddLine({ boxCenterX, boxAnchorY }, { labelCenterX, labelAnchorY },
                leaderShadow, 3.0f);
            dl->AddLine({ boxCenterX, boxAnchorY }, { labelCenterX, labelAnchorY },
                leaderColor, 1.0f);

            const Shadow::Color labelBackground{ 0.0f, 0.0f, 0.0f, color.a * 0.55f };
            dl->AddRectFilled({ drawPos.x - 3.0f, drawPos.y - 1.0f },
                { textSize.x + 6.0f, textSize.y + 2.0f }, labelBackground);
        }

        // 渲染文字
        dl->AddText(currentFont, g_Config::ESPScale, shadowCol, shadowCol, drawPos, color, text);
        // Canvas->K2_DrawText(currentFont, fText, drawPos, scaleSize, color, 0.0f, shadowCol, SDK::FVector2D{ 1.0f, 1.0f }, false, false, true, shadowCol);
    }

    BoxRect GetBox(SDK::UCanvas* Canvas, SDK::AActor* entity, float width_scale,
        bool useCharacterMeshBounds) {
        BoxRect rect;
        rect.valid = false;
        if (!Canvas || !entity || entity->bHidden) return rect;
        auto PC = g_Util::GetLocalPC();
        if (!PC) return rect;

        std::array<SDK::FVector, 8> worldCorners{};
        int worldCornerCount = 0;

        const auto IsFiniteVector = [](const SDK::FVector& Value) {
            return std::isfinite(Value.X) && std::isfinite(Value.Y) &&
                std::isfinite(Value.Z);
            };
        const auto HasValidExtent = [&](const SDK::FVector& Extent) {
            return IsFiniteVector(Extent) && Extent.X > 0.01 &&
                Extent.Y > 0.01 && Extent.Z > 0.01;
            };
        const auto FillAxisAlignedCorners = [&](const SDK::FVector& Origin,
            const SDK::FVector& Extent) {
                if (!IsFiniteVector(Origin) || !HasValidExtent(Extent)) return false;
                int CornerIndex = 0;
                for (int XSign : { -1, 1 }) {
                    for (int YSign : { -1, 1 }) {
                        for (int ZSign : { -1, 1 }) {
                            worldCorners[CornerIndex++] = SDK::FVector{
                                Origin.X + Extent.X * static_cast<double>(XSign),
                                Origin.Y + Extent.Y * static_cast<double>(YSign),
                                Origin.Z + Extent.Z * static_cast<double>(ZSign)
                            };
                        }
                    }
                }
                worldCornerCount = CornerIndex;
                return true;
            };
        // Ask Unreal for the skeletal component's current world-space bounds.
        // Transforming the skeletal asset's reference bounds here is unreliable:
        // some ASA dinos apply extra mesh/root-bone offsets that are not represented
        // by the component transform, which moves the entire ESP box off the model.
        bool usedMeshBounds = false;
        if (useCharacterMeshBounds && entity->IsA(SDK::ACharacter::StaticClass())) {
            SDK::ACharacter* Character = static_cast<SDK::ACharacter*>(entity);
            SDK::USkeletalMeshComponent* Mesh = Character->Mesh;
            if (Mesh && Mesh->Class) {
                SDK::FVector MeshOrigin{};
                SDK::FVector MeshExtent{};
                float SphereRadius = 0.0f;
                SDK::UKismetSystemLibrary::GetComponentBounds(
                    Mesh, &MeshOrigin, &MeshExtent, &SphereRadius);
                if (std::isfinite(SphereRadius) && SphereRadius > 0.01f) {
                    usedMeshBounds = FillAxisAlignedCorners(MeshOrigin, MeshExtent);
                }
            }

            if (!usedMeshBounds) {
                SDK::UCapsuleComponent* Capsule = Character->CapsuleComponent;
                if (Capsule && Capsule->Class) {
                    const float Radius = Capsule->GetScaledCapsuleRadius();
                    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
                    if (std::isfinite(Radius) && std::isfinite(HalfHeight)) {
                        usedMeshBounds = FillAxisAlignedCorners(
                            Capsule->K2_GetComponentLocation(),
                            SDK::FVector{ Radius, Radius, HalfHeight });
                    }
                }
            }
        }
        if (!usedMeshBounds) {
            SDK::FVector Origin{};
            SDK::FVector Extent{};
            entity->GetActorBounds(true, &Origin, &Extent, false);
            if (!FillAxisAlignedCorners(Origin, Extent)) return rect;
        }

        // Project all eight AABB corners. Projecting only the top and bottom
        // center points and guessing width from height shifts boxes whenever
        // the camera or a long/rotated creature changes perspective.
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        int projectedCorners = 0;
        for (int CornerIndex = 0; CornerIndex < worldCornerCount; ++CornerIndex) {
            const SDK::FVector& Corner = worldCorners[CornerIndex];
            if (!IsFiniteVector(Corner)) continue;
            SDK::FVector2D screen{};
            if (!PC->ProjectWorldLocationToScreen(Corner, &screen, false) ||
                !std::isfinite(screen.X) || !std::isfinite(screen.Y)) {
                continue;
            }
            const float screenX = static_cast<float>(screen.X);
            const float screenY = static_cast<float>(screen.Y);
            minX = std::min(minX, screenX);
            minY = std::min(minY, screenY);
            maxX = std::max(maxX, screenX);
            maxY = std::max(maxY, screenY);
            ++projectedCorners;
        }

        if (projectedCorners >= 2 && maxX - minX > 1.0f && maxY - minY > 1.0f) {
            const float height = maxY - minY;
            const float minimumWidth = height * std::max(0.1f, width_scale);
            if (maxX - minX < minimumWidth) {
                const float centerX = (minX + maxX) * 0.5f;
                minX = centerX - minimumWidth * 0.5f;
                maxX = centerX + minimumWidth * 0.5f;
            }
            rect.topLeft = { minX, minY };
            rect.bottomRight = { maxX, maxY };
            rect.valid = true;
        }

        return rect;
    }

    void DrawBox(SDK::UCanvas* Canvas, const BoxRect& rect, Shadow::Color color, float alpha) {
        if (!rect.valid || alpha < 0.001f || !Canvas) return;

        Shadow::ShadowDrawList* dl = Shadow::GetBackgroundDrawList();
        if (!dl) return;

        color.a *= alpha;

        float x = rect.topLeft.x;
        float y = rect.topLeft.y;
        float w = rect.bottomRight.x - rect.topLeft.x;
        float h = rect.bottomRight.y - rect.topLeft.y;
        float thickness = 1.0f; // 边框线条粗细

        // 1. 绘制黑色外边框阴影
        Shadow::Color shadowCol = { 0.0f, 0.0f, 0.0f, color.a };
        float s_t = thickness + 2.0f; // 阴影比主线稍微粗一点点
        dl->AddRectFilled( { x - 1.0f, y - 1.0f }, { w + 2.0f, s_t }, shadowCol); // 顶
        dl->AddRectFilled( { x - 1.0f, y + h - thickness - 1.0f }, { w + 2.0f, s_t }, shadowCol); // 底
        dl->AddRectFilled( { x - 1.0f, y - 1.0f }, { s_t, h + 2.0f }, shadowCol); // 左
        dl->AddRectFilled( { x + w - thickness - 1.0f, y - 1.0f }, { s_t, h + 2.0f }, shadowCol); // 右

        // 2. 绘制彩色主边框
        dl->AddRectFilled( { x, y }, { w, thickness }, color); // 顶
        dl->AddRectFilled( { x, y + h - thickness }, { w, thickness }, color); // 底
        dl->AddRectFilled( { x, y }, { thickness, h }, color); // 左
        dl->AddRectFilled( { x + w - thickness, y }, { thickness, h }, color); // 右
    }

    void DrawHealthBar(SDK::UCanvas* Canvas, BoxRect rect, float healthPercent, float maxHealth, float a) {
        if (!rect.valid || maxHealth <= 0 || !Canvas) return;

        Shadow::ShadowDrawList* dl = Shadow::GetBackgroundDrawList();
        if (!dl) return;

        float boxHeight = rect.bottomRight.y - rect.topLeft.y;
        float barWidth = 2.5f;
        float barMargin = 4.0f;
        Shadow::Vec2 barBgTop = { (float)(rect.topLeft.x - barMargin - barWidth), (float)rect.topLeft.y };
        Shadow::Vec2 barBgBottom = { (float)(rect.topLeft.x - barMargin), (float)rect.bottomRight.y };

        Shadow::Vec2 bgPos = { (float)(barBgTop.x - 1.0f), (float)(barBgTop.y - 1.0f) };
        Shadow::Vec2 bgSize = { (float)((barBgBottom.x + 1.0f) - bgPos.x), (float)((barBgBottom.y + 1.0f) - bgPos.y) };
        dl->AddRectFilled(bgPos, bgSize, { 0.0f, 0.0f, 0.0f, a * 0.7f });

        float percentage = std::clamp(healthPercent / maxHealth, 0.0f, 1.0f);

        float colr, colg, colb, cola;
        if (percentage > 0.5f)
            colr, colg, colb, cola = (1.0f - percentage) * 2.0f, 1.0f, 0.0f, a;
        else
            colr, colg, colb, cola = 1.0f, percentage * 2.0f, 0.0f, a;

        if (healthPercent > 0) {
            float dynamicHeight = boxHeight * percentage;
            Shadow::Vec2 fgPos = { (float)barBgTop.x, (float)(barBgBottom.y - dynamicHeight) };
            Shadow::Vec2 fgSize = { barWidth, dynamicHeight };
            dl->AddRectFilled(fgPos, fgSize, { colr, colg, colb, cola });
        }
    }
}
