#pragma once
#include "DeinoAirLatchRules.h"
#include <array>
#include <algorithm>

namespace PhoenixDeinoAction {
// At most one request per press, with no delayed retry after a rejected edge.
struct Trigger {
    enum class Result { Idle, Blocked, Cooldown, Submit };
    bool Released=false;
    uint64_t Next=0;
    Result Poll(bool Down,uint64_t Now,bool Eligible,uint64_t CooldownMs=3000) {
        if (!Down) { Released=true; return Result::Idle; }
        const bool Edge=Released; Released=false;
        if (!Edge) return Result::Idle;
        if (!Eligible) return Result::Blocked;
        if (CooldownMs && Now<Next) return Result::Cooldown;
        Next=CooldownMs ? Now+CooldownMs : 0; return Result::Submit;
    }
    bool Take(bool Down,uint64_t Now,bool Eligible) { return Poll(Down,Now,Eligible)==Result::Submit; }
};
inline int Mode(bool Glide,bool Jump,bool Trace,bool Latch,bool Other,bool Point=false,bool BigJump=false) {
    if (int(Glide)+int(Jump)+int(Trace)+int(Point)!=1 || Other || (BigJump && !Point) ||
        (Trace ? !Latch : Latch)) return 0;
    return Point ? 4 : Jump ? 2 : Trace ? 3 : 1;
}
struct PointReply {
    bool Waiting=false;
    uint64_t Since=0,EndSequence=0;
    void Begin(uint64_t Now,uint64_t Ends) { Waiting=true; Since=Now; EndSequence=Ends; }
    // No delayed action/retry. Timeout merely releases an unanswered request.
    int Update(uint64_t Now,uint64_t Ends,int State) {
        if (!Waiting) return 0;
        const int Result=Ends!=EndSequence ? 1 : (State==5 || State==2) ? 2 : Now-Since>=2000 ? 3 : 0;
        if (Result) Waiting=false;
        return Result;
    }
};
struct AimPoint { double X=0,Y=0,Z=0,Distance=0; bool Valid=false; };
inline uint64_t TakeoffProtectionMs(double Seconds) {
    return static_cast<uint64_t>(std::llround(std::clamp(std::isfinite(Seconds) ? Seconds : 1.0,0.1,5.0)*1000));
}
struct TakeoffControl {
    Trigger Key;
    bool OwnPress=false,ReleaseSeen=false,WasGround=false;
    uint64_t ProtectUntil=0,ReleaseUntil=0;
    bool Press(bool Down,bool Ground,uint64_t Now) {
        return Key.Poll(Down,Now,Ground,0)==Trigger::Result::Submit;
    }
    void Started(uint64_t Now,uint64_t ProtectionMs) { OwnPress=true; ReleaseSeen=false; ProtectUntil=Now+ProtectionMs; }
    bool Mask(bool Down,uint64_t Now) {
        if (!OwnPress) return false;
        if (!Down && !ReleaseSeen) { ReleaseSeen=true; ReleaseUntil=Now+100; }
        if (ReleaseSeen && Now>=ReleaseUntil) OwnPress=false;
        return OwnPress;
    }
    bool Protect(bool Ground,bool Air,uint64_t Now,uint64_t ProtectionMs) {
        if (Ground) WasGround=true;
        if (Air && WasGround) { WasGround=false; ProtectUntil=Now+ProtectionMs; }
        return Air && Now<ProtectUntil;
    }
};
struct FlightControl {
    static constexpr uint64_t HoverInterval=50;
    enum class Action { None, Dash, CaptureHover, Hover };
    bool Released=false,Pending=false,Anchor=false;
    uint64_t Deadline=0,NextHover=0;
    Action Poll(bool Down,bool Moving,bool Air,bool Busy,uint64_t Now) {
        const bool Edge=Released && Down; Released=!Down;
        if (!Air) { Pending=Anchor=false; NextHover=0; return Action::None; }
        if (Edge) { Pending=true; Deadline=Now+1500; Anchor=false; }
        if (Pending && Now>Deadline) Pending=false;
        if (Pending && !Busy) { Pending=false; Anchor=false; NextHover=Now+HoverInterval; return Action::Dash; }
        if (Busy || Pending) return Action::None;
        if (Down || Moving) { Anchor=false; return Action::None; }
        if (!Anchor) { Anchor=true; NextHover=Now+HoverInterval; return Action::CaptureHover; }
        if (Now>=NextHover) { NextHover=Now+HoverInterval; return Action::Hover; }
        return Action::None;
    }
};
struct PounceSurface { double Pitch=0,Yaw=0,Roll=-90; };
inline PounceSurface Surface(double Yaw) {
    // UE's +90 roll points Right down: IsGroundRotation treats Right.Z < -0.9
    // as ground. -90 points Right up; yaw+90 keeps the normal opposite travel.
    return {0,std::remainder(Yaw+90,360.0),-90};
}
inline bool PointState(int Movement,int Latch) { return (Movement==3 && Latch==0) || (Latch==2 && (Movement==0 || Movement==3)); }
enum class PointBlock { None, Unavailable, State, JumpRequested, JumpVelocity };
inline PointBlock PointGate(int Movement,int Latch,double Requested,double CachedSpeed,double ActiveSpeed) {
    if (!std::isfinite(Requested) || (Requested!=0 && Requested!=1) ||
        !std::isfinite(CachedSpeed) || CachedSpeed<0 || !std::isfinite(ActiveSpeed) || ActiveSpeed<0)
        return PointBlock::Unavailable;
    if (!PointState(Movement,Latch)) return PointBlock::State;
    if (Requested!=0) return PointBlock::JumpRequested;
    // CachedLatchJumpVelocity retains the previous launch even after the active
    // velocity reaches zero. It is diagnostic data, not a pending-action flag.
    if (ActiveSpeed>1) return PointBlock::JumpVelocity;
    return PointBlock::None;
}
inline AimPoint Point(double X,double Y,double Z,double Yaw,double Pitch,double Metres) {
    if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z) || !std::isfinite(Yaw) ||
        !std::isfinite(Pitch) || !std::isfinite(Metres) ||
        Metres<10 || Metres>300) return {};
    // Submit the configured experimental distance; the server validates its own range.
    const double Distance=Metres*100;
    const double A=std::remainder(Yaw,360.0)*0.017453292519943295;
    const double P=std::remainder(Pitch,360.0)*0.017453292519943295;
    AimPoint V{X+std::cos(P)*std::cos(A)*Distance,Y+std::cos(P)*std::sin(A)*Distance,
        Z+std::sin(P)*Distance,Distance,true};
    V.Valid=std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
    return V;
}
inline PhoenixAirLatch::ReleaseVector Target(double Yaw,double H,double Up) {
    if (!std::isfinite(H) || !std::isfinite(Up) || H<5 || H>30 || Up<0 || Up>10) return {};
    return PhoenixAirLatch::ReleaseVelocity(Yaw,H,Up);
}
template<class T,size_t N> struct Queue {
    std::array<T,N> Items{};
    size_t Count=0, Dropped=0;
    void Push(const T& V) { if (Count<N) Items[Count++]=V; else ++Dropped; }
    void Clear() { Count=Dropped=0; }
};
}
