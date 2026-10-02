#pragma once
#include "DeinoActionRules.h"
#include <limits>
namespace PhoenixDeinoFlight {
inline PhoenixAirLatch::ReleaseVector AimVector(double Yaw,double Pitch,double SpeedMetres) {
    if(!std::isfinite(Yaw) || !std::isfinite(Pitch) || !std::isfinite(SpeedMetres) || SpeedMetres<1 || SpeedMetres>300) return {};
    constexpr double Radians=.017453292519943295;
    const double Y=std::remainder(Yaw,360.0)*Radians;
    const double P=std::clamp(std::remainder(Pitch,360.0),-90.0,90.0)*Radians;
    const double Speed=100*SpeedMetres;
    const double Horizontal=std::abs(std::cos(P))<1e-12?0:Speed*std::cos(P);
    return {Horizontal*std::cos(Y),Horizontal*std::sin(Y),Speed*std::sin(P),true};
}
struct InputGate {
    bool Rearm=false;
    bool Poll(bool Ready,bool SpaceDown) {
        if(!Ready) { Rearm=true; return false; }
        if(Rearm) { if(!SpaceDown) Rearm=false; return false; }
        return true;
    }
};
inline PhoenixAirLatch::ReleaseVector AirVector(double Yaw,double Multiplier,double UpMetres) {
    if (!std::isfinite(Yaw) || !std::isfinite(Multiplier) || !std::isfinite(UpMetres) ||
        Multiplier<1 || Multiplier>30 || UpMetres< -300 || UpMetres>300) return {};
    const double A=std::remainder(Yaw,360.0)*0.017453292519943295;
    // 1x = 30 m/s horizontally; the resolved vertical component is independent of this multiplier.
    return {3000*Multiplier*std::cos(A),3000*Multiplier*std::sin(A),100*UpMetres,true};
}
struct Control {
    enum class Action { None, Jump, Hover };
    PhoenixDeinoAction::Trigger Space;
    uint64_t AirUntil=0,NextHover=0;
    bool Hovering=false; // Set only after the hover action was actually submitted.
    void Jumped(uint64_t Now,uint64_t Protection) { AirUntil=Now+Protection; Hovering=false; }
    Action Poll(bool Down,bool Moving,bool Air,bool Protected,uint64_t Now,double Interval) {
        const bool Edge=Space.Poll(Down,Now,true,0)==PhoenixDeinoAction::Trigger::Result::Submit;
        if (!Air) { AirUntil=NextHover=0; Hovering=false; return Action::None; }
        if (Protected || Now<AirUntil) { Hovering=false; return Action::None; } // consumed, never queued
        if (Edge) { Hovering=false; return Action::Jump; }
        if (Down || Moving || !std::isfinite(Interval)) { Hovering=false; return Action::None; }
        if (Now<NextHover) return Action::None;
        NextHover=Now+uint64_t(std::llround(std::clamp(Interval,10.0,500.0)));
        return Action::Hover; // at most one per game tick; no catch-up
    }
};
}
