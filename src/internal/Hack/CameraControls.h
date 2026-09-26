#include "../Config/InterfaceRules.h"
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace g_Hack::CameraControls {
    inline bool ConsumeFlightKey(unsigned Key, bool Pressed, bool Building) {
        if (!Pressed) return false; // Always release native keys held before entry.
        if (Key == 'W' || Key == 'A' || Key == 'S' || Key == 'D' ||
            Key == 0x20 || Key == 0x10 || Key == 0x11 || Key == 0x1b) return true;
        if ((Key >= '0' && Key <= '9') || (Key >= 0x60 && Key <= 0x69)) return false;
        return !Building;
    }
    class DoubleTap {
        uint64_t LastPress = 0;
        bool Armed = false, Down = false;
    public:
        bool Press(uint64_t Now, bool Repeated) {
            if (Repeated || Down) return false;
            Down = true;
            const bool Toggle = Armed && Now >= LastPress && Now - LastPress <= 350;
            Armed = !Toggle;
            LastPress = Now;
            return Toggle;
        }
        void Release() { Down = false; }
        void Reset() { *this = {}; }
    };

    struct Vector { double X = 0, Y = 0, Z = 0; };
    inline Vector Step(double Yaw, double Pitch, double Forward, double Right,
        double Up, double SpeedMeters, double DeltaSeconds, bool Fast) {
        if (!std::isfinite(Yaw) || !std::isfinite(Pitch) || !std::isfinite(SpeedMeters) ||
            !std::isfinite(DeltaSeconds) || DeltaSeconds <= 0) return {};
        constexpr double Radians = 3.14159265358979323846 / 180;
        const double Y = Yaw * Radians, P = std::clamp(Pitch, -89.0, 89.0) * Radians;
        Vector V{std::cos(P) * std::cos(Y) * Forward - std::sin(Y) * Right,
            std::cos(P) * std::sin(Y) * Forward + std::cos(Y) * Right, std::sin(P) * Forward + Up};
        const double Length = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
        const double Scale = std::clamp(SpeedMeters, 1.0, double(PhoenixInterface::MaxCameraSpeed)) * 100 *
            std::min(DeltaSeconds, 0.1) * (Fast ? 3 : 1) / std::max(1.0, Length);
        return {V.X * Scale, V.Y * Scale, V.Z * Scale};
    }
}
