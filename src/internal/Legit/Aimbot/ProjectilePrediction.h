#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace g_Aimbot {
    struct BallisticVector {
        double X = 0, Y = 0, Z = 0;
        BallisticVector operator+(BallisticVector v) const { return {X + v.X, Y + v.Y, Z + v.Z}; }
        BallisticVector operator-(BallisticVector v) const { return {X - v.X, Y - v.Y, Z - v.Z}; }
        BallisticVector operator*(double s) const { return {X * s, Y * s, Z * s}; }
        double Length() const { return std::hypot(X, Y, Z); }
        bool Finite() const { return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z); }
    };
    struct ProjectileSolution { BallisticVector Direction, Impact; double Time; };

    // Earliest positive intercept of a constant-velocity target with a
    // constant-speed launch under gravity. Units: centimetres and seconds.
    inline std::optional<ProjectileSolution> PredictProjectile(BallisticVector Origin,
        BallisticVector Target, BallisticVector Velocity, double Speed, double GravityZ,
        double Horizon, double Delay = 0) {
        if (!Origin.Finite() || !Target.Finite() || !Velocity.Finite() ||
            !std::isfinite(Speed) || Speed <= 1 || !std::isfinite(GravityZ) ||
            !std::isfinite(Horizon) || Horizon <= 0 || !std::isfinite(Delay) || Delay < 0) return {};
        Horizon = std::min(Horizon, 10.0);
        const auto Offset = Target + Velocity * Delay - Origin;
        const auto Launch = [&](double t) { return Offset + Velocity * t - BallisticVector{0, 0, GravityZ} * (0.5 * t * t); };
        const auto Error = [&](double t) { return Launch(t).Length() - Speed * t; };
        double Low = 0;
        // Dense bounded search brackets the low arc without Newton divergence
        // for crossing, receding, vertical and near-maximum-range targets.
        for (int i = 1; i <= 512; ++i) {
            const double High = Horizon * i / 512.0;
            if (Error(High) <= 0) {
                double End = High;
                for (int j = 0; j < 48; ++j) {
                    const double Mid = (Low + End) * 0.5;
                    if (Error(Mid) > 0) Low = Mid; else End = Mid;
                }
                const double Time = (Low + End) * 0.5;
                const auto Delta = Launch(Time);
                const double Length = Delta.Length();
                if (Time < 0.00001 || !std::isfinite(Length) || Length <= 0) return {};
                return ProjectileSolution{Delta * (1.0 / Length), Target + Velocity * (Delay + Time), Time};
            }
            Low = High;
        }
        return {};
    }
}
