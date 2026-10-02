// Local visual components only. Never creates a weapon or sends its arc RPCs.
#include "DeinoNativeArcLookup.inl"
#include "DeinoNativeArcAssetLoad.inl"
#include "DeinoArcGeometry.h"
struct DeinoNativeArcRuntime {
    std::array<ExtensionObject,16> Segments;
    ExtensionObject Template;
    uint64_t RetryAt=0,SpawnRetryAt=0;
    uint32_t LoadAttempts=0;
    int LastLoadStatus=2;
} DeinoNativeArc;
// Called on the serialized game thread, including on the ground to preload.
// Resolve or synchronously load the exact beam asset, including its dependencies.
int PrepareDeinoNativeArc(uint64_t Now) {
    auto& R=DeinoNativeArc;
    if(R.Template.Get()) return 1;
    // Throttle failures only. A previously successful weak handle can expire on GC.
    if(Now<R.RetryAt && R.LastLoadStatus!=1) return R.LastLoadStatus;
    R.RetryAt=Now+15000; // A failed load must not stall every hover frame.
    SDK::UObject* Asset=nullptr;
    ++R.LoadAttempts;
    R.LastLoadStatus=LoadDeinoNativeArcAsset(Asset);
    if(R.LastLoadStatus!=1) return R.LastLoadStatus;
    R.Template=ExtensionObject(Asset);
    R.LastLoadStatus=R.Template.Get()?1:2;
    return R.LastLoadStatus;
}
void ClearDeinoNativeArc() {
    for(auto& W:DeinoNativeArc.Segments) {
        auto* C=W.Get(); W={}; if(!C) continue;
        auto* Hide=NativeMountFunction(C,"SceneComponent","SetVisibility",3);
        if(NativeMountParam(Hide,"bNewVisibility","BoolProperty",0,1)) { bool Args[3]{}; C->ProcessEvent(Hide,Args); }
        auto* Off=NativeMountFunction(C,"ActorComponent","Deactivate",0);
        if(Off) C->ProcessEvent(Off,nullptr);
        auto* Owner=NativeMountFunction(C,"ActorComponent","GetOwner",8);
        auto* Destroy=NativeMountFunction(C,"ActorComponent","K2_DestroyComponent",8);
        if(NativeMountParam(Owner,"ReturnValue","ObjectProperty",0,8) && NativeMountParam(Destroy,"Object","ObjectProperty",0,8)) {
            SDK::UObject* O=nullptr; C->ProcessEvent(Owner,&O); C->ProcessEvent(Destroy,&O);
        }
    }
}
// 0 off, 1 components updated (visual appearance still requires a game check),
// 2 asset load failed / retry pending, 3 contract/spawn failed.
int UpdateDeinoNativeArc(SDK::UObject* Context,const std::array<SDK::FVector,33>& Points,uint64_t Now) {
    auto& R=DeinoNativeArc;
    if(!g_Config::bDeinoNativeArc) { ClearDeinoNativeArc(); return 0; }
    const int Prepared=PrepareDeinoNativeArc(Now);
    if(Prepared!=1) return Prepared;
    auto* Template=R.Template.Get();
    if(Now<R.SpawnRetryAt) return 3;
    auto* LibClass=SDK::UNiagaraFunctionLibrary::StaticClass();
    auto* ComponentClass=SDK::UNiagaraComponent::StaticClass();
    auto* NameClass=SDK::UKismetStringLibrary::StaticClass();
    if(!IsUsableObject(LibClass) || !IsUsableObject(ComponentClass) || !IsUsableObject(NameClass)) return 3;
    auto* Lib=LibClass->ClassDefaultObject;
    auto* Names=NameClass->ClassDefaultObject;
    auto* Spawn=DeinoArcClassFunction(LibClass,"NiagaraFunctionLibrary","SpawnSystemAtLocation",112);
    auto* Move=DeinoArcClassFunction(ComponentClass,"SceneComponent","K2_SetWorldLocation",304);
    auto* Set=DeinoArcClassFunction(ComponentClass,"NiagaraComponent","SetVariablePosition",32);
    auto* Name=DeinoArcClassFunction(NameClass,"KismetStringLibrary","Conv_StringToName",24);
    auto* Destroy=DeinoArcClassFunction(ComponentClass,"ActorComponent","K2_DestroyComponent",8);
    auto* Owner=DeinoArcClassFunction(ComponentClass,"ActorComponent","GetOwner",8);
    auto* Activate=DeinoArcClassFunction(ComponentClass,"ActorComponent","Activate",1);
    if(!NativeMountParam(Spawn,"WorldContextObject","ObjectProperty",0,8) ||
        !NativeMountParam(Spawn,"SystemTemplate","ObjectProperty",8,8) ||
        !NativeMountParam(Spawn,"Location","StructProperty",16,24) ||
        !NativeMountParam(Spawn,"Rotation","StructProperty",40,24) ||
        !NativeMountParam(Spawn,"Scale","StructProperty",64,24) ||
        !NativeMountParam(Spawn,"bAutoDestroy","BoolProperty",88,1) ||
        !NativeMountParam(Spawn,"bAutoActivate","BoolProperty",89,1) ||
        !NativeMountParam(Spawn,"PoolingMethod","EnumProperty",90,1) ||
        !NativeMountParam(Spawn,"bPreCullCheck","BoolProperty",91,1) ||
        !NativeMountParam(Spawn,"bDontSpawnOutOfView","BoolProperty",92,1) ||
        !NativeMountParam(Spawn,"DontSpawnOutOfViewCheckRadius","FloatProperty",96,4) ||
        !NativeMountParam(Spawn,"bAllowSpawnLoopingFX","BoolProperty",100,1) ||
        !NativeMountParam(Spawn,"ReturnValue","ObjectProperty",104,8) ||
        !NativeMountParam(Move,"NewLocation","StructProperty",0,24) ||
        !NativeMountParam(Move,"bSweep","BoolProperty",24,1) ||
        !NativeMountParam(Move,"SweepHitResult","StructProperty",32,264) ||
        !NativeMountParam(Move,"bTeleport","BoolProperty",296,1) ||
        !NativeMountParam(Set,"InVariableName","NameProperty",0,8) ||
        !NativeMountParam(Set,"InValue","StructProperty",8,24) ||
        !NativeMountParam(Name,"InString","StrProperty",0,16) ||
        !NativeMountParam(Name,"ReturnValue","NameProperty",16,8) ||
        !NativeMountParam(Activate,"bReset","BoolProperty",0,1) ||
        !NativeMountParam(Owner,"ReturnValue","ObjectProperty",0,8) ||
        !NativeMountParam(Destroy,"Object","ObjectProperty",0,8)) { ClearDeinoNativeArc(); return 3; }
    auto ToName=[&](const wchar_t* Text) {
        struct {SDK::FString Text; SDK::FName Value{};} Args{SDK::FString(Text)};
        Names->ProcessEvent(Name,&Args); return Args.Value;
    };
    const auto EndName=ToName(L"Beam_End"),NormalName=ToName(L"SurfaceNormal"),
        StartTangent=ToName(L"BeamStartTangent"),EndTangent=ToName(L"BeamEndTangent");
    for(size_t I=0;I<R.Segments.size();++I) {
        auto* C=R.Segments[I].Get(); const bool Created=!C;
        if(!C) {
            struct SpawnArgs {
                SDK::UObject* WorldContextObject=nullptr; SDK::UNiagaraSystem* SystemTemplate=nullptr;
                SDK::FVector Location{}; SDK::FRotator Rotation{}; SDK::FVector Scale{};
                bool bAutoDestroy=false,bAutoActivate=false; uint8_t PoolingMethod=0;
                bool bPreCullCheck=false,bDontSpawnOutOfView=false;
                float DontSpawnOutOfViewCheckRadius=0; bool bAllowSpawnLoopingFX=false;
                SDK::UObject* ReturnValue=nullptr;
            } Args;
            static_assert(sizeof(SpawnArgs)==112 && offsetof(SpawnArgs,ReturnValue)==104 && offsetof(SpawnArgs,bAllowSpawnLoopingFX)==100);
            Args.WorldContextObject=Context; Args.SystemTemplate=static_cast<SDK::UNiagaraSystem*>(Template);
            Args.Location=Points[I*2]; Args.Scale={1,1,1}; Args.bAutoDestroy=true;
            Args.bAutoActivate=false; Args.bAllowSpawnLoopingFX=true;
            Lib->ProcessEvent(Spawn,&Args); C=Args.ReturnValue;
            if(!IsUsableObject(C) || !C->IsA(ComponentClass)) { ClearDeinoNativeArc(); R.SpawnRetryAt=Now+2000; return 3; }
            R.Segments[I]=ExtensionObject(C);
        }
        struct MoveParams {
            SDK::FVector NewLocation{}; bool bSweep=false; uint8_t Pad[7]{};
            SDK::FHitResult SweepHitResult{}; bool bTeleport=false; uint8_t PadEnd[7]{};
        } MoveArgs;
        static_assert(sizeof(MoveParams)==304 && offsetof(MoveParams,bTeleport)==296);
        MoveArgs.NewLocation=Points[I*2]; MoveArgs.bTeleport=true; C->ProcessEvent(Move,&MoveArgs);
        auto Put=[&](SDK::FName N,const SDK::FVector& V) {
            struct { SDK::FName InVariableName{}; SDK::FVector InValue{}; } Args;
            static_assert(sizeof(Args)==32);
            Args.InVariableName=N; Args.InValue=V; C->ProcessEvent(Set,&Args);
        };
        Put(EndName,Points[I*2+2]); Put(NormalName,{0,0,1});
        const auto [ControlStart,ControlEnd]=PhoenixDeinoArc::Controls(Points[I*2],Points[I*2+2]);
        Put(StartTangent,ControlStart); Put(EndTangent,ControlEnd);
        if(Created) { bool Reset=true; C->ProcessEvent(Activate,&Reset); }
    }
    return 1;
}
