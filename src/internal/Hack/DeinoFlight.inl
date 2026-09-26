// One Space controller: native ground jump, horizontal air jump, end-chain hover.
struct DeinoFlightRuntime {
    ExtensionObject Dino;
    PhoenixDeinoAction::TakeoffControl Takeoff;
    PhoenixDeinoFlight::Control Control;
    bool HoldCleared=false;
    uint64_t NextSample=0;
} DeinoFlight;
std::string DeinoFlightStatus="恐爪龙大跳待命：地面空格起跳，空中空格向前跳，松键悬停";
void ResetDeinoFlight() { ClearDeinoFlightInput(); DeinoFlight={}; }
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
                    SDK::UCharacterMovementComponent* M,bool Ready) {
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
    const bool Down=(GetAsyncKeyState(VK_SPACE)&0x8000)!=0;
    const auto GroundMs=PhoenixDeinoAction::TakeoffProtectionMs(g_Config::DeinoTakeoffProtectionSeconds);
    const bool GroundPress=R.Takeoff.Press(Down,Ground,Now);
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
        if (Ground && MaskRelease) SetDeinoFlightInput(D,M,true); else ClearDeinoFlightInput();
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
            <<",horizontal_multiplier,"<<g_Config::DeinoAirHorizontalMultiplier<<",up_mps,"<<g_Config::DeinoAirUpSpeed
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
    bool Moving=false;
    for (int Key : {int('W'),int('A'),int('S'),int('D'),VK_CONTROL}) Moving|=(GetAsyncKeyState(Key)&0x8000)!=0;
    const bool RequestBusy=GlideProbeScalar(D,"LatchedJumpRequested")!=0;
    const auto Action=R.Control.Poll(Down,Moving,true,Protected || RequestBusy,Now,g_Config::DeinoHoverIntervalMs);
    if (Action==PhoenixDeinoFlight::Control::Action::None) return;
    if (Action==PhoenixDeinoFlight::Control::Action::Jump) {
        auto* F=AirLatchFunction(D,"ServerRequestLatchedAutoJump",24,true);
        const auto V=PhoenixDeinoFlight::AirVector(PC->GetControlRotation().Yaw,
            g_Config::DeinoAirHorizontalMultiplier,g_Config::DeinoAirUpSpeed);
        if (!FlightVectorParam(F,"Velocity") || !V.Valid) {
            ExtensionStatus(DeinoFlightStatus,"空中跳跃参数或入口校验失败，未提交"); return;
        }
        SDK::FVector Args{V.X,V.Y,V.Z};
        if (!Log("send_air_jump",Args,true)) return;
        DeinoActionMark("own_flight_air_jump"); D->ProcessEvent(F,&Args);
        R.Control.Jumped(Now,PhoenixDeinoAction::TakeoffProtectionMs(g_Config::DeinoAirProtectionSeconds));
        ExtensionStatus(DeinoFlightStatus,"已提交水平空中跳跃；保护结束后松键悬停");
    } else {
        auto* F=AirLatchFunction(D,"ServerEndUberpounce",32,true);
        if (!FlightVectorParam(F,"Velocity") || !NativeMountParam(F,"MontageToPlayOnNonZeroVelocity","ObjectProperty",24,8)) {
            ExtensionStatus(DeinoFlightStatus,"悬停结束链接口校验失败，未提交"); return;
        }
        // After the configured protection, ending may deliberately interrupt the
        // remaining propulsion. Do not treat its residual velocity as a stale lock.
        if (!Log("send_end_hover",SDK::FVector{},false)) return;
        alignas(16) uint8_t Args[32]{}; D->ProcessEvent(F,Args);
        ExtensionStatus(DeinoFlightStatus,"正在结束链悬停；空格向前跳，方向键或Ctrl暂停悬停");
    }
}
