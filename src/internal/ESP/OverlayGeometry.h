#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace g_ESP::Overlay {
    struct Point { double X = 0, Y = 0; };
    // Liang-Barsky clipping keeps projected offscreen bones from generating
    // enormous draw commands. Failed/behind-camera projections never enter here.
    inline bool ClipLine(Point& A, Point& B, double Width, double Height) {
        if (!std::isfinite(A.X) || !std::isfinite(A.Y) || !std::isfinite(B.X) ||
            !std::isfinite(B.Y) || !std::isfinite(Width) || !std::isfinite(Height) ||
            Width <= 0 || Height <= 0) return false;
        const double DX = B.X - A.X, DY = B.Y - A.Y;
        if (!std::isfinite(DX) || !std::isfinite(DY)) return false;
        double Start = 0, End = 1;
        const auto Boundary = [&](double P, double Q) {
            if (P == 0) return Q >= 0;
            const double T = Q / P;
            if (P < 0) Start = std::max(Start, T);
            else End = std::min(End, T);
            return Start <= End;
        };
        if (!Boundary(-DX, A.X) || !Boundary(DX, Width - A.X) ||
            !Boundary(-DY, A.Y) || !Boundary(DY, Height - A.Y)) return false;
        B = {A.X + End * DX, A.Y + End * DY};
        A = {A.X + Start * DX, A.Y + Start * DY};
        return std::hypot(B.X - A.X, B.Y - A.Y) >= .25;
    }
    inline constexpr std::array<std::array<int, 2>, 12> BoxEdges{{
        {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}}};
    inline bool InRange(double Distance, double Limit) {
        return std::isfinite(Distance) && std::isfinite(Limit) && Distance >= 0 &&
            Distance <= std::clamp(Limit, 1.0, 2000.0);
    }
}
