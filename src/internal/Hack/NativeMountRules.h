#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace PhoenixNativeMount {
struct Vector { double X=0, Y=0, Z=0; };
struct Plan { Vector Target, Delta; bool Valid=false; };
inline Plan Boost(Vector V, double Yaw, bool Ground, double GroundSpeed,
                  double AirStep, double HorizontalCap) {
    if (!std::isfinite(V.X) || !std::isfinite(V.Y) || !std::isfinite(V.Z) ||
        !std::isfinite(Yaw) || !std::isfinite(GroundSpeed) || !std::isfinite(AirStep) ||
        !std::isfinite(HorizontalCap) || GroundSpeed<=0 || AirStep<=0 || HorizontalCap<=0) return {};
    auto T=V;
    if (Ground) T.Z=GroundSpeed;
    else {
        const double A=Yaw*0.017453292519943295;
        T.X+=std::cos(A)*AirStep; T.Y+=std::sin(A)*AirStep;
        const double Limit=std::max(HorizontalCap,std::hypot(V.X,V.Y));
        const double Speed=std::hypot(T.X,T.Y);
        if (!std::isfinite(Speed) || !std::isfinite(Limit)) return {};
        if (Speed>Limit) { T.X*=Limit/Speed; T.Y*=Limit/Speed; }
    }
    return {T,{T.X-V.X,T.Y-V.Y,T.Z-V.Z},true};
}
struct Press {
    bool Released=false;
    uint64_t Next=0;
    bool Take(bool Down,uint64_t Now) {
        if (!Down) { Released=true; return false; }
        const bool Edge=Released; Released=false;
        if (!Edge || Now<Next) return false;
        Next=Now+250; return true;
    }
};
}
