// ESP.cpp
#include "../../external/CppSDK/SDK.hpp"
#include "../../external/Shadow-Gui/include/Shadow.h"
#include "ESP.h"
#include "ESPLayout.h"
#include <format>
#include "../Legit/Aimbot/AimSelection.h"
#include "../Config/Configs.h"
#include "../Language/LanguageManager.h"
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
    namespace {
        SDK::UCanvas* ProjectionCanvas = nullptr;
        SDK::APlayerController* ProjectionController = nullptr;
        g_Aimbot::AimCanvasSpace ProjectionSpace{};
    }
    void BeginProjectionFrame(SDK::UCanvas* Canvas) {
        ProjectionCanvas = Canvas;
        ProjectionController = g_Util::GetLocalPC();
        ProjectionSpace = {};
        if (!Canvas || !ProjectionController) return;
        int32_t Width = 0, Height = 0;
        ProjectionController->GetViewportSize(&Width, &Height);
        // Use exactly the same converter as the accurately positioned aim marker.
        ProjectionSpace = g_Aimbot::AimCanvasSpace::FromViewport(Width, Height, Canvas->ClipX, Canvas->ClipY);
    }
    bool ProjectToCanvas(SDK::UCanvas* Canvas, SDK::APlayerController* PC,
        const SDK::FVector& World, SDK::FVector2D* Screen) {
        if (!Canvas || !PC || !Screen || PC != ProjectionController || Canvas != ProjectionCanvas ||
            ProjectionSpace.ScaleX <= 0 || ProjectionSpace.ScaleY <= 0 ||
            !std::isfinite(World.X) || !std::isfinite(World.Y) || !std::isfinite(World.Z) ||
            !PC->ProjectWorldLocationToScreen(World, Screen, false) ||
            !std::isfinite(Screen->X) || !std::isfinite(Screen->Y)) return false;
        const auto Point = ProjectionSpace.ToCanvas(Screen->X, Screen->Y);
        Screen->X = Point.X; Screen->Y = Point.Y;
        return true;
    }

    struct LabelRect {
        float minX;
        float minY;
        float maxX;
        float maxY;
    };

    static std::vector<LabelRect> s_occupiedTopLabels;

    static bool ReserveTopLabel(const LabelRect& Candidate) {
        constexpr float Margin = 3.0f;
        if (g_Config::bESPPreventOverlap) for (const LabelRect& Occupied : s_occupiedTopLabels) {
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

#include "ESPStyle.inl"

    RelationType GetRelation(SDK::APrimalCharacter* TargetChar, SDK::APrimalCharacter* LocalChar) {
        if (!TargetChar || !LocalChar) return RelationType::Enemy;

        // Let ASA resolve tribe/alliance rules first. TargetingTeam equality
        // alone treats allied tribes as enemies and can disagree with the
        // model-stencil color used by the native scanner.
        if (LocalChar->IsPrimalCharFriendly(TargetChar)) {
            return RelationType::Team;
        }

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

    void FlagManager::AddFlag(SDK::UCanvas* Canvas, BoxRect rect,
        const std::string& text, Shadow::Color color, FlagPos pos,
        float alphaMult, const BarManager* barMgr, float scaleMultiplier) {
        if (!rect.valid || text.empty() || !Canvas) return;

        Shadow::ShadowDrawList* dl = Shadow::GetBackgroundDrawList();
        if (!dl) return;

        static SDK::UFont* SansationBold18 = g_Util::GetSansationBold18();
        static SDK::UFont* OpenSansRegular12 = g_Util::GetOpenSansRegular12();

        if (!SansationBold18 || !OpenSansRegular12) return;
        SDK::UFont* currentFont = (pos == FlagPos::Top) ? SansationBold18 : OpenSansRegular12;

        const float textScale = InfoFontScale(currentFont, scaleMultiplier, pos == FlagPos::Top);

        // 使用与最终绘制完全一致的缩放测量文字，避免缩放后锚点漂移。
        Shadow::PushFont(currentFont, textScale);
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
            // Preserve the target's horizontal anchor; do not pin labels to a screen corner.
            if (drawPos.x + textSize.x < 0 || drawPos.x > Canvas->ClipX) return;
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
                if (drawPos.x + textSize.x < 0 || drawPos.x > Canvas->ClipX) return;
                if (drawPos.y + textSize.y > Canvas->ClipY - 2.0f) {
                    drawPos.y = rect.topLeft.y - barOffset - textSize.y - 3.0f;
                    labelAboveBox = true;
                }
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

        if (!anchoredLabel) {
            const LabelRect Bounds{drawPos.x - 3, drawPos.y - 1, drawPos.x + textSize.x + 3, drawPos.y + textSize.y + 1};
            if (Bounds.minX < 0 || Bounds.minY < 0 || Bounds.maxX > Canvas->ClipX || Bounds.maxY > Canvas->ClipY ||
                !ReserveTopLabel(Bounds)) return;
            dl->AddRectFilled({Bounds.minX, Bounds.minY}, {Bounds.maxX - Bounds.minX, Bounds.maxY - Bounds.minY},
                {0.025f, 0.035f, 0.045f, alphaMult * 0.8f});
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
        dl->AddText(currentFont, textScale, shadowCol, shadowCol, drawPos, color, text);
        // Canvas->K2_DrawText(currentFont, fText, drawPos, scaleSize, color, 0.0f, shadowCol, SDK::FVector2D{ 1.0f, 1.0f }, false, false, true, shadowCol);
    }

    BoxRect GetBox(SDK::UCanvas* Canvas, SDK::AActor* entity, float width_scale,
        bool useCharacterMeshBounds, const SDK::FVector* visualBoundsOrigin,
        const SDK::FVector* visualBoundsExtent) {
        BoxRect rect;
        rect.valid = false;
        if (!Canvas || !entity || entity->bHidden) return rect;
        auto PC = g_Util::GetLocalPC();
        if (!PC) return rect;

        std::array<SDK::FVector, 8> worldCorners{};
        int worldCornerCount = 0;
        SDK::FVector boundsOrigin{};
        SDK::FVector boundsExtent{};

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
                boundsOrigin = Origin;
                boundsExtent = Extent;
                return true;
            };
        // Ask Unreal for the skeletal component's current world-space bounds.
        // Transforming the skeletal asset's reference bounds here is unreliable:
        // some ASA dinos apply extra mesh/root-bone offsets that are not represented
        // by the component transform, which moves the entire ESP box off the model.
        bool usedMeshBounds = false;
        if (visualBoundsOrigin && visualBoundsExtent) {
            usedMeshBounds = FillAxisAlignedCorners(
                *visualBoundsOrigin, *visualBoundsExtent);
        }
        if (useCharacterMeshBounds && entity->IsA(SDK::ACharacter::StaticClass())) {
            SDK::ACharacter* Character = static_cast<SDK::ACharacter*>(entity);
            SDK::USkeletalMeshComponent* Mesh = Character->Mesh;
            if (!usedMeshBounds && Mesh && Mesh->Class) {
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

        // The mesh's current local bounds avoid projecting an already expanded
        // world AABB a second time. Retain the verified world center so ASA mesh
        // offsets cannot reintroduce the previous target-position bug.
        if (usedMeshBounds && useCharacterMeshBounds && entity->IsA(SDK::ACharacter::StaticClass())) {
            auto* Mesh = static_cast<SDK::ACharacter*>(entity)->Mesh;
            // Current runtime reflection verified these flags and bounds fields.
            // The shared cache can contain world-space data: never transform it
            // again unless it explicitly holds valid local bounds.
            if (Mesh && Mesh->bCachedLocalBoundsUpToDate && !Mesh->bCachedWorldSpaceBoundsUpToDate) {
                const auto Local = Mesh->CachedWorldOrLocalSpaceBounds.BoxExtent;
                const auto Transform = Mesh->K2_GetComponentToWorld();
                const auto& S = Transform.Scale3D;
                const auto& Q = Transform.Rotation;
                const auto Oriented = Layout::TightBounds({Local.X, Local.Y, Local.Z}, {S.X, S.Y, S.Z},
                    {Q.X, Q.Y, Q.Z, Q.W}, {boundsExtent.X, boundsExtent.Y, boundsExtent.Z});
                if (Oriented) for (int I = 0; I < 8; ++I) {
                    const auto& Offset = (*Oriented)[I];
                    worldCorners[I] = boundsOrigin + SDK::FVector{Offset.X, Offset.Y, Offset.Z};
                }
            }
        }

        // Project the AABB against the camera near plane. The old implementation
        // accepted any two projected corners; for a large creature close to the
        // camera, most corners sit behind the near plane and the surviving pair
        // describes only one corner of the model.
        SDK::FVector CameraLocation{};
        SDK::FRotator CameraRotation{};
        PC->GetPlayerViewPoint(&CameraLocation, &CameraRotation);
        const SDK::FVector CameraForward =
            SDK::UKismetMathLibrary::GetForwardVector(CameraRotation);
        const auto CameraDepth = [&](const SDK::FVector& Point) {
            const SDK::FVector Delta{
                Point.X - CameraLocation.X,
                Point.Y - CameraLocation.Y,
                Point.Z - CameraLocation.Z
            };
            return Delta.X * CameraForward.X + Delta.Y * CameraForward.Y +
                Delta.Z * CameraForward.Z;
            };

        const bool cameraInsideBounds =
            std::abs(CameraLocation.X - boundsOrigin.X) <= boundsExtent.X &&
            std::abs(CameraLocation.Y - boundsOrigin.Y) <= boundsExtent.Y &&
            std::abs(CameraLocation.Z - boundsOrigin.Z) <= boundsExtent.Z;
        if (cameraInsideBounds) return rect;

        constexpr double NearPlaneDepth = 10.0;
        constexpr int BoxEdges[12][2] = {
            {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
            {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}
        };
        std::array<SDK::FVector, 20> projectionPoints{};
        int projectionPointCount = 0;
        std::array<double, 8> cornerDepths{};
        for (int CornerIndex = 0; CornerIndex < worldCornerCount; ++CornerIndex) {
            cornerDepths[CornerIndex] = CameraDepth(worldCorners[CornerIndex]);
            if (cornerDepths[CornerIndex] >= NearPlaneDepth) {
                projectionPoints[projectionPointCount++] = worldCorners[CornerIndex];
            }
        }
        for (const auto& Edge : BoxEdges) {
            const int AIndex = Edge[0];
            const int BIndex = Edge[1];
            const double ADepth = cornerDepths[AIndex];
            const double BDepth = cornerDepths[BIndex];
            if ((ADepth >= NearPlaneDepth) == (BDepth >= NearPlaneDepth) ||
                std::abs(BDepth - ADepth) < 0.0001) {
                continue;
            }
            const double T = std::clamp(
                (NearPlaneDepth - ADepth) / (BDepth - ADepth), 0.0, 1.0);
            const SDK::FVector& A = worldCorners[AIndex];
            const SDK::FVector& B = worldCorners[BIndex];
            projectionPoints[projectionPointCount++] = SDK::FVector{
                A.X + (B.X - A.X) * T,
                A.Y + (B.Y - A.Y) * T,
                A.Z + (B.Z - A.Z) * T
            };
        }

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        int projectedCorners = 0;
        for (int PointIndex = 0; PointIndex < projectionPointCount; ++PointIndex) {
            const SDK::FVector& Point = projectionPoints[PointIndex];
            if (!IsFiniteVector(Point)) continue;
            SDK::FVector2D screen{};
            if (!ProjectToCanvas(Canvas, PC, Point, &screen) ||
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

        if (projectedCorners >= 4 && maxX - minX > 1.0f && maxY - minY > 1.0f) {
            // Retain the projected model aspect ratio. The old half-height
            // minimum made tall/edge-on targets and their labels unnecessarily wide.
            const float Padding = std::isfinite(g_Config::ESPBoxPaddingPixels) ?
                std::clamp(g_Config::ESPBoxPaddingPixels, 0.0f, 10.0f) : 2.0f;
            const float paddingX = PixelsX(Padding), paddingY = PixelsY(Padding);
            rect.topLeft = { minX - paddingX, minY - paddingY };
            rect.bottomRight = { maxX + paddingX, maxY + paddingY };
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
        const float ThicknessPixels = std::isfinite(g_Config::ESPBoxThicknessPixels) ?
            std::clamp(g_Config::ESPBoxThicknessPixels, 0.5f, 3.0f) : 1.25f;
        const float thickness = PixelsX(ThicknessPixels);
        const float CornerX = std::min(w * 0.25f, PixelsX(18)), CornerY = std::min(h * 0.25f, PixelsY(18));
        const auto Line = [&](Shadow::Vec2 A, Shadow::Vec2 B) {
            dl->AddLine(A, B, {0, 0, 0, color.a * 0.85f}, thickness + PixelsX(1.5f));
            dl->AddLine(A, B, color, thickness);
        };
        if (g_Config::bESPCornerBox) {
            for (int SideX : {0, 1}) for (int SideY : {0, 1}) {
                const float X = x + SideX * w, Y = y + SideY * h;
                Line({X, Y}, {X + (SideX ? -CornerX : CornerX), Y});
                Line({X, Y}, {X, Y + (SideY ? -CornerY : CornerY)});
            }
        }
        else {
            Line({x, y}, {x + w, y}); Line({x + w, y}, {x + w, y + h});
            Line({x + w, y + h}, {x, y + h}); Line({x, y + h}, {x, y});
        }
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
