#pragma once
#include <cstdint>
#include <cmath>
namespace PhoenixAirLatch {
struct ReleaseVector { double X=0,Y=0,Z=0; bool Valid=false; };
inline ReleaseVector ReleaseVelocity(double Yaw,double Horizontal,double Up) {
    if (!std::isfinite(Yaw) || !std::isfinite(Horizontal) || !std::isfinite(Up) ||
        Horizontal<=0 || Horizontal>100 || Up<0 || Up>30) return {};
    const double R=Yaw*0.017453292519943295;
    return {100*Horizontal*std::cos(R),100*Horizontal*std::sin(R),100*Up,true};
}
inline bool AllowsRenewalMode(int Mode) {
    // None, Walking/NavWalking, Falling, Flying and Custom can all occur
    // during latch transitions. Swimming and invalid modes are excluded.
    return Mode>=0 && Mode<=6 && Mode!=4;
}
struct Landing {
    bool Seen=false;
    uint64_t Since=0;
    bool Confirm(uint64_t Now,bool FloorClose) {
        if (!FloorClose) { Seen=false; return false; }
        if (!Seen) { Seen=true; Since=Now; return false; }
        return Now>=Since && Now-Since>=250;
    }
};
struct Renewal {
    uint64_t Next=0;
    void Submitted(uint64_t Now) { Next=Now+250; }
    bool Take(uint64_t Now,int State,bool Eligible) {
        if (State!=0 || !Eligible || Now<Next) return false;
        Submitted(Now); // No catch-up burst after a stalled frame.
        return true;
    }
};
}
