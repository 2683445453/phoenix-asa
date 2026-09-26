// Adapt the reference repository's CharacterMovement flying mode to an
// isolated nonreplicated Character. The survivor and its movement stay intact.
namespace {
    std::atomic<bool> FreeCameraActive{false};
    std::atomic<unsigned> NativeFlightStage{0}, NativeFlightError{0}, NativeFlightExitReason{0};
    ExpansionPatch LocalFlightAutoCamera;
    std::atomic<bool> FreeCameraExit{false};
    std::string FreeCameraStatus = "按 Z 开启本地自由相机；可在快捷键页面修改";
    struct LocalGcmFlight {
        ExtensionObject World, Controller, Human, ViewTarget, Proxy, Movement, Camera;
        bool OwnMove = false;
    } GcmFlight;

    SDK::UFunction* LocalFlightFunction(SDK::UObject* O, const char* Owner, const char* Name, int Size) {
        // Class default objects are valid for capability checks during spawn.
        // IsUsableObject deliberately rejects them, which made every spawn fail.
        auto* F = O && O->Class && !(O->Flags & SDK::EObjectFlags::BeginDestroyed) &&
            !(O->Flags & SDK::EObjectFlags::FinishDestroyed) ? O->Class->GetFunction(Owner, Name) : nullptr;
        return IsUsableObject(F) && F->Size == Size && !(uint32_t(F->FunctionFlags) & 0x40u) ? F : nullptr;
    }
    bool LocalFlightCall(SDK::UObject* O, const char* Owner, const char* Name, int Size=0, void* Args=nullptr) {
        auto* F=LocalFlightFunction(O,Owner,Name,Size);
        if(!F)return false; O->ProcessEvent(F,Args); return true;
    }
    bool LocalFlightStatic(const char* Name,int Size) {
        auto* C=SDK::UGameplayStatics::StaticClass();
        auto* O=SDK::UGameplayStatics::GetDefaultObj();
        auto* F=IsUsableObject(C)?C->GetFunction("GameplayStatics",Name):nullptr;
        return O && O->Class==C && IsUsableObject(F) && F->Size==Size &&
            (uint32_t(F->FunctionFlags)&(0x2000u|0x40u))==0x2000u;
    }
    void RetireLocalFlightActor(SDK::UObject* Actor) {
        if(!IsUsableObject(Actor))return;
        // Controller overrides K2_DestroyActor with an empty implementation.
        // The native lifespan timer owns its callback even after DLL unload.
        float Remaining=0.001f;
        LocalFlightCall(Actor,"Actor","SetLifeSpan",4,&Remaining);
        LocalFlightCall(Actor,"Actor","K2_DestroyActor");
    }
    bool SetLocalFlightValue(SDK::UObject* O,const char* Name,float Value) {
        ExpansionPatch P; P.Set(O,Name,Value);
        // Only locally created objects use this helper; destruction releases them.
        return P.Offset>=0 && P.Read(O)==Value;
    }
    SDK::AActor* SpawnLocalFlightActor(SDK::UWorld* World,SDK::UClass* Class,const SDK::FTransform& Transform) {
        if(!IsUsableObject(Class) || !LocalFlightStatic("BeginDeferredActorSpawnFromClass",144) ||
            !LocalFlightStatic("FinishSpawningActor",128)){NativeFlightError.store(10);return nullptr;}
        auto* Template=static_cast<SDK::AActor*>(Class->ClassDefaultObject);
        if(!Template || Template->Class!=Class || !Template->IsDefaultObject() ||
            !LocalFlightFunction(Template,"Actor","SetLifeSpan",4)){NativeFlightError.store(11);return nullptr;}
        SDK::AActor* A=nullptr;
        {
            // Native CanSpawnActor rejects a replicated CDO on NM_Client before
            // an instance exists. Restore only these two bits synchronously;
            // the newly constructed instance retains its local-only defaults.
            struct LocalTemplate {
                SDK::AActor* Actor; bool Replicates, Movement;
                explicit LocalTemplate(SDK::AActor* T):Actor(T),Replicates(T->bReplicates),Movement(T->bReplicateMovement){
                    Actor->bReplicates=false;Actor->bReplicateMovement=false;
                }
                ~LocalTemplate(){Actor->bReplicates=Replicates;Actor->bReplicateMovement=Movement;}
            } Defaults(Template);
            A=SDK::UGameplayStatics::BeginDeferredActorSpawnFromClass(World,Class,Transform,
                SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn,nullptr,SDK::ESpawnActorScaleMethod::OverrideRootScale);
        }
        if(!IsUsableObject(A)){NativeFlightError.store(12);return nullptr;}
        bool Disabled=false;
        if(!LocalFlightCall(A,"Actor","SetReplicates",1,&Disabled) ||
            !LocalFlightCall(A,"Actor","SetReplicateMovement",1,&Disabled)) {
            NativeFlightError.store(13);RetireLocalFlightActor(A); return nullptr;
        }
        auto* Finished=SDK::UGameplayStatics::FinishSpawningActor(A,Transform,SDK::ESpawnActorScaleMethod::OverrideRootScale);
        if(Finished!=A || !IsUsableObject(A) || A->bReplicates || A->bReplicateMovement) {
            NativeFlightError.store(14);RetireLocalFlightActor(A); return nullptr;
        }
        return A;
    }
    bool SetLocalFlightView(SDK::APlayerController* PC,SDK::AActor* Target) {
        if(!IsUsableObject(Target) || !LocalFlightFunction(PC,"PlayerController","SetViewTargetWithBlend",24))return false;
        PC->SetViewTargetWithBlend(Target,0,SDK::EViewTargetBlendFunction::VTBlend_Linear,0,false);
        return PC->GetViewTarget()==Target;
    }
}
bool IsFreeCameraActive(){return FreeCameraActive.load();}
bool IsFreeCameraBuilding(){return false;}
bool GetFreeCameraBuildView(SDK::UObject*,SDK::APrimalCharacter*,SDK::FVector&,SDK::FVector&){return false;}
std::string GetFreeCameraStatus(){
    const auto Error=NativeFlightError.load();
    if(Error && !FreeCameraActive.load())
        return "灵魂出窍启动失败（阶段 " + std::to_string(NativeFlightStage.load()) +
            "，错误 " + std::to_string(Error) + "）；请反馈此编号";
    std::scoped_lock L(ExtensionStatusMutex);
    return LanguageManager::Get().TranslateCopy(FreeCameraStatus);
}
bool HandleFreeCameraKey(unsigned Key,bool Pressed,bool Repeated) {
    if(Key==VK_ESCAPE&&Pressed&&FreeCameraActive.load()){FreeCameraExit.store(true);return true;}
    return Key==VK_SPACE && FreeCameraActive.load(); // Camera ascent must not jump the survivor.
}
void ResetFreeCamera() {
    FreeCameraExit.store(false);
    auto* PC=static_cast<SDK::APlayerController*>(GcmFlight.Controller.Get());
    auto* Proxy=static_cast<SDK::AActor*>(GcmFlight.Proxy.Get());
    auto* Camera=static_cast<SDK::AActor*>(GcmFlight.Camera.Get());
    if(PC && Camera && LocalFlightFunction(PC,"Controller","GetViewTarget",8) && PC->GetViewTarget()==Camera) {
        auto* Previous=static_cast<SDK::AActor*>(GcmFlight.ViewTarget.Get());
        if(!Previous)Previous=static_cast<SDK::AActor*>(GcmFlight.Human.Get());
        if(Previous&&!SetLocalFlightView(PC,Previous)){FreeCameraActive.store(true);return;}
    }
    if(GcmFlight.OwnMove) {
        bool Disabled=false;
        if(PC&&!LocalFlightCall(PC,"Controller","SetIgnoreMoveInput",1,&Disabled)) {FreeCameraActive.store(true);return;}
        GcmFlight.OwnMove=false;
    }
    LocalFlightAutoCamera.Restore();
    RetireLocalFlightActor(Camera);
    RetireLocalFlightActor(Proxy);
    const bool WasActive=FreeCameraActive.exchange(false);GcmFlight={};
    if(WasActive)g_Config::bFreeCameraEnabled=false;
    if(WasActive)ExtensionStatus(FreeCameraStatus,"已通过原生接口返回人物视角");
}
void PrepareFreeCameraUpdate(SDK::AShooterPlayerCameraManager*){}
void UpdateFreeCamera(SDK::AShooterPlayerCameraManager*,float){}
void TickNativeFreeCamera() {
    auto* World=SDK::UWorld::GetWorld();auto* PC=g_Util::GetLocalPC();
    auto* Shooter=IsUsableObject(PC)&&PC->IsA(SDK::AShooterPlayerController::StaticClass())?static_cast<SDK::AShooterPlayerController*>(PC):nullptr;
    auto* Human=Shooter&&LocalFlightFunction(Shooter,"ShooterPlayerController","GetPlayerCharacter",8)?Shooter->GetPlayerCharacter():nullptr;
    DWORD PID=0;GetWindowThreadProcessId(GetForegroundWindow(),&PID);
    if(!g_Config::bFreeCameraEnabled || !IsUsableObject(World) || !IsUsableObject(Human) ||
        PC->Pawn!=Human || Human->IsDead() || PID!=GetCurrentProcessId()) {
        if(FreeCameraActive.load())NativeFlightExitReason.store(!g_Config::bFreeCameraEnabled?1:PID!=GetCurrentProcessId()?2:3);
        ResetFreeCamera();return;
    }
    if(GcmFlight.Controller.Pointer && (GcmFlight.Controller.Get()!=PC || GcmFlight.Human.Get()!=Human || GcmFlight.World.Get()!=World)) {NativeFlightExitReason.store(4);ResetFreeCamera();return;}
    const bool Ready=PhoenixShortcuts::GameplayInputReady(PC,g_MDX12::g_MenuState::g_isOpen,FreeCameraActive.load());
    if(FreeCameraExit.exchange(false) || (!Ready && FreeCameraActive.load())){
        NativeFlightExitReason.store(5);ResetFreeCamera();return;
    }
    if(Ready&&!FreeCameraActive.load()) {
        const auto Fail=[&](){g_Config::bFreeCameraEnabled=false;ResetFreeCamera();};
        NativeFlightStage.store(1);
        NativeFlightError.store(0);
        NativeFlightExitReason.store(0);
        auto* Camera=PC->PlayerCameraManager;
        if(!IsUsableObject(Camera) || !LocalFlightFunction(PC,"Controller","GetViewTarget",8) ||
            !LocalFlightFunction(PC,"Controller","GetControlRotation",24)){NativeFlightError.store(1);Fail();return;}
        auto* Target=PC->GetViewTarget();if(!IsUsableObject(Target)){NativeFlightError.store(2);Fail();return;}
        const auto Position=Camera->GetCameraLocation();const auto Rotation=Camera->GetCameraRotation();
        if(!std::isfinite(Position.X)||!std::isfinite(Position.Y)||!std::isfinite(Position.Z)){NativeFlightError.store(3);Fail();return;}
        GcmFlight.World=ExtensionObject(World);GcmFlight.Controller=ExtensionObject(PC);
        GcmFlight.Human=ExtensionObject(Human);GcmFlight.ViewTarget=ExtensionObject(Target);
        SDK::FTransform T{};T.Rotation={0,0,0,1};T.Translation=Position;T.Scale3D={1,1,1};
        auto* Proxy=SpawnLocalFlightActor(World,SDK::ACharacter::StaticClass(),T);GcmFlight.Proxy=ExtensionObject(Proxy);
        NativeFlightStage.store(2);
        auto* Movement=ReadReflectedObjectValue(Proxy,"CharacterMovement");GcmFlight.Movement=ExtensionObject(Movement);
        if(!Proxy){Fail();return;}
        if(!IsUsableObject(Movement)){NativeFlightError.store(20);Fail();return;}
        auto* Move=static_cast<SDK::UCharacterMovementComponent*>(Movement);
        if(!LocalFlightFunction(Move,"CharacterMovementComponent","SetMovementMode",2)) {
            NativeFlightError.store(23);Fail();return;
        }
        // The reference OutBody() switches CharacterMovement to MOVE_Flying
        // and tunes these three flight parameters. Apply them only to Proxy.
        if(!SetLocalFlightValue(Move,"bRunPhysicsWithNoController",1) ||
            !SetLocalFlightValue(Move,"MaxFlySpeed",10000.f) ||
            !SetLocalFlightValue(Move,"MaxAcceleration",5000.f) ||
            !SetLocalFlightValue(Move,"BrakingDecelerationFlying",9999.f)) {
            NativeFlightError.store(24);Fail();return;
        }
        Move->SetMovementMode(SDK::EMovementMode::MOVE_Flying,0);
        NativeFlightStage.store(3);
        bool Disabled=false;
        if(Move->MovementMode!=SDK::EMovementMode::MOVE_Flying ||
            !SetLocalFlightValue(Proxy,"BaseEyeHeight",0) ||
            !LocalFlightCall(Proxy,"Actor","SetActorEnableCollision",1,&Disabled) ||
            !LocalFlightFunction(Proxy,"Pawn","AddMovementInput",32) || !LocalFlightFunction(Proxy,"Actor","K2_SetActorRotation",32)) {NativeFlightError.store(24);Fail();return;}
        Proxy->K2_SetActorRotation(Rotation,true);
        // ShooterPlayerCameraManager::UpdateViewTarget replaces non-camera
        // targets with the possessed pawn. Use its supported CameraActor path
        // and let native attachment follow the independently flying Character.
        auto* View=SpawnLocalFlightActor(World,SDK::ACameraActor::StaticClass(),T);
        GcmFlight.Camera=ExtensionObject(View);
        if(!View || !LocalFlightFunction(View,"Actor","K2_AttachToActor",24) ||
            !View->K2_AttachToActor(Proxy,{},SDK::EAttachmentRule::SnapToTarget,
                SDK::EAttachmentRule::SnapToTarget,SDK::EAttachmentRule::KeepWorld,false)) {
            if(!NativeFlightError.load())NativeFlightError.store(26);Fail();return;
        }
        bool Enabled=true;
        GcmFlight.OwnMove=LocalFlightCall(PC,"Controller","SetIgnoreMoveInput",1,&Enabled);
        LocalFlightAutoCamera.Set(PC,"bAutoManageActiveCameraTarget",0);
        if(!GcmFlight.OwnMove||!SetLocalFlightView(PC,View)){NativeFlightError.store(25);Fail();return;}
        FreeCameraActive.store(true);NativeFlightStage.store(4);
        ExtensionStatus(FreeCameraStatus,"原生自由相机：WASD／空格／Ctrl 移动，Shift 加速，Esc 返回");
    }
    if(!FreeCameraActive.load()||!Ready)return;
    auto* Proxy=static_cast<SDK::ACharacter*>(GcmFlight.Proxy.Get());
    auto* Move=GcmFlight.Movement.Get();
    auto* View=GcmFlight.Camera.Get();
    if(!Proxy||!Move||!View||PC->GetViewTarget()!=View){NativeFlightExitReason.store(!Proxy?6:!Move?7:!View?9:8);ResetFreeCamera();return;}
    const auto Rotation=PC->GetControlRotation();Proxy->K2_SetActorRotation(Rotation,true);
    const auto Down=[](int Key){return (GetAsyncKeyState(Key)&0x8000)!=0;};
    const float BaseSpeed=ExtensionLimit(g_Config::FreeCameraSpeedMeters,1,PhoenixInterface::MaxCameraSpeed,15);
    const float Speed=std::min(BaseSpeed*(Down(VK_SHIFT)?4.f:1.f),
        float(PhoenixInterface::MaxCameraSpeed))*100.f;
    static_cast<SDK::UCharacterMovementComponent*>(Move)->MaxFlySpeed=Speed;
    constexpr double Rad=3.14159265358979323846/180;
    const double Y=Rotation.Yaw*Rad,P=Rotation.Pitch*Rad,F=double(Down('W'))-Down('S'),R=double(Down('D'))-Down('A');
    SDK::FVector Direction{std::cos(P)*std::cos(Y)*F-std::sin(Y)*R,std::cos(P)*std::sin(Y)*F+std::cos(Y)*R,std::sin(P)*F+double(Down(VK_SPACE))-Down(VK_CONTROL)};
    const double Length=std::sqrt(Direction.X*Direction.X+Direction.Y*Direction.Y+Direction.Z*Direction.Z);
    if(std::isfinite(Length)&&Length>0){Direction=Direction*(1.0/std::max(1.0,Length));Proxy->AddMovementInput(Direction,1,true);}
}
