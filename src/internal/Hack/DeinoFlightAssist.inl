// Runs on the game thread. The lightweight panel never opens the game cursor.
struct FlightAssist {
    bool Panel=false,Rearm=false,Capture=false,HasLocation=false;
    int Field=0,Preset=-1;
    uint64_t NextPreview=0,EndAt=0,StableAt=0,Started=0;
    SDK::FVector Origin{},Previous{},Position{};
    float H=0,Up=0,Protection=0;
    PhoenixDeinoFlight::Calibration Calibration;
    PhoenixDeinoUI::View View;
};
float* FlightParameter(int I) {
    if(I==0 && g_Config::bDeinoAimJump) return &g_Config::DeinoAimJumpSpeed;
    float* P[]={&g_Config::DeinoAirHorizontalMultiplier,&g_Config::DeinoAirUpSpeed,
        &g_Config::DeinoAirProtectionSeconds,&g_Config::DeinoTakeoffProtectionSeconds,
        &g_Config::DeinoHoverIntervalMs,&g_Config::DeinonychusJumpMultiplier};
    return P[std::clamp(I,0,5)];
}
void AdjustFlightParameter(int I,int Wheel) {
    const float Lo[]={1,-300,.1f,.1f,10,1},Hi[]={g_Config::bDeinoAimJump?300.0f:30.0f,300,5,5,500,5};
    const float Step[]={g_Config::bDeinoAimJump?g_Config::DeinoAimSpeedStep:g_Config::DeinoHorizontalStep,g_Config::DeinoUpStep,.05f,.05f,10,.1f};
    if(I<0 || I>5 || !Wheel) return;
    *FlightParameter(I)=std::clamp(*FlightParameter(I)+Step[I]*Wheel/120.f,Lo[I],Hi[I]);
}
PhoenixAirLatch::ReleaseVector FlightJumpVector(double Yaw,double Pitch) {
    return g_Config::bDeinoAimJump ? PhoenixDeinoFlight::AimVector(Yaw,Pitch,g_Config::DeinoAimJumpSpeed) :
        PhoenixDeinoFlight::AirVector(Yaw,g_Config::DeinoAirHorizontalMultiplier,g_Config::DeinoAirUpSpeed);
}
void FlightAssistTick(FlightAssist& A,SDK::AShooterPlayerController* PC,SDK::APrimalDinoCharacter* D,
    SDK::UCharacterMovementComponent* M,bool Air,bool Down,bool Moving,uint64_t Now) {
    const auto C=PhoenixDeinoUI::Take();
    if(C.Toggle) A.Panel=!A.Panel;
    if(A.Panel) A.Rearm=true;
    else if(!Down) A.Rearm=false;
    A.Field=(A.Field+C.Field)%6;
    AdjustFlightParameter(0,C.WheelH); AdjustFlightParameter(1,C.WheelUp); AdjustFlightParameter(A.Field,C.WheelSelected);
    if(C.Preset) {
        A.Preset=(A.Preset+C.Preset)%3;
        const float H[]={g_Config::DeinoPreset1H,g_Config::DeinoPreset2H,g_Config::DeinoPreset3H};
        const float U[]={g_Config::DeinoPreset1Up,g_Config::DeinoPreset2Up,g_Config::DeinoPreset3Up};
        const float T[]={g_Config::DeinoPreset1Protection,g_Config::DeinoPreset2Protection,g_Config::DeinoPreset3Protection};
        g_Config::DeinoAirHorizontalMultiplier=H[A.Preset]; g_Config::DeinoAirUpSpeed=U[A.Preset]; g_Config::DeinoAirProtectionSeconds=T[A.Preset];
    }
    auto& V=A.View; V.Time=Now; V.Available=true; V.Panel=A.Panel; V.Air=Air; V.Selected=A.Field;
    const auto Aim=PC->GetControlRotation();
    V.AimJump=g_Config::bDeinoAimJump;
    V.Pitch=std::clamp(std::remainder(Aim.Pitch,360.0),-90.0,90.0);
    const auto JumpVector=FlightJumpVector(Aim.Yaw,Aim.Pitch);
    V.EffectiveUp=JumpVector.Valid?JumpVector.Z/100:std::numeric_limits<double>::quiet_NaN();
    V.EffectiveHorizontal=JumpVector.Valid?std::hypot(JumpVector.X,JumpVector.Y)/100:std::numeric_limits<double>::quiet_NaN();
    for(int I=0;I<6;++I) V.Values[I]=*FlightParameter(I);
    const float Keys[]={g_Config::DeinoPanelFKey,g_Config::DeinoPresetFKey,g_Config::DeinoHorizontalFKey,g_Config::DeinoUpFKey};
    for(int I=0;I<4;++I) V.Keys[I]=VK_F1+unsigned(std::clamp(int(std::lround(Keys[I])),1,12))-1;
    const auto OriginalKeys=V.Keys;
    for(int I=0;I<4;++I) {
        if(PhoenixShortcuts::Conflict(V.Keys[I],nullptr)) V.Keys[I]=0;
        for(int J=0;J<4;++J) if(I!=J && OriginalKeys[I]==OriginalKeys[J]) V.Keys[I]=0;
    }
    const double CurrentSpeed=std::hypot(std::hypot(M->Velocity.X,M->Velocity.Y),M->Velocity.Z);
    V.Suppressed=Moving || Down || !std::isfinite(CurrentSpeed) || CurrentSpeed>200;
    auto* Loc=NativeMountFunction(D,"Actor","K2_GetActorLocation",24);
    SDK::FVector Position{};
    const bool HasLocation=NativeMountParam(Loc,"ReturnValue","StructProperty",0,24);
    if(HasLocation) D->ProcessEvent(Loc,&Position);
    A.HasLocation=HasLocation; A.Position=Position;
    if(A.Capture) {
        if(!Air || !HasLocation || Moving || (Down && Now-A.Started>150) ||
            std::hypot(std::hypot(Position.X-A.Previous.X,Position.Y-A.Previous.Y),Position.Z-A.Previous.Z)>10000) A.Capture=false;
        A.Previous=Position;
        if(A.Capture && A.EndAt) {
            const double Speed=std::hypot(std::hypot(M->Velocity.X,M->Velocity.Y),M->Velocity.Z);
            if(Speed>200) A.StableAt=0; else if(!A.StableAt) A.StableAt=Now;
            if(A.StableAt && Now-A.StableAt>=300) {
                A.Calibration.Add(A.H,A.Up,A.Protection,std::hypot(Position.X-A.Origin.X,Position.Y-A.Origin.Y)/100,(Position.Z-A.Origin.Z)/100);
                A.Capture=false;
            } else if(Now-A.EndAt>2000) A.Capture=false;
        }
    }
    if(Now>=A.NextPreview) {
        A.NextPreview=Now+50; V.Preview=false; V.Path={}; V.Low={}; V.High={}; V.Samples=0; V.ServerBaseline=false; V.NativeArc=0;
        if(g_Config::bDeinoFlightPreview && Air && HasLocation) {
            auto P=PhoenixDeinoFlight::PreviewVector(JumpVector,V.Values[2],g_Config::DeinoHoverIntervalMs,!V.Suppressed);
            // Client stops can differ from authority by metres; observations do not overwrite the server baseline.
            V.ServerBaseline=P.ServerBaseline; V.HeightMargin=P.HeightMargin;
            if(A.Calibration.Matches(V.EffectiveHorizontal/30,V.EffectiveUp,V.Values[2])) V.Samples=A.Calibration.Count;
            V.LastDistance=A.Calibration.LastDistance; V.LastHeight=A.Calibration.LastHeight;
            V.Distance=P.Distance; V.Height=P.Height; V.Peak=P.Peak; V.Uncertainty=P.HorizontalMargin;
            auto* Project=NativeMountFunction(PC,"PlayerController","ProjectWorldLocationToScreen",48);
            const bool CanProject=NativeMountParam(Project,"WorldLocation","StructProperty",0,24) &&
                NativeMountParam(Project,"ScreenLocation","StructProperty",24,16) &&
                NativeMountParam(Project,"bPlayerViewportRelative","BoolProperty",40,1) && NativeMountParam(Project,"ReturnValue","BoolProperty",41,1);
            const double Yaw=Aim.Yaw*.017453292519943295;
            auto Screen=[&](double H,double Z) {
                PhoenixDeinoUI::View::Point S;
                if(!CanProject) return S;
                struct {SDK::FVector World; SDK::FVector2D Screen{}; bool Relative=false,Result=false;} Args;
                Args.World={Position.X+100*H*std::cos(Yaw),Position.Y+100*H*std::sin(Yaw),Position.Z+100*Z};
                PC->ProcessEvent(Project,&Args);
                S={float(Args.Screen.X),float(Args.Screen.Y),Args.Result && std::isfinite(Args.Screen.X) && std::isfinite(Args.Screen.Y)}; return S;
            };
            if(P.Valid) {
                V.Preview=true;
                std::array<SDK::FVector,33> WorldPath{};
                for(size_t I=0;I<P.Path.size();++I) {
                    V.Path[I]=Screen(P.Path[I].H,P.Path[I].Z);
                    WorldPath[I]={Position.X+100*P.Path[I].H*std::cos(Yaw),Position.Y+100*P.Path[I].H*std::sin(Yaw),Position.Z+100*P.Path[I].Z};
                }
                V.NativeArc=UpdateDeinoNativeArc(D,WorldPath,Now);
                V.Low=Screen(std::max(0.0,P.Distance-V.Uncertainty),P.Height);
                V.High=Screen(P.Distance+V.Uncertainty,P.Height);
            }
        }
    }
    if(!V.Preview || !g_Config::bDeinoNativeArc) { ClearDeinoNativeArc(); V.NativeArc=0; }
    PhoenixDeinoUI::Publish(V);
}
