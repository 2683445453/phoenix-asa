#include "../src/internal/Hack/FeatureExpansionRules.h"
#include "../src/internal/Hack/NativeMountRules.h"
#include "../src/internal/Hack/DeinoAirLatchRules.h"
#include "../src/internal/Hack/DeinoGlideProbeRules.h"
#include "../src/internal/Hack/DeinoActionRules.h"
#include "../src/internal/Hack/DeinoFlightRules.h"
#include "../src/internal/Hack/DeinoFlightPreview.h"
#include "../src/internal/Hack/DeinoFlightUI.h"
#include "../src/internal/Menu/FeatureShortcuts.h"
#include "../src/internal/Config/ConfigManager.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>

int main() {
    const auto [BeforeSeamMin,BeforeSeamMax]=PhoenixExpansion::MountedYawWindow(179.79);
    const auto [AfterSeamMin,AfterSeamMax]=PhoenixExpansion::MountedYawWindow(-179.79);
    assert(std::abs(BeforeSeamMax-BeforeSeamMin-340.0f)<0.001f && BeforeSeamMin<180.21f && 180.21f<BeforeSeamMax);
    assert(std::abs(AfterSeamMax-AfterSeamMin-340.0f)<0.001f && AfterSeamMin<-180.21f && -180.21f<AfterSeamMax);
    const auto [InvalidMin,InvalidMax]=PhoenixExpansion::MountedYawWindow(NAN);
    assert(InvalidMin==-170.0f && InvalidMax==170.0f);
    using namespace PhoenixDeinoAction;
    using CombinedFlight=PhoenixDeinoFlight::Control;
    PhoenixDeinoFlight::InputGate Focus;
    CombinedFlight Background;
    CombinedFlight FacingHover;
    assert(FacingHover.Poll(false,false,true,false,100,500)==CombinedFlight::Action::Hover);
    assert(!FacingHover.Hovering); // A failed RPC must not enable facing.
    FacingHover.Hovering=true;
    assert(FacingHover.Poll(false,false,true,false,101,500)==CombinedFlight::Action::None && FacingHover.Hovering);
    assert(FacingHover.Poll(false,true,true,false,102,500)==CombinedFlight::Action::None && !FacingHover.Hovering);
    FacingHover.Hovering=true; FacingHover.Jumped(103,800); assert(!FacingHover.Hovering);
    FacingHover.Hovering=true;
    assert(FacingHover.Poll(false,false,true,true,904,500)==CombinedFlight::Action::None && !FacingHover.Hovering);
    FacingHover.Hovering=true;
    assert(FacingHover.Poll(true,false,true,false,905,500)==CombinedFlight::Action::Jump && !FacingHover.Hovering);
    FacingHover.Hovering=true;
    assert(FacingHover.Poll(false,false,false,false,906,500)==CombinedFlight::Action::None && !FacingHover.Hovering);
    assert(Focus.Poll(true,false));
    assert(Background.Poll(false,false,true,false,0,10)==CombinedFlight::Action::Hover);
    Background.Jumped(100,800);
    // Opening a menu or losing focus preserves protection and masks external keys.
    assert(!Focus.Poll(false,true));
    assert(Background.Poll(false,false,true,false,500,10)==CombinedFlight::Action::None);
    assert(Background.Poll(false,false,true,false,900,10)==CombinedFlight::Action::Hover);
    assert(!Focus.Poll(true,true)); // Holding Space across the focus boundary cannot jump.
    assert(!Focus.Poll(true,false)); // Release rearms; it does not launch.
    assert(Background.Poll(false,false,true,false,910,10)==CombinedFlight::Action::Hover);
    assert(Focus.Poll(true,true));
    assert(Background.Poll(true,false,true,false,920,10)==CombinedFlight::Action::Jump);
    assert(!Focus.Poll(false,false));
    assert(Background.Poll(false,false,false,false,930,10)==CombinedFlight::Action::None);
    const auto Horizontal=PhoenixDeinoFlight::AirVector(0,30,5);
    assert(Horizontal.Valid && Horizontal.X==90000 && Horizontal.Y==0 && Horizontal.Z==500);
    const auto Right=PhoenixDeinoFlight::AirVector(90,1,0);
    assert(Right.Valid && std::abs(Right.X)<1e-8 && Right.Y==3000 && Right.Z==0);
    assert(PhoenixDeinoFlight::AirVector(720,1,30).Z==3000);
    assert(!PhoenixDeinoFlight::AirVector(0,100,5).Valid && !PhoenixDeinoFlight::AirVector(0,30.01,5).Valid);
    assert(!PhoenixDeinoFlight::AirVector(NAN,1,5).Valid && !PhoenixDeinoFlight::AirVector(0,1,INFINITY).Valid);
    assert(PhoenixDeinoFlight::AirVector(0,1,300).Z==30000);
    assert(!PhoenixDeinoFlight::AirVector(0,1,300.01).Valid);
    assert(PhoenixDeinoFlight::AirVector(0,1,-300).Z==-30000);
    assert(!PhoenixDeinoFlight::AirVector(0,1,-300.01).Valid);
    using PhoenixDeinoFlight::AimVector;
    for(double Yaw : {0.0,90.0,180.0,-70.0,1080.0})
        for(double Pitch : {-90.0,-45.0,0.0,45.0,90.0,315.0})
            for(double Speed : {1.0,30.0,300.0}) {
                const auto V=AimVector(Yaw,Pitch,Speed);
                assert(V.Valid && std::abs(std::hypot(std::hypot(V.X,V.Y),V.Z)-100*Speed)<1e-8);
                const auto P=PhoenixDeinoFlight::PreviewVector(V,.8,10,true);
                assert(P.Valid);
                for(const auto& Point:P.Path) assert(std::isfinite(Point.H) && std::isfinite(Point.Z));
            }
    const auto Diagonal=AimVector(0,45,30),Wrapped=AimVector(0,315,30);
    assert(std::abs(Diagonal.X-Diagonal.Z)<1e-8 && Diagonal.Y==0 && Diagonal.Z>0);
    assert(std::abs(Wrapped.X-Diagonal.X)<1e-8 && std::abs(Wrapped.Z+Diagonal.Z)<1e-8);
    const auto ViewRight=AimVector(90,0,30);
    assert(std::abs(ViewRight.X)<1e-8 && ViewRight.Y==3000 && ViewRight.Z==0);
    for(double Pitch : {-90.0,90.0}) {
        const auto V=AimVector(20,Pitch,30);
        assert(V.X==0 && V.Y==0 && std::abs(V.Z)==3000);
        const auto P=PhoenixDeinoFlight::PreviewVector(V,.8,10,true);
        assert(P.Valid && P.Distance==0 && (Pitch<0?P.Height<0:P.Height>0));
    }
    assert(!AimVector(NAN,0,30).Valid && !AimVector(0,INFINITY,30).Valid);
    assert(!AimVector(0,0,NAN).Valid && !AimVector(0,0,0).Valid && !AimVector(0,0,301).Valid);
    assert(!PhoenixDeinoFlight::PreviewVector({0,0,0,true},.8,10,true).Valid);
    assert(!PhoenixDeinoFlight::PreviewVector({},.8,10,true).Valid);
    assert(PhoenixDeinoFlight::PreviewVector(PhoenixDeinoFlight::AirVector(111,30,300),.8,10,true).Valid);
    const auto DownPreview=PhoenixDeinoFlight::Preview(3,-5,.8,10,true);
    assert(DownPreview.Valid && !DownPreview.ServerBaseline && DownPreview.Height<0 && DownPreview.Peak==0);
    const auto Preview=PhoenixDeinoFlight::Predict(3,5,.8);
    assert(Preview.Valid && Preview.Distance>0 && Preview.Distance<90*.8);
    assert(PhoenixDeinoFlight::Predict(3,300,.8).Peak>Preview.Peak);
    assert(PhoenixDeinoFlight::Predict(3,0,.1).Distance==0);
    assert(!PhoenixDeinoFlight::Predict(3,5,NAN).Valid);
    assert(!PhoenixDeinoFlight::Predict(3,5,.8,0).Valid);
    const auto ServerProfile=PhoenixDeinoFlight::Preview(3,5,.8,10,true);
    assert(ServerProfile.Valid && ServerProfile.ServerBaseline);
    assert(std::abs(ServerProfile.Distance-30.924611527)<1e-7);
    assert(ServerProfile.HorizontalMargin>6 && ServerProfile.HeightMargin>=1);
    // Independent down-view group; neither its median nor its endpoint is used to fit.
    for(double Actual : {33.543824917,28.716188702,30.470084075}) {
        assert(std::abs(ServerProfile.Distance-Actual)<3);
        assert(std::abs(ServerProfile.Distance-Actual)<std::abs(Preview.Distance-Actual));
    }
    assert(!PhoenixDeinoFlight::Preview(3,5,.8,10,false).ServerBaseline);
    assert(!PhoenixDeinoFlight::Preview(3,5,.8,20,true).ServerBaseline);
    assert(!PhoenixDeinoFlight::Preview(3,6,.8,10,true).ServerBaseline);
    assert(!PhoenixDeinoFlight::Preview(4,5,.8,10,true).ServerBaseline);
    assert(!PhoenixDeinoFlight::Preview(3,5,1,10,true).ServerBaseline);
    assert(!PhoenixDeinoFlight::Preview(NAN,5,.8,10,true).Valid);
    for(const auto& Point:ServerProfile.Path) assert(std::isfinite(Point.H) && std::isfinite(Point.Z));
    PhoenixDeinoFlight::Calibration Cal;
    assert(Cal.Add(3,5,.8,Preview.Distance*1.2,Preview.Height+1));
    auto Calibrated=Preview; Cal.Apply(Calibrated,3,5,.8);
    assert(std::abs(Calibrated.Distance-Preview.Distance*1.2)<1e-6);
    assert(std::abs(Calibrated.Height-Preview.Height-1)<1e-6);
    assert(!Cal.Matches(3,5,1) && !Cal.Add(3,5,.8,1e6,0));
    PhoenixDeinoUI::View UI; UI.Available=true; UI.Time=100; UI.Keys={118,119,120,121};
    PhoenixDeinoUI::Publish(UI);
    assert(PhoenixDeinoUI::Input(118,true,false,0,false,false,101));
    assert(PhoenixDeinoUI::Input(118,true,true,0,false,false,102));
    assert(PhoenixDeinoUI::Take().Toggle); // repeats do not toggle again
    assert(!PhoenixDeinoUI::Input(0,false,false,120,false,false,102));
    assert(PhoenixDeinoUI::Input(0,false,false,120,true,false,103));
    assert(PhoenixDeinoUI::Take().WheelH==120);
    assert(!PhoenixDeinoUI::Input(118,true,false,0,false,false,301)); // stale focus/mount
    UI.Panel=true; PhoenixDeinoUI::Publish(UI);
    assert(PhoenixDeinoUI::Input(32,true,false,0,false,false,104));
    assert(PhoenixDeinoUI::Input(9,true,false,0,false,false,104));
    assert(PhoenixDeinoUI::Take().Field==1);
    PhoenixDeinoUI::Clear();
    assert(!PhoenixDeinoUI::Read().Available);
    CombinedFlight F;
    assert(F.Poll(true,false,true,false,0,10)==CombinedFlight::Action::None); // held on enable
    assert(F.Poll(false,false,true,false,1,10)==CombinedFlight::Action::Hover);
    assert(F.Poll(false,false,true,false,10,10)==CombinedFlight::Action::None);
    assert(F.Poll(false,false,true,false,11,10)==CombinedFlight::Action::Hover);
    assert(F.Poll(false,false,true,false,1000,10)==CombinedFlight::Action::Hover);
    assert(F.Poll(false,false,true,false,1000,10)==CombinedFlight::Action::None); // no catch-up
    assert(F.Poll(true,false,true,false,1001,10)==CombinedFlight::Action::Jump);
    F.Jumped(1001,800);
    assert(F.Poll(false,false,true,false,1200,10)==CombinedFlight::Action::None);
    assert(F.Poll(true,false,true,false,1300,10)==CombinedFlight::Action::None); // early edge consumed
    assert(F.Poll(true,false,true,false,1801,10)==CombinedFlight::Action::None); // not delayed
    assert(F.Poll(false,false,true,false,1802,10)==CombinedFlight::Action::Hover);
    assert(F.Poll(false,true,true,false,1900,10)==CombinedFlight::Action::None);
    assert(F.Poll(true,true,true,false,1901,10)==CombinedFlight::Action::Jump); // direction keys only suppress hover
    F.Jumped(1901,500);
    assert(F.Poll(false,false,false,false,1902,10)==CombinedFlight::Action::None && F.AirUntil==0);
    assert(F.Poll(true,false,false,false,2000,10)==CombinedFlight::Action::None); // ground press belongs to native path
    assert(F.Poll(true,false,true,true,2100,10)==CombinedFlight::Action::None); // no duplicate at takeoff
    assert(F.Poll(false,false,true,true,2200,10)==CombinedFlight::Action::None);
    assert(F.Poll(false,false,true,false,3000,10)==CombinedFlight::Action::Hover);
    assert(!g_Config::bDeinoFlight && g_Config::DeinoHoverIntervalMs==10 && g_Config::DeinoAirHorizontalMultiplier==1);
    Trigger Auto;
    assert(!Auto.Take(true,1000,true));
    assert(!Auto.Take(false,1001,true) && Auto.Take(true,1002,true));
    assert(!Auto.Take(true,5000,true)); // holding never repeats
    assert(!Auto.Take(false,5001,true) && !Auto.Take(true,5002,false));
    assert(!Auto.Take(true,5003,true)); // rejected edge is never deferred
    assert(!Auto.Take(false,5004,true) && Auto.Take(true,5005,true));
    assert(!Auto.Take(false,5006,true) && !Auto.Take(true,5007,true));
    assert(!Auto.Take(false,8005,true) && Auto.Take(true,8006,true));
    assert(Mode(true,false,false,false,false)==1);
    assert(Mode(false,true,false,false,false)==2);
    assert(Mode(false,false,true,true,false)==3);
    for (int Bits=0;Bits<32;++Bits) {
        const int M=Mode(Bits&1,Bits&2,Bits&4,Bits&8,Bits&16);
        if (M) assert(Bits==1 || Bits==2 || Bits==12);
    }
    for (int Bits=0;Bits<64;++Bits) {
        const int M=Mode(Bits&1,Bits&2,Bits&4,Bits&8,Bits&16,Bits&32);
        assert(bool(M)==(Bits==1 || Bits==2 || Bits==12 || Bits==32));
        if (Bits==32) assert(M==4);
    }
    auto PointAhead=Point(100,200,300,0,0,30);
    assert(PointAhead.Valid && PointAhead.X==3100 && PointAhead.Y==200 && PointAhead.Z==300);
    auto PointUp=Point(0,0,0,90,30,30);
    assert(PointUp.Valid && std::abs(PointUp.X)<1e-8 && std::abs(PointUp.Z-1500)<1e-8);
    assert(std::abs(std::hypot(std::hypot(PointUp.X,PointUp.Y),PointUp.Z)-3000)<1e-8);
    const auto Far=Point(100,200,300,0,0,300);
    assert(Far.Valid && Far.Distance==30000 && Far.X==30100 && Far.Y==200 && Far.Z==300);
    assert(Point(0,0,0,0,0,10).Distance==1000);
    assert(Point(0,0,0,0,0,35).Distance==3500);
    assert(Point(0,0,0,0,0,100).Distance==10000);
    assert(!Point(0,0,0,0,0,9.99).Valid && !Point(0,0,0,0,0,300.01).Valid);
    assert(!Point(NAN,0,0,0,0,30).Valid && !Point(0,0,0,0,NAN,30).Valid);
    assert(!Point(0,0,0,0,0,INFINITY).Valid);
    // Captured IsGroundRotation: Right.Z < -0.9 || Up.Z > 0.6.
    // UE rotation basis gives Right.Z=-sin(roll)*cos(pitch), Up.Z=cos(roll)*cos(pitch).
    constexpr double Rad=0.017453292519943295;
    for (double Yaw : {-720.0,-180.0,-90.0,0.0,45.0,180.0,720.0}) {
        const auto Wall=Surface(Yaw);
        const double P=Wall.Pitch*Rad,R=Wall.Roll*Rad,Y=Wall.Yaw*Rad;
        const double RightZ=-std::sin(R)*std::cos(P),UpZ=std::cos(R)*std::cos(P);
        assert(!(RightZ<-.9 || UpZ>.6));
        const double UpX=std::cos(R)*std::sin(P)*std::cos(Y)+std::sin(R)*std::sin(Y);
        const double UpY=std::cos(R)*std::sin(P)*std::sin(Y)-std::sin(R)*std::cos(Y);
        assert(std::abs(UpX+std::cos(Yaw*Rad))<1e-8 && std::abs(UpY+std::sin(Yaw*Rad))<1e-8);
        assert(std::abs(UpZ)<1e-8);
    }
    for (int Bits=0;Bits<128;++Bits) {
        const int M=Mode(Bits&1,Bits&2,Bits&4,Bits&8,Bits&16,Bits&32,Bits&64);
        assert(bool(M)==(Bits==1 || Bits==2 || Bits==12 || Bits==32 || Bits==96));
        if (Bits==96) assert(M==4); // point + big jump only
    }
    Trigger PointKey;
    using KeyResult=Trigger::Result;
    assert(PointKey.Poll(false,1000,false)==KeyResult::Idle);
    assert(PointKey.Poll(true,1001,false)==KeyResult::Blocked);
    assert(PointKey.Next==0);
    assert(PointKey.Poll(true,1002,true)==KeyResult::Idle); // blocked presses are never queued
    assert(PointKey.Poll(false,1003,true)==KeyResult::Idle);
    assert(PointKey.Poll(true,1004,true)==KeyResult::Submit);
    assert(PointKey.Poll(false,1005,true)==KeyResult::Idle);
    assert(PointKey.Poll(true,1006,true)==KeyResult::Cooldown);
    assert(PointKey.Next==4004);
    assert(PointKey.Poll(true,4004,true)==KeyResult::Idle); // holding through cooldown cannot submit
    assert(PointKey.Poll(false,4005,true)==KeyResult::Idle);
    assert(PointKey.Poll(true,4006,true)==KeyResult::Submit);
    Trigger NoCooldown;
    NoCooldown.Next=99999; // zero interval also ignores a previous deadline
    assert(NoCooldown.Poll(false,5000,true,0)==KeyResult::Idle);
    assert(NoCooldown.Poll(true,5001,true,0)==KeyResult::Submit && NoCooldown.Next==0);
    assert(NoCooldown.Poll(true,5002,true,0)==KeyResult::Idle);
    assert(NoCooldown.Poll(false,5003,true,0)==KeyResult::Idle);
    assert(NoCooldown.Poll(true,5004,true,0)==KeyResult::Submit);
    assert(NoCooldown.Poll(false,5005,false,0)==KeyResult::Idle);
    assert(NoCooldown.Poll(true,5006,false,0)==KeyResult::Blocked);
    assert(NoCooldown.Poll(true,5007,true,0)==KeyResult::Idle);
    assert(NoCooldown.Poll(false,5008,true,0)==KeyResult::Idle);
    assert(NoCooldown.Poll(true,5009,true,0)==KeyResult::Submit);
    using FlightAction=FlightControl::Action;
    assert(TakeoffProtectionMs(.1)==100 && TakeoffProtectionMs(1.25)==1250 && TakeoffProtectionMs(5)==5000);
    assert(TakeoffProtectionMs(-1)==100 && TakeoffProtectionMs(100)==5000);
    assert(TakeoffProtectionMs(NAN)==1000 && TakeoffProtectionMs(INFINITY)==1000);
    TakeoffControl Adjustable;
    Adjustable.Protect(true,false,0,1250);
    assert(Adjustable.Protect(false,true,100,1250));
    assert(Adjustable.Protect(false,true,1349,5000)); // airborne setting changes do not extend the current window
    assert(!Adjustable.Protect(false,true,1350,5000));
    TakeoffControl Takeoff;
    assert(!Takeoff.Press(false,true,0));
    assert(Takeoff.Press(true,true,1));
    Takeoff.Started(1,350);
    assert(!Takeoff.Press(true,true,2)); // exactly one native launch per press
    assert(Takeoff.Mask(true,2000)); // holding cannot arm another native launch
    assert(Takeoff.Mask(false,2010));
    assert(Takeoff.Mask(false,2109) && !Takeoff.Mask(false,2110)); // consume physical release
    assert(!Takeoff.Protect(true,false,2200,350));
    assert(Takeoff.Protect(false,true,2210,350)); // actual takeoff starts the grace window
    assert(Takeoff.Protect(false,true,2559,350) && !Takeoff.Protect(false,true,2560,350));
    assert(!Takeoff.Press(false,false,2600) && !Takeoff.Press(true,false,2601)); // air press is not ground jump
    Takeoff={};
    assert(!Takeoff.Mask(false,2700) && !Takeoff.Press(true,true,2701)); // reset while held cannot launch
    FlightControl Flight;
    assert(Flight.Poll(false,false,false,false,0)==FlightAction::None);
    assert(Flight.Poll(true,false,false,false,1)==FlightAction::None); // native ground press
    assert(Flight.Poll(true,false,true,false,2)==FlightAction::None); // hold across takeoff
    assert(Flight.Poll(false,false,true,false,3)==FlightAction::CaptureHover);
    assert(Flight.Poll(false,false,true,false,52)==FlightAction::None);
    assert(Flight.Poll(false,false,true,false,53)==FlightAction::Hover);
    assert(Flight.Poll(false,false,true,false,54)==FlightAction::None);
    assert(Flight.Poll(false,false,true,false,303)==FlightAction::Hover);
    assert(Flight.Poll(true,false,true,true,310)==FlightAction::None); // busy: retain one air press
    assert(Flight.Poll(true,false,true,false,400)==FlightAction::Dash);
    assert(Flight.Poll(true,false,true,false,401)==FlightAction::None); // no hold-repeat
    assert(Flight.Poll(false,false,true,true,402)==FlightAction::None); // finish pounce before hold
    assert(Flight.Poll(false,false,true,false,500)==FlightAction::CaptureHover);
    assert(Flight.Poll(false,true,true,false,501)==FlightAction::None && !Flight.Anchor);
    assert(Flight.Poll(true,false,true,true,510)==FlightAction::None);
    assert(Flight.Poll(true,false,true,false,2100)==FlightAction::None && !Flight.Pending); // expired
    Flight={};
    assert(Flight.Poll(true,false,true,false,2200)==FlightAction::None); // reopening while held
    assert(Flight.Poll(false,false,false,false,2300)==FlightAction::None && !Flight.Anchor);
    FlightControl Frequency;
    int Renewals=0;
    for (uint64_t T=0;T<=1000;++T) {
        const auto A=Frequency.Poll(false,false,true,false,T);
        Renewals+=A==FlightAction::Hover || A==FlightAction::CaptureHover;
    }
    assert(Renewals==21); // initial capture plus at most 20 renewals per second
    assert(Frequency.Poll(false,false,true,true,2000)==FlightAction::None); // feedback wait still wins
    PointReply Reply;
    // Replay the 16:29 captures: Falling/state 0, old ~5000 cm/s cache,
    // no pending request and zero active jump velocity. Both ascent and descent
    // must be eligible; the cached vector is not evidence of an active jump.
    const double OldCache=std::hypot(std::hypot(2424.16852793,3968.39853663),1837.17718326);
    assert(PointGate(3,0,0,OldCache,0)==PointBlock::None);
    assert(PointGate(0,2,0,OldCache,0)==PointBlock::None);
    assert(PointGate(3,0,1,OldCache,0)==PointBlock::JumpRequested);
    assert(PointGate(3,0,0,OldCache,100)==PointBlock::JumpVelocity);
    assert(PointGate(3,0,0,OldCache,1)==PointBlock::None);
    assert(PointGate(1,0,0,OldCache,0)==PointBlock::State);
    assert(PointGate(0,4,0,OldCache,0)==PointBlock::State);
    assert(PointGate(0,5,0,OldCache,0)==PointBlock::State);
    assert(PointGate(3,0,NAN,OldCache,0)==PointBlock::Unavailable);
    assert(PointGate(3,0,0,NAN,0)==PointBlock::Unavailable);
    assert(PointGate(3,0,0,OldCache,INFINITY)==PointBlock::Unavailable);
    assert(Reply.Update(100,0,0)==0);
    Reply.Begin(100,4);
    assert(Reply.Update(120,4,0)==0 && Reply.Waiting);
    assert(Reply.Update(150,4,4)==0 && Reply.Waiting);
    assert(Reply.Update(160,5,0)==1 && !Reply.Waiting); // immediate end feedback releases the request
    Reply.Begin(161,5);
    assert(Reply.Update(200,5,5)==2 && !Reply.Waiting);
    Reply.Begin(300,5);
    assert(Reply.Update(2299,5,0)==0 && Reply.Waiting);
    assert(Reply.Update(2300,5,0)==3 && !Reply.Waiting); // no automatic resend
    assert(Reply.Update(3000,6,0)==0);
    Reply.Begin(3001,6); Reply={};
    assert(Reply.Update(3002,7,2)==0 && !Reply.Waiting); // disabled/menu clears pending request
    assert(-std::sin(90*Rad)<-.9); // old +90 roll selects the ground branch
    for (int Movement=0;Movement<7;++Movement) for (int State=0;State<6;++State)
        assert(PointState(Movement,State)==((Movement==3 && State==0) || (State==2 && (Movement==0 || Movement==3))));
    assert(!g_Config::bDeinoPointPounceTest && !g_Config::bDeinoPointZeroStart && g_Config::DeinoPointPounceDistance==30);
    const auto V=Target(90,15,4);
    assert(V.Valid && std::abs(V.X)<1e-8 && V.Y==1500 && V.Z==400);
    assert(Target(0,30,10).Valid && Target(0,5,0).Valid);
    assert(!Target(0,30.1,4).Valid && !Target(0,15,10.1).Valid && !Target(0,4.9,4).Valid);
    assert(!Target(NAN,15,4).Valid && !Target(0,NAN,4).Valid && !Target(0,15,NAN).Valid);
    Queue<int,2> Events;
    Events.Push(1); Events.Push(2); Events.Push(3);
    assert(Events.Count==2 && Events.Dropped==1 && Events.Items[0]==1 && Events.Items[1]==2);
    Events.Clear(); Events.Push(4); assert(Events.Count==1 && Events.Dropped==0 && Events.Items[0]==4);
    assert(!g_Config::bDeinoAutoJumpTest && !g_Config::bDeinoLatchTrace);

    PhoenixGlideProbe::Window Probe;
    assert(!Probe.Take(1000) && !Probe.Expired(1000));
    Probe.Begin(1000);
    assert(Probe.Take(1000) && !Probe.Take(1049) && Probe.Take(1050));
    assert(Probe.Take(5000) && !Probe.Take(5000)); // stalls never produce a burst
    assert(!Probe.Expired(120999) && Probe.Expired(121000) && !Probe.Take(121000));
    Probe={}; assert(!Probe.Take(200000));
    auto Release=PhoenixAirLatch::ReleaseVelocity(90,30,6);
    assert(Release.Valid && std::abs(Release.X)<1e-8 && Release.Y==3000 && Release.Z==600);
    assert(PhoenixAirLatch::ReleaseVelocity(0,30,0).Z==0);
    assert(!PhoenixAirLatch::ReleaseVelocity(NAN,30,6).Valid);
    assert(!PhoenixAirLatch::ReleaseVelocity(0,30,-1).Valid);
    assert(!PhoenixAirLatch::ReleaseVelocity(0,INFINITY,6).Valid);
    for (int Mode : {0,1,2,3,5,6}) assert(PhoenixAirLatch::AllowsRenewalMode(Mode));
    for (int Mode : {-1,4,7,255}) assert(!PhoenixAirLatch::AllowsRenewalMode(Mode));
    PhoenixAirLatch::Landing Landing;
    assert(!Landing.Confirm(1000,true)); // a transient contact is not landing
    assert(!Landing.Confirm(1249,true));
    assert(!Landing.Confirm(1250,false)); // airborne resets confirmation
    assert(!Landing.Confirm(1500,true));
    assert(Landing.Confirm(1750,true));
    PhoenixAirLatch::Renewal Renewal;
    Renewal.Submitted(1000);
    assert(!Renewal.Take(1249,0,true));
    assert(!Renewal.Take(1250,2,true)); // never replace an active latch
    assert(!Renewal.Take(1250,0,false)); // never renew on landing
    assert(!Renewal.Take(1250,-1,true)); // failed state read
    assert(Renewal.Take(1250,0,true));
    assert(!Renewal.Take(1250,0,true));
    assert(Renewal.Take(10000,0,true)); // one attempt after a stalled frame
    assert(!Renewal.Take(10001,0,true));
    using namespace PhoenixExpansion;
    using PhoenixNativeMount::Boost;
    auto Ground=Boost({100,200,-300},90,true,2000,1000,6000);
    assert(Ground.Valid && Ground.Target.X==100 && Ground.Target.Y==200 && Ground.Target.Z==2000);
    assert(Ground.Delta.Z==2300);
    auto Air=Boost({100,200,-300},90,false,2000,1000,6000);
    assert(Air.Valid && std::abs(Air.Target.X-100)<1e-9 && Air.Target.Y==1200 && Air.Delta.Z==0);
    auto Cap=Boost({5900,0,100},0,false,2000,1000,6000);
    assert(Cap.Valid && Cap.Target.X==6000 && Cap.Target.Z==100);
    auto Fast=Boost({7000,0,100},0,false,2000,1000,6000);
    assert(Fast.Valid && Fast.Target.X==7000);
    assert(!Boost({NAN,0,0},0,false,2000,1000,6000).Valid);
    assert(!Boost({0,0,0},INFINITY,false,2000,1000,6000).Valid);
    PhoenixNativeMount::Press Edge;
    assert(!Edge.Take(true,1000)); // no activation from an already held key
    assert(!Edge.Take(false,1001) && Edge.Take(true,1002));
    assert(!Edge.Take(true,1300)); // holding never repeats
    assert(!Edge.Take(false,1301) && Edge.Take(true,1302));
    assert(!Edge.Take(false,1303) && !Edge.Take(true,1304)); // debounce

    assert(ClassifyUse("Unclaim") == Use::None);
    assert(ClassifyUse("取消认领") == Use::None);
    assert(ClassifyUse("Turn Off") == Use::None);
    assert(ClassifyUse("Deactivate") == Use::None);
    assert(ClassifyUse("Destroy") == Use::None);
    assert(ClassifyUse("Claim") == Use::Claim);
    assert(ClassifyUse("认养") == Use::Claim);
    assert(ClassifyUse("Cuddle") == Use::Imprint);
    assert(ClassifyUse("Ride") == Use::Ride);
    assert(ClassifyUse("Turn On") == Use::Activate);
    assert(ClassifyUse("Extract Embryo") == Use::Extract);
    assert(ClassifyUse("Remove Bola") == Use::RemoveBola);
    assert(MatchesSupply(Supply::Gacha,"PrimalItemConsumable_Seed_PlantSpeciesY_C"));
    assert(!MatchesSupply(Supply::Gacha,"PrimalItemConsumable_Seed_PlantSpeciesX_C"));
    assert(MatchesSupply(Supply::Baby,"PrimalItemConsumable_RawMeat_C"));
    assert(!MatchesSupply(Supply::Baby,"PrimalItemConsumable_CookedMeat_C"));
    assert(!MatchesSupply(Supply::Baby,"PrimalItemConsumable_SpoiledMeat_C"));
    assert(MatchesSupply(Supply::Crop,"PrimalItemConsumable_Fertilizer_Compost_C"));
    assert(MatchesSupply(Supply::Turret,"PrimalItemAmmo_AdvancedRifleBullet_C"));
    assert(!MatchesSupply(Supply::Turret,"PrimalItemAmmo_SimpleRifleBullet_C"));
    auto P = ProjectRadar(100,0,0,200); assert(P.Visible && std::abs(P.X)<1e-12 && P.Y == -.5);
    P = ProjectRadar(100,0,90,200); assert(P.Visible && std::abs(P.X+.5)<1e-12 && std::abs(P.Y)<1e-12);
    assert(!ProjectRadar(201,0,0,200).Visible);
    assert(!ProjectRadar(0,0,0,0).Visible);
    assert(!ProjectRadar(NAN,0,0,200).Visible);
    const auto Dir = std::filesystem::temp_directory_path()/"phoenix-feature-expansion-tests";
    std::filesystem::create_directories(Dir);
    auto& Config = ConfigManager::Get(); Config.Initialize(Dir.string());
    assert(!g_Config::bJackalopeBigJump && g_Config::JackalopeJumpMultiplier == 2);
    assert(!g_Config::bJackalopeChargeCap && !g_Config::bJackalopeFastCharge);
    g_Config::bJackalopeBigJump = true;
    g_Config::JackalopeJumpMultiplier = 2.5f;
    assert(!g_Config::bDeinonychusAirSpeed);
    g_Config::bDeinonychusAirSpeed = true;
    g_Config::DeinonychusAirSpeedMultiplier = 2.5f;
    g_Config::bFastAim = g_Config::bAutoFillGacha = g_Config::bRadar = true;
    g_Config::bTekNoHeat = g_Config::bDeinonychusBigJump = g_Config::bFloatingStructures = true;
    g_Config::DeinonychusJumpMultiplier = 3.25f;
    g_Config::RadarRangeMeters = 350; g_Config::RadarTeamColor[3] = .25f;
    g_Config::bJackalopeChargeCap = g_Config::bJackalopeFastCharge = true;
    g_Config::JackalopeChargeCapMultiplier = 3; g_Config::JackalopeChargeSpeedMultiplier = 7;
    assert(!g_Config::bNativeMountJump && !g_Config::bNativeMountHover && !g_Config::bDeinoAirLatch);
    g_Config::bNativeMountJump=g_Config::bNativeMountHover=g_Config::bNativeMountUseImpulse=true;
    assert(!g_Config::bDeinoAirLatch && !g_Config::bDeinoGlideProbe);
    g_Config::bDeinoGlideProbe=true;
    g_Config::bDeinoPointPounceTest=true; g_Config::bDeinoPointZeroStart=true; g_Config::DeinoPointPounceDistance=300; g_Config::bDeinoAutoVectorTest=true; g_Config::DeinoCandidateJumpMultiplier=3.8f; g_Config::DeinoAutoVectorMultiplier=100; g_Config::DeinoCandidateHoverSeconds=2; g_Config::DeinoCandidateIntervalMs=50; g_Config::DeinoTakeoffProtectionSeconds=2.25f;
    g_Config::bDeinoNativeArc=true; g_Config::DeinoPreset3Up=-300; g_Config::DeinoPanelFKey=11; g_Config::DeinoUpStep=20;
    g_Config::bDeinoFlight=true; g_Config::DeinoAirHorizontalMultiplier=30; g_Config::DeinoAirUpSpeed=-7; g_Config::DeinoHoverIntervalMs=10; g_Config::DeinoAirProtectionSeconds=1.2f;
    g_Config::bDeinoAimJump=true; g_Config::DeinoAimJumpSpeed=90; g_Config::DeinoAimSpeedStep=7;
    g_Config::bDeinoHoverFollowView=true;
    g_Config::bDeinoAutoJumpTest=true; g_Config::bDeinoLatchTrace=true;
    g_Config::DeinoAutoJumpSpeed=20; g_Config::DeinoAutoJumpUpSpeed=5;
    g_Config::bDeinoAirLatch=true; g_Config::DeinoAirLatchReleaseSpeed=45; g_Config::DeinoAirLatchReleaseUpSpeed=8;
    assert(!g_Config::bNativeMountPhysicsHook);
    g_Config::bNativeMountPhysicsHook=true;
    g_Config::NativeMountGroundSpeed=33; g_Config::NativeMountAirStep=12; g_Config::NativeMountHorizontalCap=90;
    assert(!g_Config::bGargantarClient);
    g_Config::bGargantarClient=g_Config::bGargantarReach=true;
    g_Config::GargantarReachMeters=27; g_Config::GargantarFlightMultiplier=2;
    assert(Config.SaveConfig("expansion.ini"));
    { std::ifstream In(Dir/"expansion.ini"); std::string Saved((std::istreambuf_iterator<char>(In)),{});
      assert(Saved.find("bDeinoAirLatch")==std::string::npos && Saved.find("bDeinoPointZeroStart")==std::string::npos);
      assert(Saved.find("bNativeMountJump")==std::string::npos && Saved.find("bDeinonychusBigJump")==std::string::npos);
      assert(Saved.find("DeinoAutoVectorMultiplier")==std::string::npos && Saved.find("DeinoPointPounceDistance")==std::string::npos);
      assert(Saved.find("bDeinoAutoVectorTest")==std::string::npos && Saved.find("bDeinoEndHoverTest")==std::string::npos); }
    g_Config::bNativeMountJump=g_Config::bNativeMountHover=false;
    g_Config::bDeinoAimJump=false; g_Config::DeinoAimJumpSpeed=30; g_Config::DeinoAimSpeedStep=5;
    g_Config::bDeinoHoverFollowView=false;
    g_Config::bDeinoAirLatch=false; g_Config::DeinoAirLatchReleaseSpeed=1; g_Config::DeinoAirLatchReleaseUpSpeed=0;
    g_Config::bDeinoGlideProbe=false;
    g_Config::bDeinoPointPounceTest=false; g_Config::bDeinoPointZeroStart=false; g_Config::DeinoPointPounceDistance=10; g_Config::bDeinoAutoVectorTest=false; g_Config::DeinoCandidateJumpMultiplier=1; g_Config::DeinoAutoVectorMultiplier=1; g_Config::DeinoTakeoffProtectionSeconds=.1f;
    g_Config::bDeinoAutoJumpTest=false; g_Config::bDeinoLatchTrace=false;
    g_Config::DeinoAutoJumpSpeed=5; g_Config::DeinoAutoJumpUpSpeed=0;
    g_Config::bNativeMountPhysicsHook=false;
    g_Config::NativeMountGroundSpeed=1;

    g_Config::bJackalopeChargeCap = g_Config::bJackalopeFastCharge = false;
    g_Config::JackalopeChargeCapMultiplier = g_Config::JackalopeChargeSpeedMultiplier = 1;
    g_Config::bJackalopeBigJump = false;
    g_Config::JackalopeJumpMultiplier = 1;
    g_Config::bDeinonychusAirSpeed = false;
    g_Config::DeinonychusAirSpeedMultiplier = 1;
    g_Config::bFastAim = g_Config::bAutoFillGacha = g_Config::bRadar = false;
    g_Config::bTekNoHeat = g_Config::bDeinonychusBigJump = g_Config::bFloatingStructures = false;
    g_Config::DeinonychusJumpMultiplier = 1;
    g_Config::RadarRangeMeters = 10; g_Config::RadarTeamColor[3] = 1;
    g_Config::bGargantarClient=false; g_Config::GargantarReachMeters=5;
    assert(Config.LoadConfig("expansion.ini"));
    assert(g_Config::bGargantarClient && g_Config::bGargantarReach && g_Config::GargantarReachMeters==27 && g_Config::GargantarFlightMultiplier==2);
    assert(g_Config::bJackalopeBigJump && g_Config::JackalopeJumpMultiplier == 2.5f);
    assert(!g_Config::bNativeMountJump && !g_Config::bNativeMountHover && !g_Config::bNativeMountUseImpulse);
    assert(!g_Config::bDeinoAirLatch && g_Config::DeinoAirLatchReleaseSpeed==30);
    assert(g_Config::DeinoAirLatchReleaseUpSpeed==6);
    assert(!g_Config::bDeinoGlideProbe);
    assert(!g_Config::bDeinoAutoJumpTest && !g_Config::bDeinoLatchTrace);
    assert(!g_Config::bDeinoPointPounceTest && !g_Config::bDeinoPointZeroStart && g_Config::DeinoPointPounceDistance==30);
    assert(g_Config::DeinoTakeoffProtectionSeconds==2.25f);
    assert(!g_Config::bDeinoAutoVectorTest && g_Config::DeinoCandidateJumpMultiplier==1);
    assert(g_Config::DeinoCandidateHoverSeconds==0 && g_Config::DeinoCandidateIntervalMs==100);
    assert(g_Config::DeinoAutoVectorMultiplier==1);
    assert(g_Config::bDeinoNativeArc);
    assert(g_Config::DeinoPreset3Up==-300 && g_Config::DeinoPanelFKey==11 && g_Config::DeinoUpStep==20);
    assert(g_Config::bDeinoFlight && g_Config::DeinoAirHorizontalMultiplier==30 && g_Config::DeinoAirUpSpeed==-7);
    assert(g_Config::bDeinoAimJump && g_Config::DeinoAimJumpSpeed==90 && g_Config::DeinoAimSpeedStep==7);
    assert(g_Config::bDeinoHoverFollowView);
    assert(g_Config::DeinoHoverIntervalMs==10 && g_Config::DeinoAirProtectionSeconds==1.2f);
    assert(g_Config::DeinoAutoJumpSpeed==15 && g_Config::DeinoAutoJumpUpSpeed==4);
    assert(!g_Config::bNativeMountPhysicsHook);
    assert(g_Config::NativeMountGroundSpeed==20 && g_Config::NativeMountAirStep==10 && g_Config::NativeMountHorizontalCap==60);

    assert(g_Config::bJackalopeChargeCap && g_Config::bJackalopeFastCharge);
    assert(g_Config::JackalopeChargeCapMultiplier == 3 && g_Config::JackalopeChargeSpeedMultiplier == 7);
    assert(!g_Config::bDeinonychusAirSpeed && g_Config::DeinonychusAirSpeedMultiplier == 2);
    assert(g_Config::bFastAim && g_Config::bAutoFillGacha && g_Config::bRadar);
    assert(g_Config::bTekNoHeat && !g_Config::bDeinonychusBigJump && g_Config::bFloatingStructures && g_Config::DeinonychusJumpMultiplier == 3.25f);
    assert(g_Config::RadarRangeMeters == 350 && g_Config::RadarTeamColor[3] == .25f);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::RadarRangeMeters=99999\ng_Config::HusbandryIntervalSeconds=-100\n"; }
    assert(Config.LoadConfig("expansion.ini"));
    assert(!g_Config::bFastAim && !g_Config::bAutoFillGacha && !g_Config::bRadar);
    assert(!g_Config::bTekNoHeat && !g_Config::bDeinonychusBigJump && !g_Config::bFloatingStructures);
    assert(g_Config::RadarRangeMeters == 1000 && g_Config::HusbandryIntervalSeconds == .5f);
    assert(!g_Config::bDeinoAimJump && g_Config::DeinoAirUpSpeed==5); // older configs retain manual behavior
    assert(!g_Config::bDeinoHoverFollowView);
    assert(!g_Config::bJackalopeBigJump && g_Config::JackalopeJumpMultiplier == 2);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::JackalopeJumpMultiplier=999\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::JackalopeJumpMultiplier == 20);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::JackalopeJumpMultiplier=-100\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::JackalopeJumpMultiplier == 1);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinoAutoVectorMultiplier=999\ng_Config::DeinoCandidateJumpMultiplier=999\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::DeinoAutoVectorMultiplier==1 && g_Config::DeinoCandidateJumpMultiplier==1);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinoAutoVectorMultiplier=-1\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::DeinoAutoVectorMultiplier==1);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinoPointPounceDistance=999\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::DeinoPointPounceDistance==30);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinoPointPounceDistance=1\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::DeinoPointPounceDistance==30);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinoAirHorizontalMultiplier=100\ng_Config::DeinoAirUpSpeed=400\ng_Config::DeinoHoverIntervalMs=1\ng_Config::DeinoAirProtectionSeconds=99\nbDeinoAutoVectorTest=1\n"; }
    assert(Config.LoadConfig("expansion.ini"));
    assert(g_Config::DeinoAirHorizontalMultiplier==30 && g_Config::DeinoAirUpSpeed==300);
    assert(g_Config::DeinoHoverIntervalMs==10 && g_Config::DeinoAirProtectionSeconds==5 && !g_Config::bDeinoAutoVectorTest);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinoAirUpSpeed=-400\ng_Config::DeinoPreset1Up=-400\ng_Config::DeinoAimJumpSpeed=999\ng_Config::DeinoAimSpeedStep=99\n"; }
    assert(Config.LoadConfig("expansion.ini"));
    assert(g_Config::DeinoAirUpSpeed==-300 && g_Config::DeinoPreset1Up==-300);
    assert(g_Config::DeinoAimJumpSpeed==300 && g_Config::DeinoAimSpeedStep==30);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::bDeinoPitchVertical=1\ng_Config::DeinoAirHorizontalMultiplier=3\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::bDeinoAimJump && g_Config::DeinoAimJumpSpeed==90);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::bDeinoAimJump=0\ng_Config::bDeinoPitchVertical=1\ng_Config::DeinoAimJumpSpeed=-50\n"; }
    assert(Config.LoadConfig("expansion.ini") && !g_Config::bDeinoAimJump && g_Config::DeinoAimJumpSpeed==1);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinoTakeoffProtectionSeconds=999\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::DeinoTakeoffProtectionSeconds==5);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinoTakeoffProtectionSeconds=-1\n"; }
    assert(Config.LoadConfig("expansion.ini") && g_Config::DeinoTakeoffProtectionSeconds==.1f);
    assert(!g_Config::bDeinonychusAirSpeed);
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::DeinonychusAirSpeedMultiplier=999\n"; }
    assert(Config.LoadConfig("expansion.ini"));
    assert(g_Config::DeinonychusAirSpeedMultiplier == 2); // retired key ignored
    g_Config::bDeinonychusAirSpeed = true;
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::JackalopeChargeCapMultiplier=999\ng_Config::JackalopeChargeSpeedMultiplier=999\n"; }
    assert(Config.LoadConfig("expansion.ini"));
    assert(g_Config::JackalopeChargeCapMultiplier == 20 && g_Config::JackalopeChargeSpeedMultiplier == 10);
    g_Config::bNoRecoil = g_Config::bFastBow = g_Config::bAutoClaimBabies = true;
    { std::ofstream O(Dir/"expansion.ini"); O << "g_Config::GargantarReachMeters=999\ng_Config::GargantarHoldSeconds=-1\ng_Config::GargantarTransitVisualSeconds=nan\n"; }
    assert(Config.LoadConfig("expansion.ini"));
    assert(g_Config::GargantarReachMeters==50 && g_Config::GargantarHoldSeconds==.02f && g_Config::GargantarTransitVisualSeconds==.4f);
    g_Config::bNoRecoil = g_Config::bFastBow = g_Config::bAutoClaimBabies = true;
    g_Config::bAutoExtractEmbryos = true;
    g_Config::bTekNoHeat = g_Config::bDeinonychusBigJump = g_Config::bFloatingStructures = true;
    g_Config::bRadar = true; PhoenixShortcuts::PendingLevelStat.store(0);
    g_Config::bJackalopeBigJump = true;
    g_Config::bJackalopeChargeCap = g_Config::bJackalopeFastCharge = true;
    g_Config::bNativeMountJump=g_Config::bNativeMountHover=true;
    g_Config::bDeinoEndHoverTest=g_Config::bDeinoAutoVectorTest=g_Config::bDeinoEndJumpTest=true;
    g_Config::bDeinoFlight=true; g_Config::bDeinoHoverFollowView=true;
    g_Config::bGargantarClient=g_Config::bGargantarReach=g_Config::bGargantarFlightSpeed=true;
    g_Config::bGargantarFastHold=g_Config::bGargantarAttackInterval=g_Config::bGargantarTransitVisual=true;
    PhoenixShortcuts::StopAll();
    assert(!g_Config::bGargantarClient && !g_Config::bGargantarReach && !g_Config::bGargantarFlightSpeed);
    assert(!g_Config::bGargantarFastHold && !g_Config::bGargantarAttackInterval && !g_Config::bGargantarTransitVisual);
    assert(!g_Config::bDeinoFlight);
    assert(!g_Config::bDeinoHoverFollowView);
    assert(!g_Config::bDeinoEndHoverTest && !g_Config::bDeinoAutoVectorTest && !g_Config::bDeinoEndJumpTest);
    assert(!g_Config::bNativeMountJump && !g_Config::bNativeMountHover && !g_Config::bDeinoAirLatch);
    assert(!g_Config::bJackalopeChargeCap && !g_Config::bJackalopeFastCharge);
    assert(!g_Config::bJackalopeBigJump);
    assert(!g_Config::bDeinonychusAirSpeed);
    assert(!g_Config::bNoRecoil && !g_Config::bFastBow && !g_Config::bAutoClaimBabies);
    assert(!g_Config::bAutoExtractEmbryos);
    assert(!g_Config::bTekNoHeat && !g_Config::bDeinonychusBigJump && !g_Config::bFloatingStructures);
    assert(g_Config::bRadar && PhoenixShortcuts::PendingLevelStat.load() == -1);
    std::filesystem::remove(Dir/"expansion.ini");
    assert(!g_Config::bDeinoGlideProbe);
    assert(!g_Config::bDeinoPointPounceTest);
    assert(!g_Config::bDeinoAutoJumpTest && !g_Config::bDeinoLatchTrace);
    std::cout << "Feature expansion: interaction classification, supply filters, radar projection, persistence, limits and stop behavior passed.\n";
}
