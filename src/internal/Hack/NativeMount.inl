// Included inside FeatureExpansion's anonymous namespace. Game thread only.
struct NativeMountRuntime {
    ExtensionObject Mount, HoverMove;
    SDK::EMovementMode PreviousMode=SDK::EMovementMode::MOVE_Falling;
    uint8_t PreviousCustom=0;
    PhoenixNativeMount::Press Press;
    ExtensionObject QueuedMove, QueuedController;
    double QueuedYaw=0, QueuedGroundSpeed=0, QueuedAirStep=0, QueuedCap=0;
    uint64_t QueuedAt=0;
    SDK::EMovementMode QueuedMode=SDK::EMovementMode::MOVE_None;
} NativeMount;
std::atomic<DWORD> NativeMountThread{0};
std::string NativeMountStatus="原生坐骑运动未启用";
// Retain the last request result when opening the menu pauses the feature.
std::string NativeMountLastResult;
void NativeMountResult(const char* Text) {
    std::scoped_lock L(ExtensionStatusMutex);
    NativeMountStatus=Text;
    NativeMountLastResult=Text;
}

SDK::UFunction* NativeMountFunction(SDK::UObject* O,const char* Class,const char* Name,int Size) {
    if (!IsUsableObject(O)) return nullptr;
    auto* F=O->Class->GetFunction(Class,Name);
    // Only local native functions, never an RPC or an unverified bytecode body.
    if (!IsUsableObject(F) || F->Size!=Size ||
        !(static_cast<uint32_t>(F->FunctionFlags)&0x400) ||
        (static_cast<uint32_t>(F->FunctionFlags)&0x40)) return nullptr;
    return F;
}
bool NativeMountParam(SDK::UFunction* F,const char* Name,const char* Type,int Offset,int Size) {
    auto* P=F ? FindReflectedProperty(F,Name) : nullptr;
    if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString()!=Type ||
        P->ArrayDim!=1 || P->Offset!=Offset || P->ElementSize!=Size) return false;
    if (std::string_view(Type)=="BoolProperty") {
        auto* B=static_cast<SDK::FBoolProperty*>(P);
        return B->ByteOffset==0 && B->FieldMask==0xff;
    }
    return true;
}
bool NativeMountMode(SDK::UCharacterMovementComponent* M,SDK::EMovementMode Mode,uint8_t Custom) {
    auto* F=NativeMountFunction(M,"CharacterMovementComponent","SetMovementMode",2);
    if (!NativeMountParam(F,"NewMovementMode","ByteProperty",0,1) ||
        !NativeMountParam(F,"newCustomMode","ByteProperty",1,1)) return false;
    struct { SDK::EMovementMode Mode; uint8_t Custom; } Args{Mode,Custom};
    M->ProcessEvent(F,&Args); return M->MovementMode==Mode && M->CustomMovementMode==Custom;
}
void RestoreNativeMountHover() {
    auto* M=static_cast<SDK::UCharacterMovementComponent*>(NativeMount.HoverMove.Get());
    // Do not overwrite a newer swimming/landing/correction state.
    if (M && M->MovementMode==SDK::EMovementMode::MOVE_Flying && M->CustomMovementMode==0)
        NativeMountMode(M,NativeMount.PreviousMode,NativeMount.PreviousCustom);
    NativeMount.HoverMove={};
}
void ResetNativeMount() {
    if (NativeMount.QueuedAt) NativeMountResult("运动Hook请求因暂停或骑乘状态变化取消");
    RestoreNativeMountHover(); NativeMount={};
}
bool NativeMountImpulse(SDK::APrimalDinoCharacter* D,SDK::UCharacterMovementComponent* M,
                        const PhoenixNativeMount::Plan& Plan) {
    if (g_Config::bNativeMountUseImpulse) {
        auto* F=NativeMountFunction(M,"CharacterMovementComponent","AddImpulse",40);
        if (!NativeMountParam(F,"Impulse","StructProperty",0,24) ||
            !NativeMountParam(F,"bVelocityChange","BoolProperty",24,1) ||
            !NativeMountParam(F,"MassScaleImpulseExponent","FloatProperty",28,4) ||
            !NativeMountParam(F,"bOverrideMaxImpulseZ","BoolProperty",32,1)) return false;
        struct { SDK::FVector V; bool VelocityChange; uint8_t Pad[3]; float Exponent;
                 bool OverrideZ; uint8_t Tail[7]; } Args{};
        static_assert(sizeof(Args)==40);
        Args.V={Plan.Delta.X,Plan.Delta.Y,Plan.Delta.Z}; Args.VelocityChange=true; Args.Exponent=1;
        M->ProcessEvent(F,&Args);
    } else {
        auto* F=NativeMountFunction(D,"Character","LaunchCharacter",32);
        if (!NativeMountParam(F,"LaunchVelocity","StructProperty",0,24) ||
            !NativeMountParam(F,"bXYOverride","BoolProperty",24,1) ||
            !NativeMountParam(F,"bZOverride","BoolProperty",25,1)) return false;
        struct { SDK::FVector V; bool XY,Z; uint8_t Pad[6]; } Args{};
        static_assert(sizeof(Args)==32);
        Args.V={Plan.Target.X,Plan.Target.Y,Plan.Target.Z}; Args.XY=Args.Z=true;
        D->ProcessEvent(F,&Args);
    }
    return true;
}
void TickNativeMount(SDK::AShooterPlayerController* PC,SDK::APrimalDinoCharacter* D,
                     SDK::UCharacterMovementComponent* M,bool Ready) {
    NativeMountThread.store(GetCurrentThreadId());
    if (NativeMount.QueuedAt && (!g_Config::bNativeMountPhysicsHook ||
        GetTickCount64()-NativeMount.QueuedAt>200)) {
        NativeMount.QueuedAt=0; NativeMount.QueuedMove={};
        NativeMountResult(g_Config::bNativeMountPhysicsHook ?
            "200毫秒内未收到当前坐骑的同线程运动回调" : "运动Hook选项已关闭，请求取消");
    }
    const bool Enabled=(g_Config::bNativeMountJump || g_Config::bNativeMountHover) && !g_Config::bDeinoAirLatch && !g_Config::bDeinoAutoJumpTest && !g_Config::bDeinoLatchTrace && !g_Config::bDeinoPointPounceTest;
    const bool Deino=ExpansionHasClass(D,"deinonychus_character_bp");
    if (!Enabled || !Ready || !M || IsFreeCameraActive() ||
        !(Deino || ExpansionHasClass(D,"jackalope_character_bp")) ||
        !(D->Role==SDK::ENetRole::ROLE_AutonomousProxy || D->Role==SDK::ENetRole::ROLE_Authority)) {
        ResetNativeMount(); ExtensionStatus(NativeMountStatus,Enabled ? "原生运动等待骑乘恐爪龙或穴角兔并关闭菜单" : "原生坐骑运动未启用");
        return;
    }
    if (Deino) {
        auto* P=FindReflectedProperty(D->Class,"UberpounceState");
        if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString()!="ByteProperty" ||
            P->ElementSize!=1 || P->ArrayDim!=1 || P->Offset<0 || P->Offset>=D->Class->Size ||
            *(reinterpret_cast<uint8_t*>(D)+P->Offset)!=0) {
            ResetNativeMount(); ExtensionStatus(NativeMountStatus,"抓附或扑击状态中，原生运动暂停"); return;
        }
    }
    if (NativeMount.Mount.Get()!=D) { ResetNativeMount(); NativeMount.Mount=ExtensionObject(D); }
    if (NativeMount.HoverMove.Get() && NativeMount.HoverMove.Get()!=M) RestoreNativeMountHover();
    const bool OwnedFlying=NativeMount.HoverMove.Get()==M && M->MovementMode==SDK::EMovementMode::MOVE_Flying;
    const bool Ground=M->MovementMode==SDK::EMovementMode::MOVE_Walking;
    const bool Air=M->MovementMode==SDK::EMovementMode::MOVE_Falling || OwnedFlying;
    if (!Ground && !Air) { ResetNativeMount(); ExtensionStatus(NativeMountStatus,"当前运动模式不支持原生跳跃与悬停"); return; }
    if (PhoenixShortcuts::Conflict(VK_F6,nullptr)) {
        ResetNativeMount(); ExtensionStatus(NativeMountStatus,"F6已绑定菜单或快捷功能，请先解除冲突"); return;
    }
    const bool Down=(GetAsyncKeyState(VK_F6)&0x8000)!=0;
    const bool Boost=NativeMount.Press.Take(Down,GetTickCount64()) && g_Config::bNativeMountJump;
    if (Boost) {
        RestoreNativeMountHover();
        const auto V=M->Velocity;
        auto P=PhoenixNativeMount::Boost({V.X,V.Y,V.Z},PC->GetControlRotation().Yaw,Ground,
            100*ExtensionLimit(g_Config::NativeMountGroundSpeed,1,100,20),
            100*ExtensionLimit(g_Config::NativeMountAirStep,1,100,10),
            100*ExtensionLimit(g_Config::NativeMountHorizontalCap,1,200,60));
        if (P.Valid && g_Config::bNativeMountPhysicsHook) {
            if (!g_Hook::PhysicsRotationOK) {
                NativeMountResult("运动Hook未安装，未追加速度"); return;
            }
            NativeMount.QueuedMove=ExtensionObject(M); NativeMount.QueuedController=ExtensionObject(PC);
            NativeMount.QueuedYaw=PC->GetControlRotation().Yaw;
            NativeMount.QueuedGroundSpeed=100*ExtensionLimit(g_Config::NativeMountGroundSpeed,1,100,20);
            NativeMount.QueuedAirStep=100*ExtensionLimit(g_Config::NativeMountAirStep,1,100,10);
            NativeMount.QueuedCap=100*ExtensionLimit(g_Config::NativeMountHorizontalCap,1,200,60);
            NativeMount.QueuedMode=M->MovementMode; NativeMount.QueuedAt=GetTickCount64();
            NativeMountResult("已排队，等待当前坐骑运动Hook"); return;
        }
        const bool Applied=P.Valid && NativeMountImpulse(D,M,P);
        NativeMountResult(Applied ? "已调用原生运动入口；联机效果请在游戏内确认" : "原生运动参数或函数校验失败，未调用");
        return;
    }
    if (!g_Config::bNativeMountHover || !Air || Down || NativeMount.QueuedAt || (GetAsyncKeyState(VK_SPACE)&0x8000)) {
        RestoreNativeMountHover(); return;
    }
    auto* Last=NativeMountFunction(M,"PawnMovementComponent","GetLastInputVector",24);
    auto* Pending=NativeMountFunction(M,"PawnMovementComponent","GetPendingInputVector",24);
    auto* Stop=NativeMountFunction(M,"MovementComponent","StopMovementImmediately",0);
    if (!NativeMountParam(Last,"ReturnValue","StructProperty",0,24) ||
        !NativeMountParam(Pending,"ReturnValue","StructProperty",0,24) || !Stop) {
        RestoreNativeMountHover(); ExtensionStatus(NativeMountStatus,"悬停入口校验失败，未修改运动模式"); return;
    }
    SDK::FVector A{},B{}; M->ProcessEvent(Last,&A); M->ProcessEvent(Pending,&B);
    const double Input=A.Magnitude()+B.Magnitude();
    if (!std::isfinite(Input) || Input>0.001) { RestoreNativeMountHover(); return; }
    if (!OwnedFlying) {
        NativeMount.PreviousMode=M->MovementMode; NativeMount.PreviousCustom=M->CustomMovementMode;
        if (!NativeMountMode(M,SDK::EMovementMode::MOVE_Flying,0)) {
            ExtensionStatus(NativeMountStatus,"原生飞行模式未接受，悬停未应用"); return;
        }
        NativeMount.HoverMove=ExtensionObject(M);
    }
    M->ProcessEvent(Stop,nullptr);
    ExtensionStatus(NativeMountStatus,"本地悬停已应用；服务器仍可能纠正位置");
}
