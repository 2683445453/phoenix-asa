#pragma once
#include <algorithm>
#include <cmath>
namespace PhoenixMotion {
    inline float Approach(float Value,float Target,float Delta,float Duration=.14f) {
        if (!std::isfinite(Delta) || Delta<=0) return Value;
        const float Step=std::min(Delta,.1f)/Duration;
        return Value<Target?std::min(Target,Value+Step):std::max(Target,Value-Step);
    }
    struct PageFade {
        int Current=0, Pending=0;
        float Alpha=1;
        void Request(int Page) { Pending=Page; }
        void Snap(int Page) { Current=Pending=Page; Alpha=1; }
        void Tick(float Delta) {
            Alpha=Approach(Alpha,Pending==Current?1.f:0.f,Delta,.12f);
            if (Alpha==0 && Current!=Pending) Current=Pending;
        }
    };
}
