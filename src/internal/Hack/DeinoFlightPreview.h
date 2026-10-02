#pragma once
#include "DeinoFlightRules.h"
#include <array>
#include "DeinoFlightMeasured.h"
namespace PhoenixDeinoFlight {
struct Prediction {
    struct Point { double H=0,Z=0; };
    std::array<Point,33> Path{};
    double Distance=0,Height=0,Peak=0;
    bool Valid=false,ServerBaseline=false;
    double HorizontalMargin=0,HeightMargin=0;
};
// Approximation of the observed animation delay and constant-vector decay.
// Does not simulate collision, server correction, or an exact stop on End RPC.
inline Prediction PredictComponents(double Horizontal,double Up,double Protection,double Decay=130,double Delay=.434) {
    Prediction P;
    if (!std::isfinite(Horizontal) || Horizontal<0 || Horizontal>900+1e-8 || !std::isfinite(Up) || Up< -300 || Up>300 ||
        !std::isfinite(Protection) || Protection<.1 || Protection>5 ||
        !std::isfinite(Decay) || Decay<=0 || !std::isfinite(Delay) || Delay<0 || Delay>2) return P;
    Horizontal=std::min(Horizontal,900.0);
    const double Duration=std::max(0.0,Protection-Delay),Speed=std::hypot(Horizontal,Up);
    if(Speed<=0) return P;
    for (size_t I=0;I<P.Path.size();++I) {
        const double T=Duration*I/(P.Path.size()-1),Powered=std::min(T,Speed/Decay);
        const double Travel=Speed*Powered-.5*Decay*Powered*Powered;
        // Gravity after launch; preview assumes initially stationary hover.
        P.Path[I]={Travel*Horizontal/Speed,Travel*Up/Speed-9.8*T*T};
        P.Peak=std::max(P.Peak,P.Path[I].Z);
    }
    P.Distance=P.Path.back().H; P.Height=P.Path.back().Z; P.Valid=true; return P;
}
inline Prediction Predict(double H,double Up,double Protection,double Decay=130,double Delay=.434) {
    return AirVector(0,H,Up).Valid?PredictComponents(30*H,Up,Protection,Decay,Delay):Prediction{};
}
// Never extrapolate the recorded baseline to unmeasured parameters or moving starts.
inline Prediction PreviewVector(const PhoenixAirLatch::ReleaseVector& V,double Protection,double HoverMs,bool Stationary) {
    if(!V.Valid || !std::isfinite(V.X) || !std::isfinite(V.Y) || !std::isfinite(V.Z)) return {};
    const double Horizontal=std::hypot(V.X,V.Y)/100,Up=V.Z/100;
    auto P=PredictComponents(Horizontal,Up,Protection);
    if (!P.Valid) return P;
    if(Stationary && std::isfinite(HoverMs) && std::abs(Horizontal-90)<.03 && std::abs(Up-5)<.001 &&
        std::abs(Protection-.8)<.001 && std::abs(HoverMs-10)<.001) {
        P.Peak=0;
        for(size_t I=0;I<P.Path.size();++I) {
            P.Path[I]={Measured::Path[I].H,Measured::Path[I].Z};
            P.Peak=std::max(P.Peak,P.Path[I].Z);
        }
        P.Distance=P.Path.back().H; P.Height=P.Path.back().Z; P.ServerBaseline=true;
        P.HorizontalMargin=Measured::HorizontalMargin; P.HeightMargin=Measured::HeightMargin;
    } else {
        P.HorizontalMargin=std::max(2.0,P.Distance*.6); P.HeightMargin=std::max(2.0,std::abs(P.Height)*.6);
    }
    return P;
}
inline Prediction Preview(double H,double Up,double Protection,double HoverMs,bool Stationary) {
    return PreviewVector(AirVector(0,H,Up),Protection,HoverMs,Stationary);
}
struct Calibration {
    double H=0,Up=0,Protection=0,Ratio=1,Bias=0,LastDistance=0,LastHeight=0;
    unsigned Count=0;
    bool Matches(double A,double B,double C) const {
        return Count && std::abs(H-A)<.001 && std::abs(Up-B)<.001 && std::abs(Protection-C)<.001;
    }
    bool Add(double A,double B,double C,double Distance,double Height) {
        const auto P=Predict(A,B,C);
        if (!P.Valid || P.Distance<1 || !std::isfinite(Distance) || !std::isfinite(Height) ||
            Distance<.25*P.Distance || Distance>2*P.Distance || std::abs(Height-P.Height)>100) return false;
        if (!Matches(A,B,C)) *this={};
        const double Weight=Count?.25:1;
        Ratio+=(Distance/P.Distance-Ratio)*Weight; Bias+=(Height-P.Height-Bias)*Weight;
        H=A; Up=B; Protection=C; LastDistance=Distance; LastHeight=Height; ++Count; return true;
    }
    void Apply(Prediction& P,double A,double B,double C) const {
        if (!P.Valid || !Matches(A,B,C)) return;
        P.Peak=0;
        for(size_t I=0;I<P.Path.size();++I) {
            P.Path[I].H*=Ratio; P.Path[I].Z+=Bias*I/(P.Path.size()-1);
            P.Peak=std::max(P.Peak,P.Path[I].Z);
        }
        P.Distance=P.Path.back().H; P.Height=P.Path.back().Z;
    }
};
}
