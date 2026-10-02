// Execute the production lookup against the SDK's path-stripping name semantics.
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <cmath>
#include "../src/internal/Hack/DeinoArcGeometry.h"
namespace SDK {
    enum class EObjectFlags : uint32_t { None=0,BeginDestroyed=1,FinishDestroyed=2,MirroredGarbage=4 };
    bool operator&(EObjectFlags A,EObjectFlags B) { return (uint32_t(A)&uint32_t(B))!=0; }
    struct FName {
        std::string Raw;
        std::string GetRawString() const { return Raw; }
        std::string ToString() const { return Raw.substr(Raw.find_last_of('/')+1); }
    };
    struct UClass;
    struct UObject {
        UClass* Class=nullptr; UObject* Outer=nullptr; FName Name;
        EObjectFlags Flags=EObjectFlags::None; int32_t Index=1;
        bool Live=true,Default=false;
        bool IsDefaultObject() const { return Default; }
        std::string GetName() const { return Name.ToString(); }
    };
    struct UFunction : UObject { int Size=112; uint32_t FunctionFlags=0x400; };
    struct UClass : UObject {
        UObject* ClassDefaultObject=nullptr;
        std::map<std::string,UFunction*> Functions;
        UFunction* GetFunction(const char*,const char* N) { auto I=Functions.find(N); return I==Functions.end()?nullptr:I->second; }
    };
}
bool IsUsableObject(SDK::UObject* O) {
    return O && O->Class && O->Live && !O->Default && O->Flags==SDK::EObjectFlags::None;
}
bool TryGetLiveObjectSerial(SDK::UObject* O,int32_t Index,int32_t* Out) {
    if(!O || !O->Live || O->Index!=Index) return false;
    *Out=1; return true;
}
#include "../src/internal/Hack/DeinoNativeArcLookup.inl"
int main() {
    SDK::UClass Meta,Niagara,PackageType,WrongType;
    Meta.Class=&Meta; Niagara.Class=PackageType.Class=WrongType.Class=&Meta;
    Niagara.Name.Raw="NiagaraSystem"; PackageType.Name.Raw="Package"; WrongType.Name.Raw="NiagaraEmitter";
    SDK::UObject Package,Asset;
    Package.Class=&PackageType;
    Package.Name.Raw="/Game/Genesis/Effects/Particles/Weapons/TekGrenadeLauncher/NS_TekGrenadeLauncherProjArcBeam";
    Asset.Class=&Niagara; Asset.Outer=&Package; Asset.Name.Raw="NS_TekGrenadeLauncherProjArcBeam";
    assert(Package.GetName()==Asset.GetName()); // The previous full-path comparison cannot match.
    assert(IsDeinoNativeArcTemplate(&Asset));
    const auto CorrectPackage=Package.Name.Raw;
    Package.Name.Raw="/Game/Other/NS_TekGrenadeLauncherProjArcBeam";
    assert(!IsDeinoNativeArcTemplate(&Asset)); // Same leaf, wrong package.
    Package.Name.Raw=CorrectPackage;
    Asset.Name.Raw="NS_TekGrenadeLauncherProjectileArcBeamSegment";
    assert(!IsDeinoNativeArcTemplate(&Asset)); // Legacy segment has a different parameter contract.
    Asset.Name.Raw="NS_TekGrenadeLauncherProjArcBeam";
    Asset.Class=&WrongType; assert(!IsDeinoNativeArcTemplate(&Asset)); Asset.Class=&Niagara;
    Asset.Default=true; assert(!IsDeinoNativeArcTemplate(&Asset)); Asset.Default=false;
    Package.Outer=&Meta; assert(!IsDeinoNativeArcTemplate(&Asset)); Package.Outer=nullptr;
    Asset.Flags=SDK::EObjectFlags::BeginDestroyed; assert(!IsDeinoNativeArcTemplate(&Asset));
    assert(!IsDeinoNativeArcTemplate(nullptr));
    SDK::UClass Library; Library.Class=&Meta;
    SDK::UObject CDO; CDO.Class=&Library; CDO.Default=true; Library.ClassDefaultObject=&CDO;
    SDK::UFunction Spawn; Spawn.Class=&Meta; Library.Functions["SpawnSystemAtLocation"]=&Spawn;
    auto Resolve=[&] { return DeinoArcClassFunction(&Library,"NiagaraFunctionLibrary","SpawnSystemAtLocation",112); };
    assert(!IsUsableObject(&CDO)); // General mount checks must still reject CDOs.
    assert(Resolve()==&Spawn);
    struct Vec { double X,Y,Z; };
    const Vec Start{-287686,249841,-8270},End{-286786,250141,-8210};
    const auto [A,B]=PhoenixDeinoArc::Controls(Start,End);
    assert(std::abs(A.X-(Start.X+300))<1e-8 && std::abs(B.Y-(Start.Y+200))<1e-8);
    for(double T : {0.0,.1,.5,.9,1.0}) {
        const double U=1-T;
        const double CurveX=U*U*U*Start.X+3*U*U*T*A.X+3*U*T*T*B.X+T*T*T*End.X;
        assert(std::abs(CurveX-(Start.X+(End.X-Start.X)*T))<1e-7);
    }
    const auto [SameA,SameB]=PhoenixDeinoArc::Controls(Start,Start);
    assert(SameA.X==Start.X && SameB.Z==Start.Z);
    Spawn.FunctionFlags|=0x40; assert(!Resolve()); Spawn.FunctionFlags=0x400; // Never an RPC.
    Spawn.Size=104; assert(!Resolve()); Spawn.Size=112;
    CDO.Live=false; assert(!Resolve()); CDO.Live=true;
    CDO.Flags=SDK::EObjectFlags::FinishDestroyed; assert(!Resolve()); CDO.Flags=SDK::EObjectFlags::None;
    CDO.Class=&WrongType; assert(!Resolve()); CDO.Class=&Library;
    CDO.Default=false; assert(!Resolve()); CDO.Default=true;
    assert(Resolve()==&Spawn);
    std::cout<<"Native arc: raw package identity, CDO library lookup, lifecycle and local-function guards passed.\n";
}
