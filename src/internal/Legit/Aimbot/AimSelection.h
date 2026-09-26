#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace g_Aimbot {
    struct AimScreenRanking {
        uint64_t Previous = 0, Target = 0;
        double Score = std::numeric_limits<double>::infinity();

        bool Consider(uint64_t Key, double DistanceSquared) {
            if (!Key || !std::isfinite(DistanceSquared) || DistanceSquared < 0 || DistanceSquared > Score) return false;
            // Only exact ties retain the previous identity. A strictly closer
            // point always wins, independent of weapon, health or actor order.
            if (DistanceSquared == Score && Target &&
                (Target == Previous || (Key != Previous && Key >= Target))) return false;
            Target = Key;
            Score = DistanceSquared;
            return true;
        }
    };

    template<typename Vector>
    bool InFrontOfAimView(const Vector& Point, const Vector& Camera, const Vector& Player, const Vector& Forward) {
        const auto Depth = [&](const Vector& Origin) {
            return (Point.X - Origin.X) * Forward.X + (Point.Y - Origin.Y) * Forward.Y + (Point.Z - Origin.Z) * Forward.Z;
        };
        const double CameraDepth = Depth(Camera), PlayerDepth = Depth(Player);
        // Screen projection alone is not evidence of positive depth. Also reject
        // the space behind the player but in front of a third-person camera.
        return std::isfinite(CameraDepth) && std::isfinite(PlayerDepth) && CameraDepth > 1.0 && PlayerDepth > 1.0;
    }
    struct AimScreenPoint { double X = 0, Y = 0; };

    struct AimCanvasSpace {
        double ScaleX = 0, ScaleY = 0;

        // Projection uses viewport pixels. DebugCanvas ClipX/Y describe its
        // logical drawing extent; SizeX/Y still contain physical pixels when
        // the engine has a DPI transform on the canvas (e.g. 150%).
        static AimCanvasSpace FromViewport(double Width, double Height, double ClipWidth, double ClipHeight) {
            if (!std::isfinite(Width) || !std::isfinite(Height) ||
                !std::isfinite(ClipWidth) || !std::isfinite(ClipHeight) ||
                Width <= 0 || Height <= 0 || ClipWidth <= 0 || ClipHeight <= 0) return {};
            return {ClipWidth / Width, ClipHeight / Height};
        }

        AimScreenPoint ToCanvas(double X, double Y) const { return {X * ScaleX, Y * ScaleY}; }
    };

    struct AimLabelLayout {
        double X = 0, Y = 0, Scale = 0;

        static AimLabelLayout WithinCircle(double X, double Y, double Radius, double Width, double Height) {
            if (!std::isfinite(Radius) || !std::isfinite(Width) || !std::isfinite(Height) ||
                Radius <= 0 || Width <= 0 || Height <= 0) return {};
            // Keep the whole label below the reticle and inside the circle,
            // including long names and the smallest configurable circle.
            const double Scale = std::min({1.0, Radius * 1.5 / Width, Radius * 0.5 / Height});
            return {X - Width * Scale * 0.5, Y + Radius * 0.1, Scale};
        }
    };

    struct AimCircle {
        double Width = 0, Height = 0, X = 0, Y = 0, Radius = 0;

        static AimCircle FromViewport(double Width, double Height, float Percent) {
            if (!std::isfinite(Width) || !std::isfinite(Height) || Width <= 0 || Height <= 0) return {};
            const double Size = std::isfinite(Percent) ? std::clamp(Percent, 1.0f, 100.0f) : 30.0f;
            return {Width, Height, Width * 0.5, Height * 0.5, std::min(Width, Height) * Size * 0.005};
        }

        // The same circle drives selection and rendering. No world-distance or
        // previous-target bonus can make a farther screen target win.
        double Score(double ScreenX, double ScreenY) const {
            if (Radius <= 0 || !std::isfinite(ScreenX) || !std::isfinite(ScreenY) ||
                ScreenX < 0 || ScreenX >= Width || ScreenY < 0 || ScreenY >= Height)
                return std::numeric_limits<double>::infinity();
            const double DX = ScreenX - X, DY = ScreenY - Y;
            const double DistanceSquared = DX * DX + DY * DY;
            return DistanceSquared <= Radius * Radius ? DistanceSquared : std::numeric_limits<double>::infinity();
        }

        bool OverlapsBounds(double MinX, double MinY, double MaxX, double MaxY) const {
            // Broad phase only: an uncertain projection must not discard a
            // creature before its current collision points have been read.
            if (!std::isfinite(MinX) || !std::isfinite(MinY) || !std::isfinite(MaxX) ||
                !std::isfinite(MaxY) || MinX > MaxX || MinY > MaxY) return true;
            const double DX = X - std::clamp(X, MinX, MaxX);
            const double DY = Y - std::clamp(Y, MinY, MaxY);
            return Radius > 0 && DX * DX + DY * DY <= Radius * Radius;
        }
    };
}
