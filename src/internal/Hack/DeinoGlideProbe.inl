// Recorder is observational. The separately gated action test below sends one RPC.
struct DeinoGlideProbeRuntime {
    ExtensionObject Dino;
    PhoenixGlideProbe::Window Window;
    std::ofstream File;
    bool Done=false;
    bool Restart=false;
    int Mode=0;
    uint64_t LastFlush=0;
    size_t Rows=0;
} DeinoGlideProbe;
std::string DeinoGlideProbeStatus="滑翔记录未开始";
std::string DeinoGlideProbePath;

double GlideProbeScalar(SDK::UObject* O,const char* Name) {
    const double Missing=std::numeric_limits<double>::quiet_NaN();
    if (!IsUsableObject(O)) return Missing;
    auto* P=FindReflectedProperty(O->Class,Name);
    if (!P || !P->ClassPrivate || P->ArrayDim!=1 || P->Offset<0 || P->ElementSize<=0 ||
        P->ElementSize>O->Class->Size || P->Offset>O->Class->Size-P->ElementSize) return Missing;
    const auto Type=P->ClassPrivate->Name.ToString();
    auto* B=reinterpret_cast<const uint8_t*>(O)+P->Offset;
    if (Type=="BoolProperty") {
        auto* V=static_cast<SDK::FBoolProperty*>(P);
        const auto Mask=V->FieldMask ? V->FieldMask : V->ByteMask;
        if (!Mask || V->ByteOffset>=P->ElementSize) return Missing;
        return (B[V->ByteOffset]&Mask)!=0;
    }
    if (Type=="ByteProperty" && P->ElementSize==1) return *B;
    if (Type=="FloatProperty" && P->ElementSize==4) { float V; std::memcpy(&V,B,4); return V; }
    if (Type=="DoubleProperty" && P->ElementSize==8) { double V; std::memcpy(&V,B,8); return V; }
    return Missing;
}
void FinishDeinoGlideProbe(const char* Reason) {
    auto& R=DeinoGlideProbe;
    StopDeinoActionTrace();
    if (!R.Window.Started) return;
    DrainDeinoActionTrace(R.File,R.Window.Start);
    R.File<<"# end="<<Reason<<",rows="<<R.Rows<<'\n';
    R.File.flush(); const bool Good=R.File.good(); R.File.close();
    R.Window={}; R.Dino={}; R.Done=true;
    R.Restart=std::string_view(Reason)=="paused_or_unavailable" || std::string_view(Reason)=="time_limit";
    ExtensionStatus(DeinoGlideProbeStatus,Good ? "滑翔记录已保存；重新关闭再开启可录下一组" : "滑翔记录写入失败，请检查日志目录");
}
int DeinoProbeMode() {
    return g_Config::bDeinoFlight && !g_Config::bDinoMoveSpeed && !g_Config::bNativeMountJump &&
        !g_Config::bNativeMountHover && !g_Config::bDeinonychusAirSpeed ? 4 : 0;
}
bool GlideProbeVector(SDK::UObject* O,const char* Name,SDK::FVector& V) {
    auto* P=IsUsableObject(O) ? FindReflectedProperty(O->Class,Name) : nullptr;
    if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString()!="StructProperty" ||
        P->ArrayDim!=1 || P->ElementSize!=24 || P->Offset<0 || P->Offset>O->Class->Size-24) return false;
    auto* S=static_cast<SDK::FStructProperty*>(P)->Struct;
    if (!S || S->GetName()!="Vector") return false;
    std::memcpy(&V,reinterpret_cast<uint8_t*>(O)+P->Offset,24);
    return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
}
double GlideProbeTargetType(SDK::UObject* O,const char* Name) {
    const double Missing=std::numeric_limits<double>::quiet_NaN();
    auto* P=IsUsableObject(O) ? FindReflectedProperty(O->Class,Name) : nullptr;
    if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString()!="StructProperty" ||
        P->ArrayDim!=1 || P->ElementSize!=72 || P->Offset<0 || P->Offset>O->Class->Size-72) return Missing;
    auto* S=static_cast<SDK::FStructProperty*>(P)->Struct;
    if (!S || S->GetName()!="UberpounceData") return Missing;
    auto* T=FindReflectedProperty(S,"Type_23_2964764F48C642C4AF1E538ACB43EA6E");
    if (!T || !T->ClassPrivate || T->ClassPrivate->Name.ToString()!="ByteProperty" ||
        T->ArrayDim!=1 || T->Offset!=64 || T->ElementSize!=1) return Missing;
    return *(reinterpret_cast<const uint8_t*>(O)+P->Offset+64);
}
bool StartDeinoGlideProbe(SDK::APrimalDinoCharacter* D,uint64_t Now) {
    auto& R=DeinoGlideProbe;
    try {
        HMODULE Module=nullptr; wchar_t Path[32768]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&DeinoGlideProbe),&Module)) return false;
        const DWORD N=GetModuleFileNameW(Module,Path,32768);
        if (!N || N>=32768) return false;
        auto Dir=std::filesystem::path(Path).parent_path()/L"logs";
        std::error_code Error; std::filesystem::create_directories(Dir,Error);
        if (Error) return false;
        SYSTEMTIME Time{}; GetLocalTime(&Time);
        const auto Name=std::format("deino-glide-{:04}{:02}{:02}-{:02}{:02}{:02}-{}-{}.csv",
            Time.wYear,Time.wMonth,Time.wDay,Time.wHour,Time.wMinute,Time.wSecond,GetCurrentProcessId(),Now);
        const auto Destination=Dir/Name;
        R.File.clear(); R.File.open(Destination,std::ios::out|std::ios::trunc);
        if (!R.File) return false;
        R.File.imbue(std::locale::classic()); R.File<<std::setprecision(12);
        R.File<<"# mode="<<R.Mode<<"; process_event_hook="<<g_Hook::ProjectileShotEventsOK<<"; events_observed_only_not_complete_BP_trace\n";
        const bool HookMatches=g_Hook::DinoActionEventsOK &&
            (*reinterpret_cast<void***>(D))[SDK::Offsets::ProcessEventIdx]==g_Hook::DinoActionEventTarget;
        R.File<<"# dino_event_hook="<<HookMatches<<"; schema=3; client_only; positions=cm; velocities=cm/s; missing=nan; max_duration_ms=120000\n";
        R.File<<"# movement_config,big_jump,"<<g_Config::bDeinoFlight<<",jump_multiplier,"<<g_Config::DeinonychusJumpMultiplier
            <<",integrated_flight,"<<g_Config::bDeinoFlight<<",horizontal_multiplier,"<<g_Config::DeinoAirHorizontalMultiplier
            <<",manual_up_mps,"<<g_Config::DeinoAirUpSpeed<<",hover_ms,"<<g_Config::DeinoHoverIntervalMs
            <<",hover_follow_view,"<<g_Config::bDeinoHoverFollowView<<"; flight_revision=12\n";
        R.File<<"# aim_jump,enabled,"<<g_Config::bDeinoAimJump<<",speed_mps,"<<g_Config::DeinoAimJumpSpeed<<'\n';
        R.File<<"elapsed_ms,utc_ms,space,w,a,s,d,shift,mode,custom_mode,role,x,y,z,vx,vy,vz,yaw,pitch,held_jump,slow_falling,latch_state,latched_jump_requested,gravity_scale,holding_jump_gravity,air_control_scalar,max_walk_speed,max_fall_speed,cached_vx,cached_vy,cached_vz,latched_vx,latched_vy,latched_vz,interp_speed,last_queued,last_started,allow_falling,own_latch_active,has_queued_target,queued_target_type,current_target_type,server_latch_state,cached_surface,last_auto_jump,last_jump_input_action\n";
        const auto UTF8=Destination.u8string();
        { std::scoped_lock L(ExtensionStatusMutex); DeinoGlideProbePath.assign(reinterpret_cast<const char*>(UTF8.data()),UTF8.size()); }
        R.Dino=ExtensionObject(D); R.Window.Begin(Now); R.LastFlush=Now; R.Rows=0;
        ArmDeinoActionTrace(D); // read-only mode also captures native wall inputs
        { std::scoped_lock Lock(DeinoActionTrace.Mutex);
            for (const auto& W:DeinoActionTrace.Watches) R.File<<"# watch,"<<W.Label<<','<<(W.Function!=nullptr)<<'\n'; }
        ExtensionStatus(DeinoGlideProbeStatus,"正在记录滑翔；打开菜单结束，最长120秒");
        return true;
    } catch (...) {
        StopDeinoActionTrace(); R.Window={}; R.Dino={}; R.File.close(); return false;
    }
}
void TickDeinoGlideProbe(SDK::AShooterPlayerController* PC,SDK::APrimalDinoCharacter* D,
    SDK::UCharacterMovementComponent* M,bool Ready,bool InputReady) {
    auto& R=DeinoGlideProbe;
    if (!g_Config::bDeinoFlight) { FinishDeinoGlideProbe("disabled"); R.Done=false; R.Mode=0; return; }
    if (R.Done) {
        if (g_Config::bDeinoFlight && Ready && R.Restart) { R.Done=false; R.Restart=false; }
        else return;
    }
    const bool Valid=Ready && M && ExpansionHasClass(D,"deinonychus_character_bp") && !IsFreeCameraActive();
    if (!Valid) {
        if (R.Window.Started) FinishDeinoGlideProbe("paused_or_unavailable");
        else ExtensionStatus(DeinoGlideProbeStatus,"滑翔记录待命：骑恐爪龙，关闭菜单后起跳");
        return;
    }
    const int Mode=DeinoProbeMode();
    if (!Mode || (R.Window.Started && Mode!=R.Mode)) {
        FinishDeinoGlideProbe("conflicting_movement_feature");
        ExtensionStatus(DeinoGlideProbeStatus,"实验模式冲突：每次只选一种记录；抓附追踪须搭配无目标抓附，关闭其他运动实验");
        return;
    }
    if (R.Window.Started && R.Dino.Get()!=D) { FinishDeinoGlideProbe("mount_changed"); return; }
    const auto Now=GetTickCount64();
    if (R.Window.Expired(Now)) { FinishDeinoGlideProbe("time_limit"); return; }
    if (!R.Window.Started) {
        if (M->MovementMode!=SDK::EMovementMode::MOVE_Falling && !(Mode==4 && AirLatchState(D)==2)) return;
        R.Mode=Mode;
        if (!StartDeinoGlideProbe(D,Now)) {
            R.Done=true; ExtensionStatus(DeinoGlideProbeStatus,"滑翔记录文件创建失败，请检查日志目录"); return;
        }
    }
    DrainDeinoActionTrace(R.File,R.Window.Start);
    if (!R.Window.Take(Now)) return;
    auto* F=NativeMountFunction(D,"Actor","K2_GetActorLocation",24);
    if (!NativeMountParam(F,"ReturnValue","StructProperty",0,24)) {
        FinishDeinoGlideProbe("location_contract_failed");
        ExtensionStatus(DeinoGlideProbeStatus,"滑翔位置入口校验失败，记录已停止"); return;
    }
    SDK::FVector Position{}; D->ProcessEvent(F,&Position);
    const auto V=M->Velocity; const auto Rotation=PC->GetControlRotation();
    FILETIME FT{}; GetSystemTimeAsFileTime(&FT); ULARGE_INTEGER UTC{}; UTC.LowPart=FT.dwLowDateTime; UTC.HighPart=FT.dwHighDateTime;
    R.File<<(Now-R.Window.Start)<<','<<(UTC.QuadPart/10000ULL-11644473600000ULL);
    for (int Key : {VK_SPACE,int('W'),int('A'),int('S'),int('D'),VK_SHIFT}) R.File<<','<<(InputReady && (GetAsyncKeyState(Key)&0x8000)!=0);
    R.File<<','<<int(M->MovementMode)<<','<<int(M->CustomMovementMode)<<','<<int(D->Role)
        <<','<<Position.X<<','<<Position.Y<<','<<Position.Z<<','<<V.X<<','<<V.Y<<','<<V.Z<<','<<Rotation.Yaw<<','<<Rotation.Pitch;
    for (const char* Name : {"IsJumpButtonHeld","bIsHeldJumpSlowFalling","UberpounceState","LatchedJumpRequested"}) R.File<<','<<GlideProbeScalar(D,Name);
    R.File<<','<<GlideProbeScalar(M,"GravityScale")<<','<<GlideProbeScalar(D,"HoldingJumpGravScale")
        <<','<<GlideProbeScalar(D,"AirControlScalar")<<','<<GlideProbeScalar(M,"MaxWalkSpeed")<<','<<GlideProbeScalar(D,"MaxFallSpeed");
    for (const char* Name : {"CachedLatchJumpVelocity","LatchedJumpVelocity"}) {
        const double Missing=std::numeric_limits<double>::quiet_NaN(); SDK::FVector Vec{Missing,Missing,Missing};
        if (!GlideProbeVector(D,Name,Vec)) Vec={Missing,Missing,Missing};
        R.File<<','<<Vec.X<<','<<Vec.Y<<','<<Vec.Z;
    }
    for (const char* Name : {"LatchedJumpVelcityInterpSpeed","LastTimeQueuedAutoJump","LastTimeStartedLatchedJump"}) R.File<<','<<GlideProbeScalar(D,Name);
    R.File<<','<<GlideProbeScalar(M,"bAllowFalling")<<','<<DeinoAirLatch.Active
        <<','<<GlideProbeScalar(D,"HasQueuedUberpounce")<<','<<GlideProbeTargetType(D,"UberpounceQueuedData")
        <<','<<GlideProbeTargetType(D,"UberpounceCurrentData")<<','<<GlideProbeScalar(D,"UberpounceServerState")
        <<','<<GlideProbeScalar(D,"CachedHasLatchableSurfaceInFrontOfDeinonychusResult")
        <<','<<GlideProbeScalar(D,"LastTimeAutoLatchedJump")<<','<<GlideProbeScalar(D,"LastTimeJumpReleasedTookAction")<<'\n';
    ++R.Rows;
    if (Now-R.LastFlush>=1000) { R.File.flush(); R.LastFlush=Now; }
    if (!R.File.good()) FinishDeinoGlideProbe("write_failed");
}

