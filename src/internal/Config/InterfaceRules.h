#pragma once
#include <algorithm>
#include <cmath>

namespace PhoenixInterface {
    // Shared by controls and runtime; distances never override server permissions.
    inline constexpr float MaxAimDistance = 2000.0f;
    inline constexpr float MaxFontPixels = 48.0f;
    inline constexpr float MaxCameraSpeed = 200.0f;
    inline constexpr float MaxSpearTapSeconds = 0.5f;
    inline constexpr float MaxHarvestSearchMeters = 300.0f;
    inline constexpr float MaxHarvestApproachMeters = 30.0f;
    inline constexpr float MaxHarvestBeamMeters = 150.0f;
    inline constexpr float MaxHarvestAttackIntervalMs = 10000.0f;
    inline constexpr float MaxHarvestSafeDropMeters = 15.0f;
    inline constexpr float MaxDinoHarvestRangeMultiplier = 5.0f;
    inline constexpr unsigned Ctrl = 0x100, Alt = 0x200, Shift = 0x400;
    inline constexpr unsigned ModifierMask = Ctrl | Alt | Shift;
    inline constexpr unsigned DefaultMenuKey = 0x71; // F2
    inline constexpr unsigned DefaultStopKey = Ctrl | 0x7b; // Ctrl+F12
    inline bool IsAssignable(unsigned Chord) {
        const unsigned Key = Chord & 0xff;
        return (Chord & ~(ModifierMask | 0xff)) == 0 &&
            ((Key >= 0x30 && Key <= 0x39) || (Key >= 0x41 && Key <= 0x5a) ||
             (Key >= 0x70 && Key <= 0x87) || Key == 0x2d || Key == 0x24 || Key == 0x23 ||
             Key == 0x21 || Key == 0x22);
    }
    inline bool ShouldTrigger(unsigned Binding, unsigned Chord, bool Repeated,
        bool Foreground, bool TextInput, bool MenuOpen, bool StopAction = false) {
        return Binding != 0 && Binding == Chord && !Repeated && Foreground &&
            (StopAction || (!TextInput && !MenuOpen));
    }
    inline float FiniteClamp(float Value, float Min, float Max, float Fallback) {
        return std::clamp(std::isfinite(Value) ? Value : Fallback, Min, Max);
    }
}
