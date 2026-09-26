// Client-only, reversible experiment. No server memory or correction hooks.
struct DeinoAirLatchRuntime {
    ExtensionObject Dino, Function;
    uint8_t* Code=nullptr;
    uint8_t Original[14]{}, Replacement[14]{};
    uint64_t Started=0;
    bool Active=false;
    uint64_t ReleaseAt=0, ReleaseDeadline=0;
    SDK::FVector ReleaseVelocity{};
    PhoenixAirLatch::Renewal Renewal;
    PhoenixAirLatch::Landing Landing;
    PhoenixNativeMount::Press Enter, Release;
} DeinoAirLatch;
std::string DeinoAirLatchStatus="无目标抓附实验未启用";

SDK::UFunction* AirLatchFunction(SDK::UObject* D,const char* Name,int Size,bool Net=false) {
    if (!IsUsableObject(D)) return nullptr;
    auto* F=D->Class->GetFunction("Deinonychus_Character_BP_C",Name);
    if (!IsUsableObject(F) || F->Size!=Size) return nullptr;
    const auto Flags=static_cast<uint32_t>(F->FunctionFlags);
    if ((Flags&0x400) || bool(Flags&0x40)!=Net || (Net && !(Flags&0x200000))) return nullptr;
    return F;
}
SDK::TArray<uint8_t>* AirLatchScript(SDK::UFunction* F) {
    // Current client reflection capture verifies UStruct.Script at 0x60.
    return reinterpret_cast<SDK::TArray<uint8_t>*>(reinterpret_cast<uint8_t*>(F)+0x60);
}
void RestoreAirLatchCheck() {
    auto* F=static_cast<SDK::UFunction*>(DeinoAirLatch.Function.Get());
    if (F) {
        auto* S=AirLatchScript(F);
        if (S->Num()==568 && const_cast<uint8_t*>(S->GetDataPtr())==DeinoAirLatch.Code &&
            std::memcmp(const_cast<uint8_t*>(S->GetDataPtr()),DeinoAirLatch.Replacement,14)==0)
            std::memcpy(const_cast<uint8_t*>(S->GetDataPtr()),DeinoAirLatch.Original,14);
    }
    DeinoAirLatch.Function={}; DeinoAirLatch.Code=nullptr;
}
bool PatchAirLatchCheck(SDK::APrimalDinoCharacter* D) {
    auto* F=AirLatchFunction(D,"CanContinueUberpounce",441);
    auto* Ret=F ? FindReflectedProperty(F,"Ret") : nullptr;
    if (!NativeMountParam(F,"Ret","BoolProperty",0,1)) return false;
    auto* S=AirLatchScript(F);
    // Validate known bytecode layout, including the return epilogue.
    if (S->Num()!=568 || !const_cast<uint8_t*>(S->GetDataPtr())) return false;
    const uint8_t Prefix[]={0x4c,0x35,0x02,0,0,0x14,0};
    if (std::memcmp(const_cast<uint8_t*>(S->GetDataPtr()),Prefix,sizeof(Prefix)) ||
        const_cast<uint8_t*>(S->GetDataPtr())[565]!=4 || const_cast<uint8_t*>(S->GetDataPtr())[566]!=0x0b || const_cast<uint8_t*>(S->GetDataPtr())[567]!=0x53) return false;
    DeinoAirLatch.Function=ExtensionObject(F); DeinoAirLatch.Code=const_cast<uint8_t*>(S->GetDataPtr());
    std::memcpy(DeinoAirLatch.Original,const_cast<uint8_t*>(S->GetDataPtr()),14);
    // Same EX_LetBool / EX_LocalOutVariable / true / return sequence verified
    // in IsValidUberpounceDataForLatching. Keep the original array allocation.
    uint8_t B[14]={0x14,0x48};
    std::memcpy(B+2,&Ret,sizeof(Ret)); B[10]=0x27; B[11]=4; B[12]=0x0b; B[13]=0x53;
    std::memcpy(DeinoAirLatch.Replacement,B,14); std::memcpy(const_cast<uint8_t*>(S->GetDataPtr()),B,14);
    return true;
}
bool EndAirLatch(SDK::APrimalDinoCharacter* D,SDK::FVector V) {
    DeinoActionMark("own_end_latch");
    auto* Server=AirLatchFunction(D,"ServerEndUberpounce",32,true);
    auto* Local=AirLatchFunction(D,"EndUberpounce",1088);
    if (!NativeMountParam(Server,"Velocity","StructProperty",0,24) ||
        !NativeMountParam(Server,"MontageToPlayOnNonZeroVelocity","ObjectProperty",24,8) ||
        !NativeMountParam(Local,"Velocity","StructProperty",0,24) ||
        !NativeMountParam(Local,"OverrideLocation","BoolProperty",32,1)) return false;
    alignas(16) uint8_t Args[1088]{}; std::memcpy(Args,&V,sizeof(V));
    D->ProcessEvent(Server,Args); D->ProcessEvent(Local,Args); return true;
}
bool JumpFromAirLatch(SDK::AShooterPlayerController* PC,SDK::APrimalDinoCharacter* D) {
    DeinoActionMark("own_space_manual_jump");
    auto* Jump=AirLatchFunction(D,"LatchedJump",644);
    if (!NativeMountParam(Jump,"JumpStrength","DoubleProperty",0,8) ||
        !NativeMountParam(Jump,"IsLatchedAutoJump","BoolProperty",8,1) ||
        !NativeMountParam(Jump,"WallHugDirection","StructProperty",16,24) ||
        !NativeMountParam(Jump,"DirectionOverride","StructProperty",40,24)) return false;
    const auto V=PhoenixAirLatch::ReleaseVelocity(PC->GetControlRotation().Yaw,
        ExtensionLimit(g_Config::DeinoAirLatchReleaseSpeed,1,100,30),
        ExtensionLimit(g_Config::DeinoAirLatchReleaseUpSpeed,0,30,6));
    if (!V.Valid) return false;
    // Native LatchedJump computes its velocity synchronously, starts the jump
    // state and chooses its own request path. Do not append EndUberpounce here:
    // that would cancel the jump we just requested.
    const double Speed=std::hypot(std::hypot(V.X,V.Y),V.Z);
    ExpansionPatch SpeedPatch, CapPatch, HugPatch;
    SpeedPatch.Set(D,"LatchedJumpSpeed",Speed);
    CapPatch.Set(D,"UberpounceLatchedJumpMaxSpeed",Speed);
    HugPatch.Set(D,"LatchedJumpWallHugSpeed",Speed);
    const bool Valid=SpeedPatch.Offset>=0 && CapPatch.Offset>=0 && HugPatch.Offset>=0;
    if (Valid) {
        alignas(16) uint8_t Args[648]{};
        const double Strength=1.0;
        const SDK::FVector Direction{V.X/Speed,V.Y/Speed,V.Z/Speed};
        std::memcpy(Args,&Strength,8); // manual jump, no wall-hug direction
        std::memcpy(Args+40,&Direction,24);
        D->ProcessEvent(Jump,Args);
        DeinoAirLatch.ReleaseVelocity={V.X,V.Y,V.Z};
        DeinoAirLatch.ReleaseAt=GetTickCount64()+50;
        DeinoAirLatch.ReleaseDeadline=DeinoAirLatch.ReleaseAt+450;
    }
    HugPatch.Restore(); CapPatch.Restore(); SpeedPatch.Restore();
    return Valid;
}
bool LaunchAirLatchRelease(SDK::APrimalDinoCharacter* D) {
    DeinoActionMark("own_legacy_launch_fallback");
    auto* F=NativeMountFunction(D,"Character","LaunchCharacter",32);
    if (!NativeMountParam(F,"LaunchVelocity","StructProperty",0,24) ||
        !NativeMountParam(F,"bXYOverride","BoolProperty",24,1) ||
        !NativeMountParam(F,"bZOverride","BoolProperty",25,1)) return false;
    struct { SDK::FVector V; bool XY,Z; uint8_t Pad[6]; } Args{};
    static_assert(sizeof(Args)==32);
    Args.V=DeinoAirLatch.ReleaseVelocity; Args.XY=Args.Z=true;
    D->ProcessEvent(F,&Args); return true;
}
void ResetDeinoAirLatch() {
    if (DeinoAirLatch.Active || DeinoAirLatch.ReleaseAt) DeinoActionMark("own_reset_latch");
    const bool Active=DeinoAirLatch.Active;
    auto* D=static_cast<SDK::APrimalDinoCharacter*>(DeinoAirLatch.Dino.Get());
    DeinoAirLatch.Active=false; RestoreAirLatchCheck();
    if (Active && D) {
        const bool Ended=EndAirLatch(D,{});
        ExtensionStatus(DeinoAirLatchStatus,Ended ? "无目标抓附已取消，检查已恢复" : "检查已恢复，脱离入口失效；请手动下骑");
    }
    DeinoAirLatch={};
}
int AirLatchState(SDK::APrimalDinoCharacter* D) {
    auto* P=IsUsableObject(D) ? FindReflectedProperty(D->Class,"UberpounceState") : nullptr;
    if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString()!="ByteProperty" ||
        P->ElementSize!=1 || P->ArrayDim!=1 || P->Offset<0 || P->Offset>=D->Class->Size) return -1;
    return *(reinterpret_cast<uint8_t*>(D)+P->Offset);
}
bool AirLatchFloor(SDK::APrimalDinoCharacter* D,SDK::UCharacterMovementComponent* M,bool& Close) {
    // Query a fresh floor result: CurrentFloor can still contain pre-jump data.
    auto* F=NativeMountFunction(M,"CharacterMovementComponent","K2_FindFloor",304);
    if (!NativeMountParam(F,"CapsuleLocation","StructProperty",0,24) ||
        !NativeMountParam(F,"FloorResult","StructProperty",24,280)) return false;
    struct { SDK::FVector Position; SDK::FFindFloorResult Floor; } Args{};
    static_assert(sizeof(Args)==304);
    Args.Position=D->K2_GetActorLocation();
    M->ProcessEvent(F,&Args);
    const auto& R=Args.Floor;
    const float Distance=R.bLineTrace ? R.LineDist : R.FloorDist;
    Close=R.bBlockingHit && R.bWalkableFloor && std::isfinite(Distance) && Distance>=-5 && Distance<=5;
    return true;
}
bool BeginAirLatch(SDK::APrimalDinoCharacter* D) {
    auto* Local=AirLatchFunction(D,"StartUberpounceFromFalling",72);
    auto* Server=AirLatchFunction(D,"ServerStartUberpounceFromFalling",120,true);
    auto* Transform=AirLatchFunction(D,"GetUberpounceTransform",864);
    if (!NativeMountParam(Local,"Data","StructProperty",0,72) ||
        !NativeMountParam(Server,"Data","StructProperty",0,72) ||
        !NativeMountParam(Server,"Location","StructProperty",72,24) ||
        !NativeMountParam(Server,"Rotation","StructProperty",96,24) ||
        !NativeMountParam(Transform,"UberpounceData","StructProperty",0,72) ||
        !NativeMountParam(Transform,"Location","StructProperty",72,24) ||
        !NativeMountParam(Transform,"Rotation","StructProperty",96,24)) return false;
    auto* P=static_cast<SDK::FStructProperty*>(FindReflectedProperty(Local,"Data"));
    if (!P->Struct || P->Struct->GetName()!="UberpounceData") return false;
    // Exact captured structure field names and offsets: an empty socket selects
    // the world-coordinate branch; no actor is substituted as a fake target.
    const char* Names[]={"Socket_2_5083A12F491F7162FEFFF2A97F18030D",
        "WorldGeoLocation_7_ADBE3EC7480595A1AF6B0B9FA97EE3F9",
        "WorldGeoRotation_16_C8BC5C724F99806264418FBB303F78E9",
        "Character_15_A587DB6C415808C1555C65BE528C7559",
        "Type_23_2964764F48C642C4AF1E538ACB43EA6E"};
    const int Offsets[]={0,8,32,56,64}, Sizes[]={8,24,24,8,1};
    for (int I=0;I<5;++I) {
        auto* Field=FindReflectedProperty(P->Struct,Names[I]);
        if (!Field || Field->Offset!=Offsets[I] || Field->ElementSize!=Sizes[I] || Field->ArrayDim!=1) return false;
    }
    const auto Position=D->K2_GetActorLocation();
    if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z)) return false;
    // Retain the data struct's zero/default Type; do not invent enum meanings.
    // Pose/position may be rejected by the game's other state checks.
    alignas(16) uint8_t Args[864]{};
    auto Rotation=D->K2_GetActorRotation(); Rotation.Pitch=Rotation.Roll=0;
    auto SurfaceRotation=Rotation; SurfaceRotation.Roll=90;
    std::memcpy(Args+8,&Position,24); std::memcpy(Args+32,&SurfaceRotation,24);
    D->ProcessEvent(Transform,Args);
    SDK::FVector Target{}; std::memcpy(&Target,Args+72,24);
    const auto Delta=Target-Position;
    if (!std::isfinite(Delta.Magnitude()) || Delta.Magnitude()>500) return false;
    // Compensate native capsule/mesh offset so the candidate anchor resolves
    // near the current location, rather than teleporting to an offset target.
    const auto Anchor=Position-Delta;
    std::memcpy(Args+8,&Anchor,24);
    std::memcpy(Args+72,&Position,24); std::memcpy(Args+96,&Rotation,24);
    if (!PatchAirLatchCheck(D)) return false;
    DeinoAirLatch.Dino=ExtensionObject(D); DeinoAirLatch.Active=true; DeinoAirLatch.Started=GetTickCount64();
    DeinoAirLatch.Renewal.Submitted(DeinoAirLatch.Started);
    DeinoActionMark("own_begin_latch");
    D->ProcessEvent(Server,Args); D->ProcessEvent(Local,Args);
    return true;
}
void TickDeinoAirLatch(SDK::AShooterPlayerController* PC,SDK::APrimalDinoCharacter* D,
                       SDK::UCharacterMovementComponent* M,bool Ready) {
    if (!g_Config::bDeinoAirLatch || !Ready || !M || IsFreeCameraActive() ||
        !ExpansionHasClass(D,"deinonychus_character_bp") ||
        !(D->Role==SDK::ENetRole::ROLE_AutonomousProxy || D->Role==SDK::ENetRole::ROLE_Authority)) {
        ResetDeinoAirLatch(); return;
    }
    if ((DeinoAirLatch.Active || DeinoAirLatch.ReleaseAt) && DeinoAirLatch.Dino.Get()!=D) ResetDeinoAirLatch();
    if (PhoenixShortcuts::Conflict(VK_F6,nullptr)) {
        ResetDeinoAirLatch(); ExtensionStatus(DeinoAirLatchStatus,"F6已绑定菜单或快捷功能，请先解除冲突"); return;
    }
    const auto Now=GetTickCount64();
    const bool Enter=DeinoAirLatch.Enter.Take((GetAsyncKeyState(VK_F6)&0x8000)!=0,Now);
    const bool Release=DeinoAirLatch.Release.Take((GetAsyncKeyState(VK_SPACE)&0x8000)!=0,Now);
    if (DeinoAirLatch.ReleaseAt) {
        if (Enter || Now>DeinoAirLatch.ReleaseDeadline) {
            DeinoAirLatch.ReleaseAt=0;
            ExtensionStatus(DeinoAirLatchStatus,"脱离起跳已取消或等待超时，未追加脉冲");
        } else if (Now>=DeinoAirLatch.ReleaseAt && AirLatchState(D)==0) {
            // Consume before entering game code. Never accumulate repeated
            // impulses, and do not launch into an active latch or after landing.
            DeinoAirLatch.ReleaseAt=0;
            bool FloorClose=false;
            const bool Air=PhoenixAirLatch::AllowsRenewalMode(static_cast<int>(M->MovementMode)) &&
                AirLatchFloor(D,M,FloorClose) && !FloorClose;
            const bool Applied=Air && LaunchAirLatchRelease(D);
            ExtensionStatus(DeinoAirLatchStatus,Applied ? "已提交水平及向上起跳脉冲；服务器仍可能回拉" : "脱离起跳条件或入口校验失败，未追加脉冲");
        }
        return;
    }
    if (DeinoAirLatch.Active) {
        if (Release || Enter) {
            DeinoActionMark(Release ? "own_space_pressed" : "own_f6_cancel");
            // Stop renewal before entering game code, but retain the owned
            // continuation check until the native jump invocation returns.
            DeinoAirLatch.Active=false;
            if (Release) {
                const bool Invoked=JumpFromAirLatch(PC,D);
                RestoreAirLatchCheck();
                if (Invoked) {
                    ExtensionStatus(DeinoAirLatchStatus,"已调用抓附跳跃，等待解除后追加一次起跳脉冲");
                } else {
                    const bool Ended=EndAirLatch(D,{});
                    ExtensionStatus(DeinoAirLatchStatus,Ended ? "抓附跳跃入口校验失败，已取消抓附" : "检查已恢复，脱离入口失效；请手动下骑");
                }
            } else {
                RestoreAirLatchCheck();
                const bool Sent=EndAirLatch(D,{});
                ExtensionStatus(DeinoAirLatchStatus,Sent ? "无目标抓附已取消，检查已恢复" : "检查已恢复，脱离入口失效；请手动下骑");
            }
        } else {
            const int State=AirLatchState(D);
            if (State<0 || State>5 || !PhoenixAirLatch::AllowsRenewalMode(static_cast<int>(M->MovementMode))) {
                ResetDeinoAirLatch();
                ExtensionStatus(DeinoAirLatchStatus,"进入游泳或状态读取异常，自动续抓已停止");
                return;
            }
            if (State!=0) DeinoAirLatch.Landing={};
            if (DeinoAirLatch.Renewal.Take(Now,State,true)) {
                DeinoActionMark("own_renewal_check");
                bool FloorClose=false;
                if (!AirLatchFloor(D,M,FloorClose)) {
                    DeinoAirLatch.Landing={};
                    ExtensionStatus(DeinoAirLatchStatus,"地面检测入口校验失败，续抓暂停；空格或F6退出");
                    return;
                }
                if (DeinoAirLatch.Landing.Confirm(Now,FloorClose)) {
                    ResetDeinoAirLatch();
                    ExtensionStatus(DeinoAirLatchStatus,"已连续确认落地，自动续抓停止");
                    return;
                }
                if (FloorClose) {
                    ExtensionStatus(DeinoAirLatchStatus,"正在确认落地，暂缓续抓");
                    return;
                }
                // Restore before reapplying so the saved baseline never becomes
                // our own 'true' stub. Renew only after the prior state ended.
                RestoreAirLatchCheck();
                DeinoAirLatch.Active=false;
                if (!BeginAirLatch(D)) {
                    ResetDeinoAirLatch();
                    ExtensionStatus(DeinoAirLatchStatus,"续抓校验失败，已恢复检查并停止");
                } else {
                    ExtensionStatus(DeinoAirLatchStatus,"正在自动续抓；空格脱离，F6取消");
                }
            }
        }
        return;
    }
    if (!Enter) return;
    if (M->MovementMode!=SDK::EMovementMode::MOVE_Falling || AirLatchState(D)!=0) {
        ExtensionStatus(DeinoAirLatchStatus,"请在未抓附的下落状态轻按F6"); return;
    }
    ExtensionStatus(DeinoAirLatchStatus,BeginAirLatch(D) ?
        "正在自动续抓；空格脱离，F6取消" : "抓附函数或字节码校验未通过，未进入实验");
}
