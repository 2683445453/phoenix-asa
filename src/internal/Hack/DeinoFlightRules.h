#pragma once
#include "DeinoActionRules.h"
namespace PhoenixDeinoFlight {
inline PhoenixAirLatch::ReleaseVector AirVector(double Yaw,double Multiplier,double UpMetres) {
    if (!std::isfinite(Yaw) || !std::isfinite(Multiplier) || !std::isfinite(UpMetres) ||
        Multiplier<1 || Multiplier>30 || UpMetres<0 || UpMetres>30) return {};
    const double A=std::remainder(Yaw,360.0)*0.017453292519943295;
    // 1x = 30 m/s horizontally; upward speed is independent of pitch/multiplier.
    return {3000*Multiplier*std::cos(A),3000*Multiplier*std::sin(A),100*UpMetres,true};
}
struct Control {
    enum class Action { None, Jump, Hover };
    PhoenixDeinoAction::Trigger Space;
    uint64_t AirUntil=0,NextHover=0;
    void Jumped(uint64_t Now,uint64_t Protection) { AirUntil=Now+Protection; }
    Action Poll(bool Down,bool Moving,bool Air,bool Protected,uint64_t Now,double Interval) {
        const bool Edge=Space.Poll(Down,Now,true,0)==PhoenixDeinoAction::Trigger::Result::Submit;
        if (!Air) { AirUntil=NextHover=0; return Action::None; }
        if (Protected || Now<AirUntil) return Action::None; // consumed, never queued
        if (Edge) return Action::Jump;
        if (Down || Moving || Now<NextHover || !std::isfinite(Interval)) return Action::None;
        NextHover=Now+uint64_t(std::llround(std::clamp(Interval,10.0,500.0)));
        return Action::Hover; // at most one per game tick; no catch-up
    }
};
}
