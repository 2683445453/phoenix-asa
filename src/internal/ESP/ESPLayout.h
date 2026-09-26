#include "../Config/InterfaceRules.h"
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace g_ESP::Layout {
    struct Point3 { double X = 0, Y = 0, Z = 0; };
    // Transform only the extents; the caller retains the current rendered center.
    inline std::optional<std::array<Point3, 8>> TightBounds(Point3 Local, Point3 Scale,
        std::array<double, 4> Q, Point3 World) {
        const auto Finite = [](Point3 V) { return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z); };
        if (!Finite(Local) || !Finite(Scale) || !Finite(World) || Local.X <= 0 || Local.Y <= 0 || Local.Z <= 0) return {};
        const double Norm = std::sqrt(Q[0]*Q[0] + Q[1]*Q[1] + Q[2]*Q[2] + Q[3]*Q[3]);
        if (!std::isfinite(Norm) || Norm < 0.9 || Norm > 1.1) return {};
        for (auto& V : Q) V /= Norm;
        std::array<Point3, 8> Result{};
        Point3 Extent{};
        for (int I = 0; I < 8; ++I) {
            const Point3 V{Local.X * Scale.X * (I & 4 ? 1 : -1),
                Local.Y * Scale.Y * (I & 2 ? 1 : -1), Local.Z * Scale.Z * (I & 1 ? 1 : -1)};
            const Point3 T{2 * (Q[1]*V.Z - Q[2]*V.Y), 2 * (Q[2]*V.X - Q[0]*V.Z), 2 * (Q[0]*V.Y - Q[1]*V.X)};
            auto& R = Result[I];
            R = {V.X + Q[3]*T.X + Q[1]*T.Z - Q[2]*T.Y,
                 V.Y + Q[3]*T.Y + Q[2]*T.X - Q[0]*T.Z,
                 V.Z + Q[3]*T.Z + Q[0]*T.Y - Q[1]*T.X};
            if (!Finite(R)) return {};
            Extent.X = std::max(Extent.X, std::abs(R.X));
            Extent.Y = std::max(Extent.Y, std::abs(R.Y));
            Extent.Z = std::max(Extent.Z, std::abs(R.Z));
        }
        const auto Close = [](double Actual, double Expected) {
            return Actual > 0 && Expected > 0 && Actual >= Expected * 0.9 && Actual <= Expected * 1.1;
        };
        if (!Close(Extent.X, World.X) || !Close(Extent.Y, World.Y) || !Close(Extent.Z, World.Z)) return {};
        const double Expand = std::max({1.0, World.X / Extent.X, World.Y / Extent.Y, World.Z / Extent.Z});
        for (auto& R : Result) { R.X *= Expand; R.Y *= Expand; R.Z *= Expand; }
        return Result;
    }
    struct Rect {
        double X = 0, Y = 0, W = 0, H = 0;
        bool Valid() const { return std::isfinite(X) && std::isfinite(Y) && std::isfinite(W) && std::isfinite(H) && W > 0 && H > 0; }
        bool Overlaps(const Rect& R, double Gap = 0) const {
            return X < R.X + R.W + Gap && X + W + Gap > R.X && Y < R.Y + R.H + Gap && Y + H + Gap > R.Y;
        }
        bool Inside(double Width, double Height, double Margin) const {
            return Valid() && X >= Margin && Y >= Margin && X + W <= Width - Margin && Y + H <= Height - Margin;
        }
    };
    struct Placement { Rect Bounds; int Slot = -1; };
    inline std::optional<Placement> Place(const Rect& Box, double Width, double Height,
        double ScreenW, double ScreenH, double Gap, const std::vector<Rect>& Occupied,
        bool AvoidOverlap, int PreviousSlot = -1) {
        if (!Box.Valid() || !std::isfinite(Width) || !std::isfinite(Height) || Width <= 0 || Height <= 0 ||
            !std::isfinite(ScreenW) || !std::isfinite(ScreenH) || ScreenW <= 0 || ScreenH <= 0 ||
            !std::isfinite(Gap) || Gap < 0) return {};
        const double X = Box.X + (Box.W - Width) * 0.5;
        const Rect Slots[] = {
            {X, Box.Y - Height - Gap, Width, Height},
            {X, Box.Y + Box.H + Gap, Width, Height},
            {Box.X + Box.W + Gap, Box.Y + (Box.H - Height) * 0.5, Width, Height},
            {Box.X - Width - Gap, Box.Y + (Box.H - Height) * 0.5, Width, Height}
        };
        const auto Accept = [&](int Slot) {
            const auto& R = Slots[Slot];
            if (!R.Inside(ScreenW, ScreenH, 2)) return false;
            if (AvoidOverlap) for (const auto& Other : Occupied) if (R.Overlaps(Other, 3)) return false;
            return true;
        };
        if (PreviousSlot >= 0 && PreviousSlot < 4 && Accept(PreviousSlot)) return Placement{Slots[PreviousSlot], PreviousSlot};
        for (int Slot = 0; Slot < 4; ++Slot) if (Accept(Slot)) return Placement{Slots[Slot], Slot};
        return {};
    }
    inline double FontPixels(double Base, double DistanceScale, double Minimum) {
        Base = std::isfinite(Base) ? std::clamp(Base, 12.0, double(PhoenixInterface::MaxFontPixels)) : 16;
        Minimum = std::isfinite(Minimum) ? std::clamp(Minimum, 12.0, Base) : std::min(13.0, Base);
        DistanceScale = std::isfinite(DistanceScale) ? std::clamp(DistanceScale, 0.25, 3.0) : 1;
        return std::clamp(Base * DistanceScale, Minimum, double(PhoenixInterface::MaxFontPixels));
    }
}
