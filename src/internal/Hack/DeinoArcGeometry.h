#pragma once
#include <utility>
namespace PhoenixDeinoArc {
// The native aiming beam consumes world-space control points, not direction vectors.
// Collinear cubic controls reproduce each short straight segment without world-origin bends.
template<class Vector> std::pair<Vector,Vector> Controls(const Vector& Start,const Vector& End) {
    auto At=[&](double T) { return Vector{Start.X+(End.X-Start.X)*T,
        Start.Y+(End.Y-Start.Y)*T,Start.Z+(End.Z-Start.Z)*T}; };
    return {At(1.0/3.0),At(2.0/3.0)};
}
}
