// Production lifecycle: native flying mode must only touch the local proxy, and a
// failed entry/exit must retain enough ownership to restore view and input.
#include "../src/internal/Hack/CameraControls.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
using DWORD=unsigned long;
constexpr int VK_SPACE=32,VK_ESCAPE=27,VK_CONTROL=17,VK_SHIFT=16;
bool Keys[256]{};uint64_t Now=1000;DWORD Foreground=1;
short GetAsyncKeyState(int K){return Keys[K]?short(0x8000):0;}
uint64_t GetTickCount64(){return Now;}
void* GetForegroundWindow(){return nullptr;}
void GetWindowThreadProcessId(void*,DWORD* P){*P=Foreground;}
DWORD GetCurrentProcessId(){return 1;}
namespace SDK {
struct UClass;struct UFunction;
enum class EObjectFlags:uint32_t {BeginDestroyed=1,FinishDestroyed=2};
bool operator&(EObjectFlags A,EObjectFlags B){return (uint32_t(A)&uint32_t(B))!=0;}
struct UObject {UClass* Class=nullptr;EObjectFlags Flags=EObjectFlags(0);bool Live=true,Default=false;int Serial=1;UObject* Outer=nullptr;bool IsDefaultObject(){return Default;}void ProcessEvent(UFunction*,void*);};
struct UFunction:UObject {int Size=0;uint32_t FunctionFlags=0;std::string Name;};
std::map<std::string,UFunction> Functions;
struct UClass:UObject {int Size=2048;UObject* ClassDefaultObject=nullptr;UFunction* GetFunction(const char*,const char* N){auto I=Functions.find(N);return I==Functions.end()?nullptr:&I->second;}};
UClass Common,CharacterClass,ControllerClass,ShooterClass,CameraClass;
struct FVector {double X=0,Y=0,Z=0;FVector operator*(double S)const{return{X*S,Y*S,Z*S};}};
struct FRotator {double Pitch=0,Yaw=0,Roll=0;};
struct FTransform {struct {double X,Y,Z,W;}Rotation;FVector Translation,Scale3D;};
enum class EMovementMode {MOVE_Walking,MOVE_Flying};
enum class ESpawnActorCollisionHandlingMethod {AlwaysSpawn};
enum class ESpawnActorScaleMethod {OverrideRootScale};
enum class EViewTargetBlendFunction {VTBlend_Linear};
enum class EAttachmentRule {SnapToTarget,KeepWorld};
struct FName{std::string ToString(){return "ObjectProperty";}};
extern bool AttachFails;
struct AActor:UObject {
    bool bReplicates=true,bReplicateMovement=true;FRotator Rotation;AActor* Parent=nullptr;float LifeSpan=0;
    void K2_SetActorRotation(FRotator R,bool){Rotation=R;}
    bool K2_AttachToActor(AActor* P,FName,EAttachmentRule L,EAttachmentRule R,EAttachmentRule S,bool Weld){
        assert(L==EAttachmentRule::SnapToTarget&&R==L&&S==EAttachmentRule::KeepWorld&&!Weld);
        if(AttachFails)return false;Parent=P;return true;
    }
};
struct ACameraActor:AActor {static UClass* StaticClass(){return &CameraClass;}};
inline int FlightModeCalls=0;
struct UCharacterMovementComponent:UObject {EMovementMode MovementMode=EMovementMode::MOVE_Walking;bool RunWithoutController=false;float MaxFlySpeed=0,Acceleration=0,Braking=0;void SetMovementMode(EMovementMode Mode,uint8_t){++FlightModeCalls;MovementMode=Mode;}};
struct ACharacter:AActor {UCharacterMovementComponent* Movement=nullptr;FVector Input;int Inputs=0;static UClass* StaticClass(){return &CharacterClass;}void AddMovementInput(FVector D,float S,bool F){assert(S==1&&F);Input=D;++Inputs;}};
struct APrimalCharacter:ACharacter {bool Dead=false;bool IsDead(){return Dead;}};
struct AShooterCharacter:APrimalCharacter {};
struct APlayerCameraManager:AActor {FVector GetCameraLocation(){return{1,2,3};}FRotator GetCameraRotation(){return{};}};
struct AShooterPlayerCameraManager:APlayerCameraManager {};
extern bool RefuseView;
struct APlayerController:AActor {
    AActor* Pawn=nullptr;AActor* Target=nullptr;APlayerCameraManager* PlayerCameraManager=nullptr;int IgnoreMove=0;bool AutoCamera=true;
    static UClass* StaticClass(){return &ControllerClass;}bool IsA(UClass* C){return Class==C;}
    AActor* GetViewTarget(){return Target;}FRotator GetControlRotation(){return{0,90,0};}
    void SetViewTargetWithBlend(AActor* A,float,EViewTargetBlendFunction,float,bool){if(!RefuseView)Target=A;}
    void ShooterUpdateViewTarget(){if(Target!=Pawn&&Target->Class!=&CameraClass)Target=Pawn;}
};
struct AShooterPlayerController:APlayerController {AShooterCharacter* Human=nullptr;AShooterCharacter* GetPlayerCharacter(){return Human;}static UClass* StaticClass(){return &ShooterClass;}};
struct UWorld:UObject {static UWorld* GetWorld();};
UWorld* World=nullptr;UWorld* UWorld::GetWorld(){return World;}
struct UCheatManager:UObject {static UClass* StaticClass(){return &Common;}};
struct UGameplayStatics:UObject {
    static UClass* StaticClass(){return &Common;}
    static UGameplayStatics* GetDefaultObj(){static UGameplayStatics O;O.Class=&Common;return &O;}
    static AActor* BeginDeferredActorSpawnFromClass(UWorld*,UClass*,const FTransform&,ESpawnActorCollisionHandlingMethod,AActor*,ESpawnActorScaleMethod);
    static AActor* FinishSpawningActor(AActor* A,const FTransform&,ESpawnActorScaleMethod){assert(!A->bReplicates&&!A->bReplicateMovement);return A;}
    static UObject* SpawnObject(UClass*,UObject*);
};
struct FFieldClass{FName Name;};struct FProperty{FFieldClass* ClassPrivate;int ArrayDim=1,ElementSize=8,Offset=0;};
bool RefuseView=false,SpawnFails=false,AttachFails=false;APlayerController* RealPC=nullptr;
std::vector<std::unique_ptr<UObject>> Owned;
template<class T>T* Make(UClass* C){auto P=std::make_unique<T>();P->Class=C;auto* A=P.get();Owned.push_back(std::move(P));return A;}
AActor* UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld*,UClass* C,const FTransform&,ESpawnActorCollisionHandlingMethod,AActor* Owner,ESpawnActorScaleMethod){
    assert(!Owner);auto* Defaults=static_cast<AActor*>(C->ClassDefaultObject);
    // The real engine rejects replicated defaults before instance creation.
    if(Defaults->bReplicates||SpawnFails)return nullptr;
    assert(!Defaults->bReplicateMovement);
    if(C==&CharacterClass){auto* P=Make<ACharacter>(C);P->Movement=Make<UCharacterMovementComponent>(&Common);return P;}
    if(C==&CameraClass)return Make<ACameraActor>(C);
    assert(C==&ControllerClass);return Make<APlayerController>(C);
}
UObject* UGameplayStatics::SpawnObject(UClass* C,UObject* Outer){auto* O=Make<UCheatManager>(C);O->Outer=Outer;return O;}
void UObject::ProcessEvent(UFunction* F,void* Args){
    const auto N=F->Name;
    if(N=="SetReplicates")static_cast<AActor*>(this)->bReplicates=*static_cast<bool*>(Args);
    else if(N=="SetReplicateMovement")static_cast<AActor*>(this)->bReplicateMovement=*static_cast<bool*>(Args);
    else if(N=="K2_DestroyActor"){assert(this!=RealPC&&this!=RealPC->Pawn);if(Class!=&ControllerClass)Live=false;}
    else if(N=="SetLifeSpan"){assert(this!=RealPC&&this!=RealPC->Pawn);static_cast<AActor*>(this)->LifeSpan=*static_cast<float*>(Args);}
    else if(N=="SetIgnoreMoveInput")static_cast<APlayerController*>(this)->IgnoreMove+=*static_cast<bool*>(Args)?1:-1;
    else if(N=="SetActorEnableCollision")assert(!*static_cast<bool*>(Args)&&this!=RealPC->Pawn);
    else assert(false);
}
void NativeTimers(){for(auto& O:Owned)if(O->Class==&ControllerClass&&static_cast<AActor*>(O.get())->LifeSpan>0)O->Live=false;}
}
namespace g_Util {SDK::APlayerController* GetLocalPC(){return SDK::RealPC;}}
namespace g_Config {bool bFreeCameraEnabled=true;float FreeCameraSpeedMeters=15;}
namespace g_MDX12::g_MenuState {bool g_isOpen=false;}
namespace PhoenixShortcuts {bool GameplayInputReady(SDK::APlayerController*,bool Menu,bool){return !Menu;}}
struct LanguageManager {static LanguageManager& Get(){static LanguageManager L;return L;}std::string TranslateCopy(const std::string& S){return S;}};
namespace g_Hack {
bool IsUsableObject(SDK::UObject* O){return O&&O->Live;}
struct ExtensionObject {SDK::UObject* Pointer=nullptr;int Serial=0;ExtensionObject()=default;explicit ExtensionObject(SDK::UObject* O):Pointer(O),Serial(O?O->Serial:0){}SDK::UObject* Get()const{return IsUsableObject(Pointer)&&Pointer->Serial==Serial?Pointer:nullptr;}};
std::mutex ExtensionStatusMutex;
void ExtensionStatus(std::string& A,const std::string& B){A=B;}
float ExtensionLimit(float V,float L,float H,float F){return std::isfinite(V)?std::clamp(V,L,H):F;}
SDK::FProperty* FindReflectedProperty(SDK::UClass*,const char* N){static SDK::FFieldClass C;static SDK::FProperty P{&C};SDK::APlayerController O;P.Offset=int(reinterpret_cast<uint8_t*>(&O.Pawn)-reinterpret_cast<uint8_t*>(&O));return std::string(N)=="Pawn"?&P:nullptr;}
SDK::UObject* ReadReflectedObjectValue(SDK::UObject* O,const char* N){return O&&std::string(N)=="CharacterMovement"?static_cast<SDK::ACharacter*>(O)->Movement:nullptr;}
struct ExpansionPatch {
    int Offset=-1;float V=0;SDK::APlayerController* Camera=nullptr;bool Original=false;
    void Set(SDK::UObject* O,const char* N,float Value){
        Offset=0;V=Value;if(std::string(N)=="MaxFlySpeed")static_cast<SDK::UCharacterMovementComponent*>(O)->MaxFlySpeed=Value;
        if(std::string(N)=="MaxAcceleration")static_cast<SDK::UCharacterMovementComponent*>(O)->Acceleration=Value;
        if(std::string(N)=="BrakingDecelerationFlying")static_cast<SDK::UCharacterMovementComponent*>(O)->Braking=Value;
        if(std::string(N)=="bRunPhysicsWithNoController")static_cast<SDK::UCharacterMovementComponent*>(O)->RunWithoutController=Value!=0;
        if(std::string(N)=="bAutoManageActiveCameraTarget"){Camera=static_cast<SDK::APlayerController*>(O);Original=Camera->AutoCamera;Camera->AutoCamera=Value!=0;}
    }
    double Read(SDK::UObject*){return V;}
    void Restore(){if(Camera)Camera->AutoCamera=Original;Camera=nullptr;Offset=-1;}
};
#include "../src/internal/Hack/GcmLocalFlight.inl"
}
void Toggle(){g_Config::bFreeCameraEnabled=true;g_Hack::TickNativeFreeCamera();}
int main(){
    for(auto [N,S]:std::vector<std::pair<std::string,int>>{{"SetReplicates",1},{"SetReplicateMovement",1},{"GetViewTarget",8},{"GetControlRotation",24},{"SetViewTargetWithBlend",24},{"GetPlayerCharacter",8},{"BeginDeferredActorSpawnFromClass",144},{"FinishSpawningActor",128},{"SetMovementMode",2},{"K2_DestroyActor",0},{"SetLifeSpan",4},{"K2_AttachToActor",24},{"SetActorEnableCollision",1},{"AddMovementInput",32},{"K2_SetActorRotation",32},{"SetIgnoreMoveInput",1}}){auto& F=SDK::Functions[N];F.Name=N;F.Size=S;F.FunctionFlags=std::string(N)=="BeginDeferredActorSpawnFromClass"||std::string(N)=="FinishSpawningActor"?0x2000:0;}
    SDK::UWorld World;SDK::World=&World;SDK::AShooterCharacter Human;SDK::AShooterPlayerController PC;SDK::APlayerCameraManager Camera;
    SDK::ACharacter CharacterCDO;SDK::APlayerController ControllerCDO;SDK::ACameraActor CameraCDO;
    CharacterCDO.Class=&SDK::CharacterClass;ControllerCDO.Class=&SDK::ControllerClass;CharacterCDO.Default=ControllerCDO.Default=true;
    SDK::CharacterClass.ClassDefaultObject=&CharacterCDO;SDK::ControllerClass.ClassDefaultObject=&ControllerCDO;
    CameraCDO.Class=&SDK::CameraClass;CameraCDO.Default=true;SDK::CameraClass.ClassDefaultObject=&CameraCDO;
    PC.Class=&SDK::ShooterClass;PC.Human=&Human;PC.Pawn=&Human;PC.Target=&Human;PC.PlayerCameraManager=&Camera;SDK::RealPC=&PC;
    Toggle();assert(g_Hack::IsFreeCameraActive()&&PC.Pawn==&Human&&PC.Target!=&Human&&PC.IgnoreMove==1&&SDK::FlightModeCalls==1);
    assert(!PC.AutoCamera);
    assert(CharacterCDO.bReplicates&&CharacterCDO.bReplicateMovement&&ControllerCDO.bReplicates&&ControllerCDO.bReplicateMovement);
    auto* View=PC.Target;auto* Proxy=static_cast<SDK::ACharacter*>(View->Parent);
    assert(View->Class==&SDK::CameraClass&&Proxy&&g_Hack::GcmFlight.Proxy.Get()==Proxy&&!View->bReplicates);
    assert(Proxy->Movement->MovementMode==SDK::EMovementMode::MOVE_Flying&&Proxy->Movement->RunWithoutController);
    assert(Proxy->Movement->Acceleration==5000.f&&Proxy->Movement->Braking==9999.f);
    PC.Target=Proxy;PC.ShooterUpdateViewTarget();assert(PC.Target==&Human); // Reproduces the previous native reset.
    PC.Target=View;PC.ShooterUpdateViewTarget();assert(PC.Target==View);
    Keys['W']=true;g_Hack::TickNativeFreeCamera();Keys['W']=false;
    assert(Proxy->Inputs>0&&Proxy->Input.Y>.99&&Human.Inputs==0);assert(!g_Hack::HandleFreeCameraKey('1',true,false));
    g_Config::FreeCameraSpeedMeters=200;Keys[VK_SHIFT]=true;g_Hack::TickNativeFreeCamera();
    assert(Proxy->Movement->MaxFlySpeed==20000.f);Keys[VK_SHIFT]=false;g_Config::FreeCameraSpeedMeters=15;
    g_MDX12::g_MenuState::g_isOpen=true;g_Hack::TickNativeFreeCamera();assert(!g_Hack::IsFreeCameraActive());g_MDX12::g_MenuState::g_isOpen=false;
    Toggle();assert(g_Hack::IsFreeCameraActive());View=PC.Target;Proxy=static_cast<SDK::ACharacter*>(View->Parent);
    SDK::RefuseView=true;g_Hack::ResetFreeCamera();assert(g_Hack::IsFreeCameraActive()&&Proxy->Live&&PC.IgnoreMove==1);
    SDK::RefuseView=false;g_Hack::ResetFreeCamera();assert(!g_Hack::IsFreeCameraActive()&&!Proxy->Live&&!View->Live&&PC.Target==&Human&&PC.IgnoreMove==0);
    assert(PC.AutoCamera);
    SDK::Functions["SetMovementMode"].Size=0;Toggle();assert(!g_Hack::IsFreeCameraActive()&&PC.Pawn==&Human&&PC.Target==&Human&&PC.IgnoreMove==0);SDK::Functions["SetMovementMode"].Size=2;
    SDK::SpawnFails=true;Toggle();assert(!g_Hack::IsFreeCameraActive()&&g_Hack::NativeFlightError.load()==12&&CharacterCDO.bReplicates&&CharacterCDO.bReplicateMovement);SDK::SpawnFails=false;
    SDK::RefuseView=true;Toggle();assert(!g_Hack::IsFreeCameraActive()&&PC.IgnoreMove==0);SDK::RefuseView=false;
    SDK::AttachFails=true;Toggle();assert(!g_Hack::IsFreeCameraActive()&&g_Hack::NativeFlightError.load()==26&&PC.Target==&Human&&PC.IgnoreMove==0);SDK::AttachFails=false;
    PC.IgnoreMove=2;Toggle();g_Hack::ResetFreeCamera();assert(PC.IgnoreMove==2);PC.IgnoreMove=0;
    Toggle();Foreground=2;g_Hack::TickNativeFreeCamera();assert(!g_Hack::IsFreeCameraActive()&&PC.Target==&Human&&PC.IgnoreMove==0);Foreground=1;
    Toggle();g_Config::bFreeCameraEnabled=false;g_Hack::TickNativeFreeCamera();assert(!g_Hack::IsFreeCameraActive()&&PC.Pawn==&Human&&Human.Inputs==0);
    for(auto& O:SDK::Owned)if(O->Class==&SDK::CharacterClass||O->Class==&SDK::CameraClass)assert(!O->Live);
    std::cout<<"Local flight lifecycle: native movement mode on nonreplicated proxy, unchanged survivor, input ownership, failure cleanup, focus and disable passed.\n";
}
