// ESP.h
#pragma once
#include <vector>
#include <string>
#include "../../external/CppSDK/SDK.hpp"
#include "../../external/Shadow-Gui/include/Shadow.h"

namespace g_ESP {
    struct BoxRect {
        Shadow::Vec2 topLeft;
        Shadow::Vec2 bottomRight;
        bool valid = false;
    };

    enum class FlagPos {
        Left,
        Right,
        Top,
        Bottom
    };

    enum class BarPos {
        Left,
        Right,
        Top,
        Bottom
    };

    enum class BarOrientation {
        Vertical,
        Horizontal
    };

    class BarManager {
    private:
        float leftOffset = 0.0f;
        float rightOffset = 0.0f;
        float topOffset = 0.0f;
        float bottomOffset = 0.0f;

    public:
        void Reset() {
            leftOffset = 0.0f;
            rightOffset = 0.0f;
            topOffset = 0.0f;
            bottomOffset = 0.0f;
        }

        float GetLeftOffset() const { return leftOffset; }
        float GetRightOffset() const { return rightOffset; }
        float GetTopOffset() const { return topOffset; }
        float GetBottomOffset() const { return bottomOffset; }

        void AddBar(SDK::UCanvas* Canvas, BoxRect rect, float currentValue, float maxValue, Shadow::Color color, BarPos pos, BarOrientation orientation, float a);
    };

    class FlagManager {
    private:
        float leftY = 0.0f;
        float rightY = 0.0f;
        float topY = 0.0f;
        float bottomY = 0.0f;

    public:
        void Reset() {
            leftY = 0.0f;
            rightY = 0.0f;
            topY = 0.0f;
            bottomY = 0.0f;
        }

        void AddFlag(SDK::UCanvas* Canvas, BoxRect rect, const std::string& text, Shadow::Color color, FlagPos pos, float alphaMult, const BarManager* barMgr);
    };

    void BeginLabelFrame();
    BoxRect GetBox(SDK::UCanvas* Canvas, SDK::AActor* entity, float width_scale,
        bool useCharacterMeshBounds = true);
    void DrawBox(SDK::UCanvas* Canvas, const BoxRect& rect, Shadow::Color color, float alpha);

    void DrawHealthBar(SDK::UCanvas* Canvas, BoxRect rect, float healthPercent, float maxHealth, float a);

    enum class RelationType {
        Enemy,
        Team,
    };

    RelationType GetRelation(SDK::APrimalCharacter* TargetChar, SDK::APrimalCharacter* LocalChar);
}
