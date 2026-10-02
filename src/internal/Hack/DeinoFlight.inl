// One Space controller: native ground jump, horizontal air jump, end-chain hover.
#include "DeinoNativeArc.inl"
#include "DeinoFlightAssist.inl"
#include "DeinoHoverFacing.inl"
struct DeinoFlightRuntime {
    FlightAssist Assist;
    uint64_t GroundProtection=1000;
    ExtensionObject Dino;
    PhoenixDeinoAction::TakeoffControl Takeoff;
    PhoenixDeinoFlight::Control Control;
    PhoenixDeinoFlight::InputGate Input;
    bool HoldCleared=false;
    uint64_t NextSample=0,NextFacingSample=0;
} DeinoFlight;
std::string DeinoFlightStatus="恐爪龙大跳待命：地面空格起跳，空中空格向前跳，松键悬停";
std::string DeinoHoverFacingStatus="朝向跟随待命：进入悬停后跟随玩家水平视角";
void ResetDeinoFlight() { ClearDeinoNativeArc(); ClearDeinoFlightInput(); PhoenixDeinoUI::Clear(); DeinoFlight={};
    ExtensionStatus(DeinoHoverFacingStatus,"朝向跟随待命：进入悬停后跟随玩家水平视角"); }
bool FlightVectorParam(SDK::UFunction* F,const char* Name) {
    if (!NativeMountParam(F,Name,"StructProperty",0,24)) return false;
    auto* S=static_cast<SDK::FStructProperty*>(FindReflectedProperty(F,Name))->Struct;
    return IsUsableObject(S) && S->GetName()=="Vector";
}
double FlightStamina(SDK::APrimalDinoCharacter* D) {
    auto* S=D->MyCharacterStatusComponent;
    auto* F=NativeMountFunction(S,"PrimalCharacterStatusComponent","BPGetCurrentStatusValue",8);
    if (!NativeMountParam(F,"ReturnValue","FloatProperty",4,4) ||
        !(NativeMountParam(F,"ValueType","ByteProperty",0,1) || NativeMountParam(F,"ValueType","EnumProperty",0,1)))
        return std::numeric_limits<double>::quiet_NaN();
    struct { uint8_t Type=1,Pad[3]{}; float Value=0; } Args;
    S->ProcessEvent(F,&Args); return Args.Value;
}
void TickDeinoFlight(SDK::AShooterPlayerController* PC,SDK::APrimalDinoCharacter* D,
                    SDK::UCharacterMovementComponent* M,bool Ready,bool InputReady) {
    if (!g_Config::bDeinoFlight || !Ready || !M || IsFreeCameraActive() || !IsUsableObject(D) || D->IsDead() ||
        !IsUsableObject(PC) || PC->Pawn!=D || !ExpansionHasClass(D,"deinonychus_character_bp") ||
        !(D->Role==SDK::ENetRole::ROLE_AutonomousProxy || D->Role==SDK::ENetRole::ROLE_Authority)) {
        ResetDeinoFlight(); return;
    }
    if (DeinoProbeMode()!=4 || PhoenixShortcuts::Conflict(VK_SPACE,nullptr)) {
        ResetDeinoFlight(); ExtensionStatus(DeinoFlightStatus,"大跳暂停：关闭其他移动功能并解除空格快捷键冲突"); return;
    }
    auto& R=DeinoFlight;
    if (R.Dino.Get()!=D) { ResetDeinoFlight(); R.Dino=ExtensionObject(D); }
    const auto Now=GetTickCount64();
    const int Move=int(M->MovementMode),State=AirLatchState(D);
    // EndUberpounce can temporarily set Walking while airborne. Never use that
    // alone to trigger a ground jump or reset the takeoff protection window.
    bool Close=false;
    if ((Move!=1 && Move!=2 && Move!=3) || State!=0 || !AirLatchFloor(D,M,Close)) {
        ResetDeinoFlight(); ExtensionStatus(DeinoFlightStatus,"大跳暂停：抓附、特殊运动或地面查询不可用"); return;
    }
    const bool Ground=(Move==1 || Move==2) && Close,Air=!Close;
    if(Ground && InputReady && g_Config::bDeinoFlightPreview && g_Config::bDeinoNativeArc)
        PrepareDeinoNativeArc(Now);
    const bool RawSpace=InputReady && (GetAsyncKeyState(VK_SPACE)&0x8000)!=0;
    const bool AcceptInput=R.Input.Poll(InputReady,RawSpace);
    const bool Down=AcceptInput && RawSpace;
    bool Moving=false;
    if(AcceptInput) for (int Key : {int('W'),int('A'),int('S'),int('D'),VK_CONTROL}) Moving|=(GetAsyncKeyState(Key)&0x8000)!=0;
    if(InputReady) FlightAssistTick(R.Assist,PC,D,M,Air,Down,Moving,Now);
    else {
        R.Assist.Capture=false; R.Assist.Rearm=true;
        PhoenixDeinoUI::Clear(); ClearDeinoNativeArc();
    }
    const bool InputEdge=R.Takeoff.Press(Down,Ground,Now);
    ExtensionStatus(DeinoHoverFacingStatus,"朝向跟随待命：进入悬停后跟随玩家水平视角");
    const bool GroundPress=InputEdge && !R.Assist.Panel && !R.Assist.Rearm;
    if(GroundPress) R.GroundProtection=PhoenixDeinoAction::TakeoffProtectionMs(g_Config::DeinoTakeoffProtectionSeconds);
    const auto GroundMs=R.GroundProtection;
    const bool Protected=R.Takeoff.Protect(Ground,Air,Now,GroundMs);
    const bool MaskRelease=R.Takeoff.Mask(Down,Now);
    if (!Air) {
        R.Control.Poll(Down,false,false,false,Now,g_Config::DeinoHoverIntervalMs);
        R.HoldCleared=false;
        if (GroundPress) {
            auto* Jump=AirLatchFunction(D,"BP_OnJumpReleased",105);
            if (!NativeMountParam(Jump,"HoldTime","DoubleProperty",0,8) || !SetDeinoFlightInput(D,M,true)) {
                ClearDeinoFlightInput(); ExtensionStatus(DeinoFlightStatus,"地面起跳接口校验失败，未接管原生空格"); return;
            }
            ClearDeinoFlightInput();
            alignas(16) uint8_t Args[112]{}; D->ProcessEvent(Jump,Args);
            R.Takeoff.Started(Now,GroundMs); SetDeinoFlightInput(D,M,true);
            DeinoActionMark("own_ground_press_jump");
            ExtensionStatus(DeinoFlightStatus,"已按下空格起跳；离地保护期间暂不悬停"); return;
        }
        if (Ground && (MaskRelease || R.Assist.Panel || R.Assist.Rearm)) SetDeinoFlightInput(D,M,true); else ClearDeinoFlightInput();
        return;
    }
    auto& Probe=DeinoGlideProbe;
    if (!Probe.Window.Started || Probe.Done || !Probe.File.good() || Probe.Mode!=4 || Probe.Dino.Get()!=D ||
        !SetDeinoFlightInput(D,M,true)) {
        ClearDeinoFlightInput(); R.HoldCleared=false;
        ExtensionStatus(DeinoFlightStatus,"大跳暂停：记录或输入接口未就绪"); return;
    }
    if (!R.HoldCleared) {
        auto* Held=AirLatchFunction(D,"ServerSetJumpHeld",1,true);
        if (!NativeMountParam(Held,"JumpHeld","BoolProperty",0,1)) { ResetDeinoFlight(); return; }
        bool Value=false; D->ProcessEvent(Held,&Value);
        SDK::UKismetSystemLibrary::K2_ClearTimer(D,SDK::FString(L"JumpPressedAndHeld"));
        R.HoldCleared=true;
    }
    auto Log=[&](const char* Event,const SDK::FVector& V,bool Flush) {
        Probe.File<<"# flight,"<<(Now-Probe.Window.Start)<<",event,"<<Event
            <<",movement,"<<Move<<",state,"<<State<<",velocity,"<<V.X<<','<<V.Y<<','<<V.Z
            <<",horizontal_multiplier,"<<g_Config::DeinoAirHorizontalMultiplier<<",manual_up_mps,"<<g_Config::DeinoAirUpSpeed
            <<",aim_jump,"<<g_Config::bDeinoAimJump<<",aim_speed_mps,"<<g_Config::DeinoAimJumpSpeed
            <<",interval_ms,"<<g_Config::DeinoHoverIntervalMs<<'\n';
        if (Flush) Probe.File.flush();
        if (!Probe.File.good()) { FinishDeinoGlideProbe("flight_log_failed"); ResetDeinoFlight(); return false; }
        return true;
    };
    if (Now>=R.NextSample) {
        R.NextSample=Now+100;
        Probe.File<<"# flight_stamina,"<<(Now-Probe.Window.Start)<<",value,"<<FlightStamina(D)<<'\n';
        if (!Log("sample",M->Velocity,false)) return;
    }
    const bool RequestBusy=GlideProbeScalar(D,"LatchedJumpRequested")!=0;
    const auto Action=R.Control.Poll(R.Assist.Panel?false:Down,R.Assist.Panel?false:Moving,true,Protected || RequestBusy,Now,g_Config::DeinoHoverIntervalMs);
    auto FollowView=[&] {
        if(!g_Config::bDeinoHoverFollowView || !R.Control.Hovering || !AcceptInput ||
            R.Assist.Panel || R.Assist.Rearm) return;
        const auto Facing=ApplyDeinoHoverFacing(D,M,PC->GetControlRotation().Yaw);
        ExtensionStatus(DeinoHoverFacingStatus,Facing.Status==1 ? "悬停朝向已跟随；服务器命中效果待实测" :
            Facing.Status==2 ? "朝向跟随接口校验失败" : Facing.Status==3 ? "朝向跟随参数无效" : "朝向跟随未生效或被修正");
        if(Now>=R.NextFacingSample) {
            R.NextFacingSample=Now+250;
            Probe.File<<"# hover_facing,"<<(Now-Probe.Window.Start)<<",status,"<<Facing.Status
                <<",before_yaw,"<<Facing.Before<<",requested_yaw,"<<Facing.Requested<<",after_yaw,"<<Facing.After<<'\n';
        }
    };
    if (Action==PhoenixDeinoFlight::Control::Action::None) { FollowView(); return; }
    if (Action==PhoenixDeinoFlight::Control::Action::Jump) {
        if(R.Assist.Panel || R.Assist.Rearm) return;
        auto* F=AirLatchFunction(D,"ServerRequestLatchedAutoJump",24,true);
        const auto Aim=PC->GetControlRotation();
        const auto V=FlightJumpVector(Aim.Yaw,Aim.Pitch);
        if (!FlightVectorParam(F,"Velocity") || !V.Valid) {
            ExtensionStatus(DeinoFlightStatus,"空中跳跃参数或入口校验失败，未提交"); return;
        }
        SDK::FVector Args{V.X,V.Y,V.Z};
        Probe.File<<"# flight_aim,yaw,"<<Aim.Yaw<<",pitch,"<<Aim.Pitch<<",effective_up_mps,"<<V.Z/100
            <<",effective_horizontal_mps,"<<std::hypot(V.X,V.Y)/100<<'\n';
        if (!Log("send_air_jump",Args,true)) return;
        auto& A=R.Assist;
        A.Capture=A.HasLocation && !Moving && std::hypot(std::hypot(M->Velocity.X,M->Velocity.Y),M->Velocity.Z)<200; A.Started=Now; A.EndAt=A.StableAt=0;
        A.Origin=A.Previous=A.Position; A.H=float(std::hypot(V.X,V.Y)/3000);
        A.Up=float(V.Z/100); A.Protection=g_Config::DeinoAirProtectionSeconds;
        DeinoActionMark("own_flight_air_jump"); D->ProcessEvent(F,&Args);
        R.Control.Jumped(Now,PhoenixDeinoAction::TakeoffProtectionMs(g_Config::DeinoAirProtectionSeconds));
        ExtensionStatus(DeinoFlightStatus,"已提交空中跳跃；保护结束后松键悬停");
    } else {
        R.Control.Hovering=false;
        auto* F=AirLatchFunction(D,"ServerEndUberpounce",32,true);
        if (!FlightVectorParam(F,"Velocity") || !NativeMountParam(F,"MontageToPlayOnNonZeroVelocity","ObjectProperty",24,8)) {
            ExtensionStatus(DeinoFlightStatus,"悬停结束链接口校验失败，未提交"); return;
        }
        // After the configured protection, ending may deliberately interrupt the
        // remaining propulsion. Do not treat its residual velocity as a stale lock.
        if (!Log("send_end_hover",SDK::FVector{},false)) return;
        alignas(16) uint8_t Args[32]{}; D->ProcessEvent(F,Args);
        R.Control.Hovering=true;
        FollowView();
        if(R.Assist.Capture && !R.Assist.EndAt) R.Assist.EndAt=Now;
        ExtensionStatus(DeinoFlightStatus,InputReady ? "正在结束链悬停；空格向前跳，方向键或Ctrl暂停悬停" :
            "菜单或后台：继续悬停，跳跃输入已暂停");
    }
}
