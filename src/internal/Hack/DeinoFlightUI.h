#pragma once
#include <array>
#include <mutex>
#include <cstdint>
#include <algorithm>
// Cross-thread exchange: window/render code never touches a game object.
namespace PhoenixDeinoUI {
struct View {
    uint64_t Time=0;
    bool Available=false,Panel=false,Air=false,Preview=false,Suppressed=false;
    int Selected=0,NativeArc=0;
    bool ServerBaseline=false;
    bool AimJump=false;
    double EffectiveUp=0,EffectiveHorizontal=0,Pitch=0;
    double HeightMargin=0;
    std::array<float,6> Values{};
    std::array<unsigned,4> Keys{};
    struct Point { float X=0,Y=0; bool Valid=false; };
    std::array<Point,33> Path{};
    Point Low,High;
    double Distance=0,Height=0,Peak=0,Uncertainty=0,LastDistance=0,LastHeight=0;
    unsigned Samples=0;
};
inline std::mutex Mutex;
inline View Current;
struct Commands { bool Toggle=false; int Preset=0,Field=0,WheelH=0,WheelUp=0,WheelSelected=0; };
inline Commands Pending;
inline View Read() { std::scoped_lock L(Mutex); return Current; }
inline void Publish(const View& V) { std::scoped_lock L(Mutex); Current=V; }
inline void Clear() { std::scoped_lock L(Mutex); Current={}; Pending={}; }
inline Commands Take() { std::scoped_lock L(Mutex); auto C=Pending; Pending={}; return C; }
inline bool Fresh(const View& V,uint64_t Now) { return V.Available && Now>=V.Time && Now-V.Time<200; }
// Windows-independent input routing for tests. HeldH/Up are configured wheel modifiers.
inline bool Input(unsigned Key,bool Down,bool Repeat,int Wheel,bool HeldH,bool HeldUp,uint64_t Now) {
    std::scoped_lock L(Mutex);
    if (!Fresh(Current,Now)) return false;
    if(Wheel && (Current.Panel || HeldH || HeldUp)) {
        auto& Delta=HeldH?Pending.WheelH:HeldUp?Pending.WheelUp:Pending.WheelSelected;
        Delta=std::clamp(Delta+Wheel,-1200,1200); return true;
    }
    if(Key && (Key==Current.Keys[0] || Key==Current.Keys[1] || Key==Current.Keys[2] || Key==Current.Keys[3] ||
        (Current.Panel && (Key==9 || Key==32)))) {
        if(Down && !Repeat) {
            if(Key==Current.Keys[0]) Pending.Toggle=!Pending.Toggle;
            else if(Key==Current.Keys[1]) Pending.Preset=std::min(Pending.Preset+1,3);
            else if(Current.Panel && Key==9) ++Pending.Field;
        }
        return true;
    }
    return false;
}
}
