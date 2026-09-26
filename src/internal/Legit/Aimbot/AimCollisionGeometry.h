#pragma once
#include "../../../external/CppSDK/SDK/Engine_structs.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace g_Aimbot {
    inline bool FiniteAimVector(const SDK::FVector& V) {
        return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
    }

    template<typename T> bool ValidAimGeometryArray(const SDK::TArray<T>& Values, int Limit = 128) {
        return Values.Num() > 0 && Values.Num() <= Limit && Values.Max() >= Values.Num() && Values.GetDataPtr();
    }

    // Points stay inside individual shapes. A skeleton pivot or an aggregate
    // bounding-box center need not be inside a flat or elongated creature.
    template<typename Rotate, typename Transform>
    std::vector<SDK::FVector> AimGeometryPoints(const SDK::FKAggregateGeom& Geometry,
        Rotate RotateVector, Transform TransformLocation) {
        std::vector<SDK::FVector> Points;
        const auto Add = [&](const SDK::FVector& Point) {
            if (FiniteAimVector(Point)) Points.push_back(Point);
        };
        const auto Capsule = [&](const auto& Shape, double Radius) {
            if (!std::isfinite(Radius) || Radius <= 0 || !std::isfinite(Shape.Length) || Shape.Length < 0) return;
            Add(Shape.Center);
            if (Shape.Length > Radius * 2) {
                const auto Offset = RotateVector(SDK::FVector{0, 0, Shape.Length * 0.25}, Shape.Rotation);
                Add(Shape.Center + Offset);
                Add(Shape.Center - Offset);
            }
        };
        if (ValidAimGeometryArray(Geometry.SphereElems))
            for (const auto& Shape : Geometry.SphereElems)
                if (std::isfinite(Shape.Radius) && Shape.Radius > 0) Add(Shape.Center);
        if (ValidAimGeometryArray(Geometry.SphylElems))
            for (const auto& Shape : Geometry.SphylElems) Capsule(Shape, Shape.Radius);
        if (ValidAimGeometryArray(Geometry.TaperedCapsuleElems))
            for (const auto& Shape : Geometry.TaperedCapsuleElems)
                if (std::isfinite(Shape.Radius0) && std::isfinite(Shape.Radius1) && Shape.Radius0 > 0 && Shape.Radius1 > 0)
                    Capsule(Shape, std::max(Shape.Radius0, Shape.Radius1));
        if (ValidAimGeometryArray(Geometry.BoxElems))
            for (const auto& Shape : Geometry.BoxElems) {
                if (!std::isfinite(Shape.X) || !std::isfinite(Shape.Y) || !std::isfinite(Shape.Z) ||
                    Shape.X <= 0 || Shape.Y <= 0 || Shape.Z <= 0) continue;
                Add(Shape.Center);
                const double Longest = std::max({Shape.X, Shape.Y, Shape.Z});
                if (Longest <= std::min({Shape.X, Shape.Y, Shape.Z}) * 2) continue;
                const SDK::FVector Axis = Longest == Shape.X ? SDK::FVector{Shape.X * 0.25, 0, 0}
                    : Longest == Shape.Y ? SDK::FVector{0, Shape.Y * 0.25, 0} : SDK::FVector{0, 0, Shape.Z * 0.25};
                const auto Offset = RotateVector(Axis, Shape.Rotation);
                Add(Shape.Center + Offset);
                Add(Shape.Center - Offset);
            }
        if (ValidAimGeometryArray(Geometry.ConvexElems))
            for (const auto& Shape : Geometry.ConvexElems) {
                if (!ValidAimGeometryArray(Shape.VertexData, 4096) || Shape.VertexData.Num() < 4) continue;
                if (!FiniteAimVector(Shape.Transform.Translation) || !FiniteAimVector(Shape.Transform.Scale3D) ||
                    std::abs(Shape.Transform.Scale3D.X) < 1e-8 || std::abs(Shape.Transform.Scale3D.Y) < 1e-8 ||
                    std::abs(Shape.Transform.Scale3D.Z) < 1e-8) continue;
                SDK::FVector Center{};
                bool Valid = true;
                for (const auto& Vertex : Shape.VertexData) {
                    if (!FiniteAimVector(Vertex)) { Valid = false; break; }
                    Center += Vertex / static_cast<double>(Shape.VertexData.Num());
                }
                if (!Valid || !FiniteAimVector(Center)) continue;
                // A convex combination of its vertices is inside the convex hull,
                // unlike the center of an asymmetric hull's bounding box.
                Add(TransformLocation(Shape.Transform, Center));
                // Interior samples toward extremal vertices cover long/flat hulls.
                // They are not surface points, leaving room for small pose changes.
                for (int Axis = 0; Axis < 3; ++Axis) {
                    const auto Coord = [Axis](const SDK::FVector& V) { return Axis == 0 ? V.X : Axis == 1 ? V.Y : V.Z; };
                    const auto* Min = &Shape.VertexData[0];
                    const auto* Max = Min;
                    for (const auto& Vertex : Shape.VertexData) {
                        if (Coord(Vertex) < Coord(*Min)) Min = &Vertex;
                        if (Coord(Vertex) > Coord(*Max)) Max = &Vertex;
                    }
                    if (Coord(*Max) - Coord(*Min) <= 0.01) continue;
                    Add(TransformLocation(Shape.Transform, (Center + *Min) * 0.5));
                    Add(TransformLocation(Shape.Transform, (Center + *Max) * 0.5));
                }
            }
        return Points;
    }
}
