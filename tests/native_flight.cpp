// Execute the production local-camera lifecycle with a controllable game surface.
// The fake SDK intentionally exposes no movement, possession or administrator API.
#include "../src/internal/Hack/CameraControls.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <cassert>
#include <iostream>
using DWORD=unsigned long;
constexpr unsigned VK_SPACE=32,VK_ESCAPE=27,VK_CONTROL=17,VK_SHIFT=16;
static uint64_t Clock=1000;
static DWORD ForegroundPID=1;
static std::array<bool,256> Keys{};
uint64_t GetTickCount64(){return Clock;}
void* GetForegroundWindow(){return nullptr;}
DWORD GetCurrentProcessId(){return 1;}
void GetWindowThreadProcessId(void*,DWORD* PID){*PID=ForegroundPID;}
short GetAsyncKeyState(int Key){return Keys.at(Key)?short(0x8000):0;}
namespace SDK {
    struct UFunction;
    struct UClass {UFunction* GetFunction(const char*,const char*);};
    inline UClass Class;
    struct UObject {UClass* Class=&SDK::Class;bool Live=true;void ProcessEvent(UFunction*,void*);};
    struct UFunction:UObject {int Size=0;bool Available=true;std::string Name;};
    inline std::map<std::string,UFunction> Functions;
    inline std::map<std::string,UFunction*> Replacements;
    inline UFunction* UClass::GetFunction(const char* Owner,const char* N){
        const std::map<std::string,std::string> Owners{{"GetPlayerCharacter","ShooterPlayerController"},
            {"IsDead","PrimalActor"},{"SetIgnoreMoveInput","Controller"},
            {"SetIgnoreLookInput","Controller"},{"GetInputMouseDelta","PlayerController"}};
        auto O=Owners.find(N);if(O==Owners.end()||O->second!=Owner)return nullptr;
        if(auto R=Replacements.find(N);R!=Replacements.end())return R->second;
        auto I=Functions.find(N);return I!=Functions.end()&&I->second.Available?&I->second:nullptr;
    }
    void InitializeFunctions(){
        Functions.clear();Replacements.clear();
        for(const auto& [Name,Size]:std::array<std::pair<const char*,int>,5>{{
            {"GetPlayerCharacter",int(sizeof(void*))},{"IsDead",1},{"SetIgnoreMoveInput",1},
            {"SetIgnoreLookInput",1},{"GetInputMouseDelta",int(sizeof(float)*2)}}}) {
            auto& F=Functions[Name];F.Name=Name;F.Size=Size;
        }
    }
    struct FVector {double X=0,Y=0,Z=0;FVector operator+(const FVector& V)const{return {X+V.X,Y+V.Y,Z+V.Z};}};
    struct FRotator {double Pitch=0,Yaw=0,Roll=0;};
    struct FMinimalViewInfo {FVector Location;FRotator Rotation;};
    struct APrimalCharacter:UObject {bool Dead=false;FVector BodyLocation{10,20,30};bool IsDead(){return Dead;}};
    struct AShooterCharacter:APrimalCharacter {};
    struct APlayerController:UObject {
        bool Shooter=true,bShowMouseCursor=false,Inventory=false,Chat=false,HUDReady=true;
        APrimalCharacter* Pawn=nullptr;int MoveIgnores=0,LookIgnores=0;float MouseX=0,MouseY=0;
        bool IsA(UClass*){return Shooter;}
    };
    inline std::vector<std::string> Calls;
    inline void UObject::ProcessEvent(UFunction* F,void* Args){
        assert(F&&F->Live&&Args);Calls.push_back(F->Name);
        auto* PC=static_cast<APlayerController*>(this);
        if(F->Name=="SetIgnoreMoveInput")PC->MoveIgnores+=*static_cast<bool*>(Args)?1:-1;
        else if(F->Name=="SetIgnoreLookInput")PC->LookIgnores+=*static_cast<bool*>(Args)?1:-1;
        else if(F->Name=="GetInputMouseDelta"){
            auto* Values=static_cast<float*>(Args);Values[0]=PC->MouseX;Values[1]=PC->MouseY;
            PC->MouseX=PC->MouseY=0;
        }else assert(false&&"Unexpected game request");
        assert(PC->MoveIgnores>=0&&PC->LookIgnores>=0);
    }
    struct AShooterPlayerController:APlayerController {
        AShooterCharacter* Human=nullptr;
        static UClass* StaticClass(){return &SDK::Class;}
        AShooterCharacter* GetPlayerCharacter(){return Human;}
    };
    struct UWorld:UObject {static UWorld* GetWorld();};
    inline UWorld* CurrentWorld=nullptr;
    inline UWorld* UWorld::GetWorld(){return CurrentWorld;}
    struct AShooterPlayerCameraManager:UObject {
        APlayerController* PCOwner=nullptr;
        struct {FMinimalViewInfo POV;} CameraCachePrivate,LastFrameCameraCachePrivate;
        struct {UObject* Target=nullptr;FMinimalViewInfo POV;} ViewTarget;
        bool bGameCameraCutThisFrame=false;
    };
}
namespace g_Util {inline SDK::APlayerController* PC=nullptr;SDK::APlayerController* GetLocalPC(){return PC;}}
namespace g_MDX12::g_MenuState {inline bool g_isOpen=false;}
namespace g_Config {inline bool bFreeCameraEnabled=true;inline float FreeCameraSpeedMeters=15;}
namespace PhoenixShortcuts {
    // ShortcutRuntime's actual reflected main-menu and chat guards have their
    // own SDK-layout regression suite; this surface controls their result here.
    bool GameplayInputReady(SDK::APlayerController* P,bool Menu,bool OwnedInput){
        return P&&P->Shooter&&P->Pawn&&P->HUDReady&&!Menu&&!P->bShowMouseCursor&&
            !P->Inventory&&!P->Chat&&(OwnedInput||(!P->MoveIgnores&&!P->LookIgnores));
    }
}
namespace g_Aimbot {inline int Resets=0;void Reset(){++Resets;}}
namespace g_TekSpear {inline int Resets=0;inline bool CanReset=true;bool Reset(){++Resets;return CanReset;}}
struct LanguageManager {static LanguageManager& Get(){static LanguageManager L;return L;}std::string TranslateCopy(const std::string& S){return S;}};
namespace g_Hack {
    bool IsUsableObject(SDK::UObject* O){return O&&O->Live&&O->Class;}
    struct ExtensionObject {SDK::UObject* Pointer=nullptr;ExtensionObject()=default;explicit ExtensionObject(SDK::UObject* O):Pointer(O){}SDK::UObject* Get()const{return IsUsableObject(Pointer)?Pointer:nullptr;}};
    std::mutex ExtensionStatusMutex;
    void ExtensionStatus(std::string& To,std::string Value){To=Value;}
#include "../src/internal/Hack/FreeCamera.inl"
}
namespace {
    bool Same(const SDK::FVector& A,const SDK::FVector& B){return A.X==B.X&&A.Y==B.Y&&A.Z==B.Z;}
    bool Same(const SDK::FRotator& A,const SDK::FRotator& B){return A.Pitch==B.Pitch&&A.Yaw==B.Yaw&&A.Roll==B.Roll;}
    bool Same(const SDK::FMinimalViewInfo& A,const SDK::FMinimalViewInfo& B){return Same(A.Location,B.Location)&&Same(A.Rotation,B.Rotation);}
    struct Fixture {
        SDK::AShooterPlayerController PC;SDK::AShooterPlayerCameraManager Camera;
        SDK::AShooterCharacter Human;SDK::UWorld World;SDK::UObject ViewActor;
        SDK::FMinimalViewInfo Native{{100,200,300},{0,0,0}},Previous{{90,190,290},{1,2,3}},Target{{110,210,310},{4,5,6}};
        Fixture(){
            SDK::InitializeFunctions();SDK::Calls.clear();Keys.fill(false);Clock+=1000;ForegroundPID=1;
            g_Config::bFreeCameraEnabled=true;g_Config::FreeCameraSpeedMeters=15;g_MDX12::g_MenuState::g_isOpen=false;
            g_Aimbot::Resets=g_TekSpear::Resets=0;g_TekSpear::CanReset=true;
            PC.Human=&Human;PC.Pawn=&Human;Camera.PCOwner=&PC;Camera.ViewTarget.Target=&ViewActor;
            g_Util::PC=&PC;SDK::CurrentWorld=&World;WriteNative();
        }
        ~Fixture(){g_Hack::ResetFreeCamera();g_Util::PC=nullptr;SDK::CurrentWorld=nullptr;}
        void WriteNative(){Camera.CameraCachePrivate.POV=Native;Camera.LastFrameCameraCachePrivate.POV=Previous;Camera.ViewTarget.POV=Target;}
        void Update(float Dt=.016f){g_Hack::UpdateFreeCamera(&Camera,Dt);}
        void Frame(float Dt=.016f){g_Hack::PrepareFreeCameraUpdate(&Camera);Update(Dt);}
        void DoubleTap(){
            Frame();
            g_Hack::HandleFreeCameraKey(VK_SPACE,true,false);g_Hack::HandleFreeCameraKey(VK_SPACE,false,false);
            Clock+=100;
            g_Hack::HandleFreeCameraKey(VK_SPACE,true,false);g_Hack::HandleFreeCameraKey(VK_SPACE,false,false);
            Frame();
        }
        void Enter(){DoubleTap();assert(g_Hack::IsFreeCameraActive());assert(PC.MoveIgnores==1&&PC.LookIgnores==1);}
        void AssertRestored(){
            assert(!g_Hack::IsFreeCameraActive());assert(PC.MoveIgnores==0&&PC.LookIgnores==0);
            assert(Same(Camera.CameraCachePrivate.POV,Native));assert(Same(Camera.LastFrameCameraCachePrivate.POV,Previous));
            assert(Same(Camera.ViewTarget.POV,Target));assert(Same(Human.BodyLocation,SDK::FVector{10,20,30}));
        }
    };
    void MovementAndPoseLifecycle(){
        Fixture F;F.Enter();assert(g_Aimbot::Resets==1&&g_TekSpear::Resets==1);
        assert(F.Camera.bGameCameraCutThisFrame);assert(!g_Hack::IsFreeCameraBuilding());
        SDK::FVector P,D;assert(!g_Hack::GetFreeCameraBuildView(&F.PC,&F.Human,P,D));
        Keys['W']=true;F.Frame(.1f);
        assert(std::abs(F.Camera.CameraCachePrivate.POV.Location.X-250)<.0001);
        assert(Same(F.Human.BodyLocation,SDK::FVector{10,20,30}));
        assert(Same(F.Camera.ViewTarget.POV.Location,F.Camera.CameraCachePrivate.POV.Location));
        g_Hack::PrepareFreeCameraUpdate(&F.Camera);
        assert(Same(F.Camera.CameraCachePrivate.POV,F.Native));
        assert(Same(F.Camera.LastFrameCameraCachePrivate.POV,F.Previous));
        assert(Same(F.Camera.ViewTarget.POV,F.Target));
        // Simulate the next engine update: restoration must use this new pose.
        F.Native={{101,202,303},{7,8,9}};F.Previous={{99,198,297},{4,5,6}};F.Target={{102,204,306},{1,2,3}};
        F.WriteNative();Keys.fill(false);F.PC.MouseX=20;F.PC.MouseY=-10;F.Update();
        assert(std::abs(F.Camera.CameraCachePrivate.POV.Rotation.Yaw-3)<.00001);
        assert(std::abs(F.Camera.CameraCachePrivate.POV.Rotation.Pitch-1.5)<.00001);
        assert(g_Hack::HandleFreeCameraKey(VK_ESCAPE,true,false));F.Frame();F.AssertRestored();
        const auto Calls=SDK::Calls.size();g_Hack::ResetFreeCamera();assert(SDK::Calls.size()==Calls);
    }
    void InputOwnershipAndKeys(){
        Fixture F;F.Frame();assert(!g_Hack::HandleFreeCameraKey('W',true,false));F.Enter();
        for(unsigned Key:{unsigned('W'),unsigned('1'),unsigned('E'),VK_CONTROL,VK_SHIFT}){
            assert(g_Hack::HandleFreeCameraKey(Key,true,false));assert(!g_Hack::HandleFreeCameraKey(Key,false,false));
        }
        // Other systems may add their own ignores during local flight.
        F.PC.MoveIgnores+=2;F.PC.LookIgnores+=3;g_Hack::ResetFreeCamera();
        assert(F.PC.MoveIgnores==2&&F.PC.LookIgnores==3);
        assert(!g_Hack::HandleFreeCameraKey('W',false,false));assert(!g_Hack::HandleFreeCameraKey(VK_SPACE,false,false));
        F.DoubleTap();assert(!g_Hack::IsFreeCameraActive());
        F.PC.MoveIgnores=F.PC.LookIgnores=0;F.AssertRestored();
    }
    void ExitTriggers(){
        for(int Trigger=0;Trigger<13;++Trigger){
            Fixture F;F.Enter();SDK::UWorld OtherWorld;SDK::AShooterCharacter OtherHuman;SDK::UObject OtherView;
            switch(Trigger){
                case 0:g_Config::bFreeCameraEnabled=false;break;
                case 1:g_MDX12::g_MenuState::g_isOpen=true;break;
                case 2:F.PC.Chat=true;break;
                case 3:F.PC.Inventory=true;break;
                case 4:ForegroundPID=2;break;
                case 5:F.Human.Dead=true;break;
                case 6:SDK::CurrentWorld=&OtherWorld;break;
                case 7:F.PC.Pawn=&OtherHuman;break;
                case 8:F.Camera.ViewTarget.Target=&OtherView;break;
                case 9:F.PC.Human=&OtherHuman;F.PC.Pawn=&OtherHuman;break;
                case 10:F.PC.bShowMouseCursor=true;break;
                case 11:F.PC.HUDReady=false;break;
                case 12:SDK::CurrentWorld=nullptr;break;
            }
            F.Frame();F.AssertRestored();
        }
        Fixture F;F.Enter();F.DoubleTap();F.AssertRestored();
    }
    void EntryGuards(){
        for(const char* Name:{"GetPlayerCharacter","IsDead","SetIgnoreMoveInput","SetIgnoreLookInput","GetInputMouseDelta"}){
            Fixture F;SDK::Functions[Name].Available=false;F.DoubleTap();F.AssertRestored();assert(SDK::Calls.empty());
        }
        for(const char* Name:{"GetPlayerCharacter","IsDead","SetIgnoreMoveInput","SetIgnoreLookInput","GetInputMouseDelta"}){
            Fixture F;++SDK::Functions[Name].Size;F.DoubleTap();F.AssertRestored();assert(SDK::Calls.empty());
        }
        for(int Case=0;Case<7;++Case){
            Fixture F;
            switch(Case){
                case 0:F.PC.Shooter=false;break;
                case 1:F.PC.Human=nullptr;break;
                case 2:F.Camera.ViewTarget.Target=nullptr;break;
                case 3:F.PC.Chat=true;break;
                case 4:F.PC.Inventory=true;break;
                case 5:F.World.Live=false;break;
                case 6:g_Util::PC=nullptr;break;
            }
            F.DoubleTap();F.AssertRestored();assert(SDK::Calls.empty());
        }
        for(int Axis=0;Axis<6;++Axis){
            Fixture F;auto& V=F.Camera.CameraCachePrivate.POV;
            double* Values[]={&V.Location.X,&V.Location.Y,&V.Location.Z,&V.Rotation.Pitch,&V.Rotation.Yaw,&V.Rotation.Roll};
            *Values[Axis]=NAN;F.DoubleTap();assert(!g_Hack::IsFreeCameraActive());
            assert(F.PC.MoveIgnores==0&&F.PC.LookIgnores==0&&SDK::Calls.empty());
        }
        Fixture F;g_TekSpear::CanReset=false;F.DoubleTap();F.AssertRestored();
        assert(g_Aimbot::Resets==1&&g_TekSpear::Resets==1&&SDK::Calls.empty());
    }
    void KeyEdgesAndExternalCameraOwnership(){
        Fixture F;F.Frame();assert(g_Hack::HandleFreeCameraKey(VK_SPACE,true,false));
        Clock+=50;g_Hack::HandleFreeCameraKey(VK_SPACE,true,true);F.Frame();assert(!g_Hack::IsFreeCameraActive());
        g_Hack::HandleFreeCameraKey(VK_SPACE,false,false);Clock+=400;
        g_Hack::HandleFreeCameraKey(VK_SPACE,true,false);g_Hack::HandleFreeCameraKey(VK_SPACE,false,false);
        F.Frame();assert(!g_Hack::IsFreeCameraActive());Clock+=400;F.Enter();
        F.Camera.CameraCachePrivate.POV.Location={999,888,777};F.Camera.ViewTarget.POV.Rotation={66,55,44};
        g_Hack::ResetFreeCamera();
        assert(Same(F.Camera.CameraCachePrivate.POV.Location,SDK::FVector{999,888,777}));
        assert(Same(F.Camera.CameraCachePrivate.POV.Rotation,F.Native.Rotation));
        assert(Same(F.Camera.ViewTarget.POV.Location,F.Target.Location));
        assert(Same(F.Camera.ViewTarget.POV.Rotation,SDK::FRotator{66,55,44}));
        assert(F.PC.MoveIgnores==0&&F.PC.LookIgnores==0);
    }
    void FunctionReplacementAndDeferredRelease(){
        for(const char* Name:{"SetIgnoreMoveInput","SetIgnoreLookInput"}){
            Fixture F;F.Enter();
            SDK::UFunction Replacement=SDK::Functions[Name];
            SDK::Functions[Name].Live=false;SDK::Replacements[Name]=&Replacement;
            g_Hack::ResetFreeCamera();F.AssertRestored();
            SDK::Replacements.clear();
        }
        for(const char* Name:{"SetIgnoreMoveInput","SetIgnoreLookInput"}){
            Fixture F;F.Enter();
            SDK::Functions[Name].Live=false;SDK::Functions[Name].Available=false;
            g_Config::bFreeCameraEnabled=false;F.Frame();
            assert(!g_Hack::IsFreeCameraActive());assert(F.PC.MoveIgnores+F.PC.LookIgnores==1);
            assert(Same(F.Camera.CameraCachePrivate.POV,F.Native));
            const auto Calls=SDK::Calls.size();F.Frame();F.Frame();
            assert(SDK::Calls.size()==Calls); // No double decrement while awaiting the missing function.
            assert(!g_Hack::HandleFreeCameraKey(VK_SPACE,true,false));
            SDK::Functions[Name].Live=true;SDK::Functions[Name].Available=true;F.Frame();F.AssertRestored();
            g_Config::bFreeCameraEnabled=true;F.Enter();g_Hack::ResetFreeCamera();F.AssertRestored();
        }
        // Losing an owner must discard its obsolete input stack without touching another controller.
        Fixture F;F.Enter();F.PC.Live=false;F.Frame();assert(!g_Hack::IsFreeCameraActive());
        assert(F.PC.MoveIgnores==1&&F.PC.LookIgnores==1);
    }
    void CameraAndControllerReplacement(){
        {
            Fixture F;F.Enter();SDK::AShooterPlayerCameraManager OtherCamera;
            OtherCamera.PCOwner=&F.PC;OtherCamera.ViewTarget.Target=&F.ViewActor;
            g_Hack::UpdateFreeCamera(&OtherCamera,.016f);F.AssertRestored();
        }
        {
            Fixture F;F.Enter();SDK::AShooterPlayerController OtherPC;
            OtherPC.Pawn=&F.Human;OtherPC.Human=&F.Human;F.Camera.PCOwner=&OtherPC;g_Util::PC=&OtherPC;
            F.Frame();F.AssertRestored();assert(OtherPC.MoveIgnores==0&&OtherPC.LookIgnores==0);
        }
    }
}
int main(){
    MovementAndPoseLifecycle();InputOwnershipAndKeys();ExitTriggers();EntryGuards();KeyEdgesAndExternalCameraOwnership();
    FunctionReplacementAndDeferredRelease();CameraAndControllerReplacement();
    std::cout<<"Local camera: native POV restoration, local movement, input ownership, key edges, exit contexts, absent/malformed functions, invalid views, automatic-input guards and deferred restoration passed.\n";
}
