// Execute the production loader with a cold asset registry and a reflected API.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>
namespace SDK {
struct FName {
    uint32_t Index=0,Number=0;
    static inline std::vector<std::string> Names={"None"};
    std::string GetRawString() const { return Names.at(Index); }
};
struct FString {
    const wchar_t* Data=nullptr; int32_t Num=0,Max=0;
    FString()=default;
    explicit FString(const wchar_t* S):Data(S),Num(int32_t(wcslen(S)+1)),Max(Num) {}
};
struct UFunction { const char* Name; };
struct UObject { void ProcessEvent(UFunction*,void*); };
struct UClass { UObject* ClassDefaultObject; };
struct UKismetSystemLibrary { static UClass* StaticClass(); };
struct UKismetStringLibrary { static UClass* StaticClass(); };
template<class T> struct TSoftObjectPtr {
    struct { int32_t Index=0,Serial=0; } WeakPtr;
    struct { struct { FName PackageName,AssetName; } AssetPath; FString SubPathString; } ObjectID;
};
}
SDK::UObject SystemCDO,NameCDO,Beam,Wrong;
SDK::UClass SystemClass{&SystemCDO},NameClass{&NameCDO};
SDK::UClass* SDK::UKismetSystemLibrary::StaticClass() { return &SystemClass; }
SDK::UClass* SDK::UKismetStringLibrary::StaticClass() { return &NameClass; }
SDK::UFunction Load{"LoadAsset_Blocking"},Name{"Conv_StringToName"};
int NameCalls=0,LoadCalls=0;
bool Resident=false,LoadFails=false,WrongAsset=false,BadNames=false,BadContract=false;
SDK::UFunction* DeinoArcClassFunction(SDK::UClass* C,const char* Owner,const char* Fn,int Size) {
    if(C==&SystemClass && std::string(Owner)=="KismetSystemLibrary" && std::string(Fn)==Load.Name && Size==48) return &Load;
    if(C==&NameClass && std::string(Owner)=="KismetStringLibrary" && std::string(Fn)==Name.Name && Size==24) return &Name;
    return nullptr;
}
bool NativeMountParam(SDK::UFunction* F,const char* Param,const char* Type,int Offset,int Size) {
    if(BadContract) return false;
    const std::string P=Param,T=Type;
    if(F==&Load) return (P=="Asset" && T=="SoftObjectProperty" && Offset==0 && Size==40) ||
        (P=="ReturnValue" && T=="ObjectProperty" && Offset==40 && Size==8);
    if(F==&Name) return (P=="InString" && T=="StrProperty" && Offset==0 && Size==16) ||
        (P=="ReturnValue" && T=="NameProperty" && Offset==16 && Size==8);
    return false;
}
bool IsDeinoNativeArcTemplate(SDK::UObject* O) { return O==&Beam; }
void SDK::UObject::ProcessEvent(SDK::UFunction* F,void* Params) {
    if(F==&Name) {
        assert(this==&NameCDO);
        ++NameCalls;
        const auto& Text=*static_cast<SDK::FString*>(Params);
        assert(Text.Data && Text.Num==int32_t(wcslen(Text.Data)+1));
        std::wstring W(Text.Data);
        std::string TextAscii;
        for(wchar_t C:W) { assert(C>=0 && C<128); TextAscii.push_back(static_cast<char>(C)); }
        FName::Names.push_back(TextAscii);
        auto* Out=reinterpret_cast<FName*>(static_cast<char*>(Params)+16);
        Out->Index=BadNames?0:uint32_t(FName::Names.size()-1);
    } else {
        assert(F==&Load && this==&SystemCDO);
        ++LoadCalls;
        const auto& Soft=*static_cast<SDK::TSoftObjectPtr<SDK::UObject>*>(Params);
        assert(Soft.WeakPtr.Serial==0);
        assert(Soft.ObjectID.AssetPath.PackageName.GetRawString()=="/Game/Genesis/Effects/Particles/Weapons/TekGrenadeLauncher/NS_TekGrenadeLauncherProjArcBeam");
        assert(Soft.ObjectID.AssetPath.AssetName.GetRawString()=="NS_TekGrenadeLauncherProjArcBeam");
        assert(!Soft.ObjectID.SubPathString.Data && Soft.ObjectID.SubPathString.Num==0);
        // Only this API populates a cold registry; there is no equipped weapon.
        if(!LoadFails) Resident=true;
        auto** Out=reinterpret_cast<SDK::UObject**>(static_cast<char*>(Params)+40);
        *Out=LoadFails?nullptr:WrongAsset?&Wrong:Resident?&Beam:nullptr;
    }
}
#include "../src/internal/Hack/DeinoNativeArcAssetLoad.inl"
int main() {
    SDK::UObject* Result=&Wrong;
    assert(!Resident);
    assert(LoadDeinoNativeArcAsset(Result)==1 && Result==&Beam && Resident);
    assert(NameCalls==2 && LoadCalls==1);
    assert(LoadDeinoNativeArcAsset(Result)==1 && Result==&Beam); // Warm registry.
    LoadFails=true; Result=&Beam;
    assert(LoadDeinoNativeArcAsset(Result)==2 && !Result); LoadFails=false;
    WrongAsset=true;
    assert(LoadDeinoNativeArcAsset(Result)==2 && !Result); WrongAsset=false;
    const auto Before=LoadCalls;
    BadNames=true;
    assert(LoadDeinoNativeArcAsset(Result)==3 && !Result && LoadCalls==Before); BadNames=false;
    const auto NamesBefore=NameCalls;
    BadContract=true;
    assert(LoadDeinoNativeArcAsset(Result)==3 && !Result && LoadCalls==Before && NameCalls==NamesBefore);
    std::cout<<"Native arc loader: cold/warm registry, exact soft path, null/wrong asset and contract failures passed.\n";
}
