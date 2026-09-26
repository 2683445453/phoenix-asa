// Local POV only: no pawn movement, possession or administrator requests.
namespace {
    std::atomic<bool> FreeCameraHotkeyReady{false},FreeCameraActive{false};
    std::atomic<unsigned> FreeCameraToggles{0};
    std::atomic<bool> FreeCameraExit{false};
    std::mutex FreeCameraKeyMutex;
    CameraControls::DoubleTap FreeCameraDoubleTap;
    std::string FreeCameraStatus="步行时双击空格进入本地自由飞行，无需管理员权限";
    bool SameCameraVector(const SDK::FVector& A,const SDK::FVector& B){return A.X==B.X&&A.Y==B.Y&&A.Z==B.Z;}
    bool SameCameraRotation(const SDK::FRotator& A,const SDK::FRotator& B){return A.Pitch==B.Pitch&&A.Yaw==B.Yaw&&A.Roll==B.Roll;}
    bool FiniteCameraPose(const SDK::FMinimalViewInfo& V) {
        return std::isfinite(V.Location.X)&&std::isfinite(V.Location.Y)&&std::isfinite(V.Location.Z)&&
            std::isfinite(V.Rotation.Pitch)&&std::isfinite(V.Rotation.Yaw)&&std::isfinite(V.Rotation.Roll);
    }
    struct OwnedCameraPose {
        SDK::FVector NativeLocation{},AppliedLocation{};
        SDK::FRotator NativeRotation{},AppliedRotation{};
        bool Written=false;
        void Apply(SDK::FMinimalViewInfo& V,const SDK::FVector& P,const SDK::FRotator& R) {
            NativeLocation=V.Location;NativeRotation=V.Rotation;
            V.Location=AppliedLocation=P;V.Rotation=AppliedRotation=R;Written=true;
        }
        void Restore(SDK::FMinimalViewInfo& V) {
            if(!Written)return;
            if(SameCameraVector(V.Location,AppliedLocation))V.Location=NativeLocation;
            if(SameCameraRotation(V.Rotation,AppliedRotation))V.Rotation=NativeRotation;
            Written=false;
        }
    };
    struct LocalFlightRuntime {
        ExtensionObject World,Camera,Controller,Human,ViewActor,MoveFunction,LookFunction,MouseFunction;
        SDK::FVector Location{},PreviousLocation{};
        SDK::FRotator Rotation{},PreviousRotation{};
        OwnedCameraPose Current,Previous,Target;
        bool OwnMove=false,OwnLook=false;
    } FreeView;
    SDK::UFunction* LocalCameraFunction(SDK::UObject* O,const char* Owner,const char* Name,int Size) {
        auto* F=IsUsableObject(O)?O->Class->GetFunction(Owner,Name):nullptr;
        return IsUsableObject(F)&&F->Size==Size?F:nullptr;
    }
    void RestoreFreeCameraPose() {
        auto* C=static_cast<SDK::AShooterPlayerCameraManager*>(FreeView.Camera.Get());
        if(!C)return;
        FreeView.Current.Restore(C->CameraCachePrivate.POV);
        FreeView.Previous.Restore(C->LastFrameCameraCachePrivate.POV);
        FreeView.Target.Restore(C->ViewTarget.POV);
    }
    bool SetOwnedCameraInput(SDK::UObject* PC,const ExtensionObject& Ref,bool Ignore) {
        auto* F=static_cast<SDK::UFunction*>(Ref.Get());
        if(!PC||!F||F->Size!=1)return false;
        struct {bool Value;} Args{Ignore};PC->ProcessEvent(F,&Args);return true;
    }
}
bool IsFreeCameraActive(){return FreeCameraActive.load();}
bool IsFreeCameraBuilding(){return false;}
bool GetFreeCameraBuildView(SDK::UObject*,SDK::APrimalCharacter*,SDK::FVector&,SDK::FVector&){return false;}
std::string GetFreeCameraStatus(){std::scoped_lock L(ExtensionStatusMutex);return LanguageManager::Get().TranslateCopy(FreeCameraStatus);}
bool HandleFreeCameraKey(unsigned int Key,bool Pressed,bool Repeated) {
    // Releases always reach Unreal, including after readiness is lost.
    if(Key==VK_SPACE&&!Pressed){std::scoped_lock L(FreeCameraKeyMutex);FreeCameraDoubleTap.Release();return false;}
    if(!FreeCameraHotkeyReady.load())return false;
    if(Key==VK_SPACE){
        std::scoped_lock L(FreeCameraKeyMutex);
        if(Pressed&&FreeCameraDoubleTap.Press(GetTickCount64(),Repeated))FreeCameraToggles.fetch_add(1);
        return Pressed; // Reserve Space so the entry gesture cannot jump the body.
    }
    if(Key==VK_ESCAPE&&Pressed&&FreeCameraActive.load())FreeCameraExit.store(true);
    // Menu and configurable feature shortcuts are handled first by the input layer.
    return FreeCameraActive.load()&&Pressed;
}
void ResetFreeCamera() {
    FreeCameraHotkeyReady.store(false);
    const bool WasActive=FreeCameraActive.exchange(false);
    FreeCameraToggles.store(0);FreeCameraExit.store(false);RestoreFreeCameraPose();
    auto* PC=FreeView.Controller.Get();
    // Remove exactly our increments, preserving other input-ignore owners.
    const auto Release=[&](bool& Owned,ExtensionObject& Function,const char* Name) {
        if(!Owned)return;
        if(!PC){Owned=false;return;} // Destroyed owner has no remaining input stack.
        if(SetOwnedCameraInput(PC,Function,false)){Owned=false;return;}
        Function=ExtensionObject(LocalCameraFunction(PC,"Controller",Name,1));
        if(SetOwnedCameraInput(PC,Function,false))Owned=false;
    };
    Release(FreeView.OwnMove,FreeView.MoveFunction,"SetIgnoreMoveInput");
    Release(FreeView.OwnLook,FreeView.LookFunction,"SetIgnoreLookInput");
    if(auto* C=static_cast<SDK::AShooterPlayerCameraManager*>(FreeView.Camera.Get());C&&WasActive)C->bGameCameraCutThisFrame=true;
    const bool Pending=FreeView.OwnMove||FreeView.OwnLook;
    if(Pending){FreeView.Camera={};FreeView.World={};FreeView.Human={};FreeView.ViewActor={};}
    else FreeView={};
    {std::scoped_lock L(FreeCameraKeyMutex);FreeCameraDoubleTap.Reset();}
    if(Pending)ExtensionStatus(FreeCameraStatus,"正在释放自动输入，请稍后重新双击空格");
    else if(WasActive)ExtensionStatus(FreeCameraStatus,"已返回人物视角，本地相机与输入已恢复");
}
void PrepareFreeCameraUpdate(SDK::AShooterPlayerCameraManager* C) {
    if(C&&FreeView.Camera.Get()==C)RestoreFreeCameraPose();
}
void UpdateFreeCamera(SDK::AShooterPlayerCameraManager* Camera,float DeltaTime) {
    if(!FreeCameraActive.load()&&(FreeView.OwnMove||FreeView.OwnLook)){ResetFreeCamera();return;}
    auto* LocalPC=g_Util::GetLocalPC();
    auto* PC=IsUsableObject(Camera)?Camera->PCOwner:nullptr;
    if(PC&&LocalPC&&PC!=LocalPC)return;
    auto* Class=SDK::AShooterPlayerController::StaticClass();
    auto* ShooterPC=IsUsableObject(PC)&&Class&&PC->IsA(Class)?static_cast<SDK::AShooterPlayerController*>(PC):nullptr;
    auto* World=SDK::UWorld::GetWorld();
    if(!g_Config::bFreeCameraEnabled||!IsUsableObject(World)||!ShooterPC||PC!=LocalPC||
        !LocalCameraFunction(ShooterPC,"ShooterPlayerController","GetPlayerCharacter",sizeof(SDK::AShooterCharacter*))) {
        ResetFreeCamera();return;
    }
    auto* Human=ShooterPC->GetPlayerCharacter();
    if(!IsUsableObject(Human)||PC->Pawn!=Human||
        !LocalCameraFunction(Human,"PrimalActor","IsDead",sizeof(bool))||Human->IsDead()) {
        ResetFreeCamera();ExtensionStatus(FreeCameraStatus,"人物尚未就绪或正在骑乘，请在步行时使用本地自由飞行");return;
    }
    if(FreeView.Controller.Pointer&&(FreeView.Controller.Get()!=PC||FreeView.Camera.Get()!=Camera||
        FreeView.World.Get()!=World||FreeView.Human.Get()!=Human||FreeView.ViewActor.Get()!=Camera->ViewTarget.Target)) {
        ResetFreeCamera();return;
    }
    DWORD ForegroundPID=0;GetWindowThreadProcessId(GetForegroundWindow(),&ForegroundPID);
    const bool Ready=ForegroundPID==GetCurrentProcessId()&&PhoenixShortcuts::GameplayInputReady(
        PC,g_MDX12::g_MenuState::g_isOpen,FreeView.OwnMove&&FreeView.OwnLook);
    if(!Ready){ResetFreeCamera();ExtensionStatus(FreeCameraStatus,"界面、聊天或失焦时已退出本地自由飞行");return;}
    FreeCameraHotkeyReady.store(true);
    const bool Toggle=(FreeCameraToggles.exchange(0)&1u)!=0;
    if(FreeCameraExit.exchange(false)||(Toggle&&FreeCameraActive.load())){ResetFreeCamera();return;}
    if(Toggle&&!FreeCameraActive.load()) {
        auto* Move=LocalCameraFunction(PC,"Controller","SetIgnoreMoveInput",1);
        auto* Look=LocalCameraFunction(PC,"Controller","SetIgnoreLookInput",1);
        auto* Mouse=LocalCameraFunction(PC,"PlayerController","GetInputMouseDelta",sizeof(float)*2);
        if(!Move||!Look||!Mouse||!IsUsableObject(Camera->ViewTarget.Target)||!FiniteCameraPose(Camera->CameraCachePrivate.POV)) {
            ExtensionStatus(FreeCameraStatus,"本地相机接口或视角尚未就绪，未进入飞行");return;
        }
        g_Aimbot::Reset();
        if(!g_TekSpear::Reset()){ExtensionStatus(FreeCameraStatus,"正在释放自动输入，请稍后重新双击空格");return;}
        FreeView.World=ExtensionObject(World);FreeView.Camera=ExtensionObject(Camera);
        FreeView.Controller=ExtensionObject(PC);FreeView.Human=ExtensionObject(Human);FreeView.ViewActor=ExtensionObject(Camera->ViewTarget.Target);
        FreeView.MoveFunction=ExtensionObject(Move);FreeView.LookFunction=ExtensionObject(Look);FreeView.MouseFunction=ExtensionObject(Mouse);
        FreeView.Location=FreeView.PreviousLocation=Camera->CameraCachePrivate.POV.Location;
        FreeView.Rotation=FreeView.PreviousRotation=Camera->CameraCachePrivate.POV.Rotation;
        FreeView.OwnMove=SetOwnedCameraInput(PC,FreeView.MoveFunction,true);FreeView.OwnLook=SetOwnedCameraInput(PC,FreeView.LookFunction,true);
        if(!FreeView.OwnMove||!FreeView.OwnLook){ResetFreeCamera();return;}
        FreeCameraActive.store(true);Camera->bGameCameraCutThisFrame=true;
        ExtensionStatus(FreeCameraStatus,"本地自由飞行中：WASD／空格／Ctrl 移动，Shift 加速，Esc 返回；人物不随相机移动");
    }
    if(!FreeCameraActive.load())return;
    auto* Mouse=static_cast<SDK::UFunction*>(FreeView.MouseFunction.Get());
    if(!Mouse||!FreeView.MoveFunction.Get()||!FreeView.LookFunction.Get()){ResetFreeCamera();return;}
    struct {float X=0,Y=0;} Delta;PC->ProcessEvent(Mouse,&Delta);
    if(std::isfinite(Delta.X)&&std::isfinite(Delta.Y)) {
        FreeView.Rotation.Yaw=std::remainder(FreeView.Rotation.Yaw+Delta.X*.15,360.0);
        FreeView.Rotation.Pitch=std::clamp(FreeView.Rotation.Pitch-Delta.Y*.15,-89.0,89.0);FreeView.Rotation.Roll=0;
    }
    const auto Down=[](int Key){return (GetAsyncKeyState(Key)&0x8000)!=0;};
    const auto Step=CameraControls::Step(FreeView.Rotation.Yaw,FreeView.Rotation.Pitch,
        double(Down('W'))-double(Down('S')),double(Down('D'))-double(Down('A')),
        double(Down(VK_SPACE))-double(Down(VK_CONTROL)),g_Config::FreeCameraSpeedMeters,DeltaTime,Down(VK_SHIFT));
    FreeView.Location=FreeView.Location+SDK::FVector{Step.X,Step.Y,Step.Z};
    FreeView.Current.Apply(Camera->CameraCachePrivate.POV,FreeView.Location,FreeView.Rotation);
    FreeView.Previous.Apply(Camera->LastFrameCameraCachePrivate.POV,FreeView.PreviousLocation,FreeView.PreviousRotation);
    FreeView.Target.Apply(Camera->ViewTarget.POV,FreeView.Location,FreeView.Rotation);
    FreeView.PreviousLocation=FreeView.Location;FreeView.PreviousRotation=FreeView.Rotation;
}
