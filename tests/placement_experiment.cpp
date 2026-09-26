// Run the production dispatcher against engine-owned parameter buffers.
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
namespace SDK {
struct UClass; struct UFunction;
struct UObject { UClass* Class=nullptr; bool Live=true; int Serial=1; std::string Name; std::map<std::string,UObject*> Refs; std::string GetName() const {return Name;} };
struct FName {std::string Text;std::string ToString()const{return Text;}};
struct FFieldClass {FName Name;};
struct FProperty {FFieldClass* ClassPrivate=nullptr;int ArrayDim=1,Offset=0,ElementSize=0;};
struct FBoolProperty:FProperty {uint8_t ByteOffset=0,FieldMask=0,ByteMask=0;};
struct UStruct:UObject {int Size=0;std::map<std::string,FProperty*> Fields;};
struct FStructProperty:FProperty {UStruct* Struct=nullptr;};
struct UFunction:UStruct {};
struct UClass:UStruct {std::map<std::string,UFunction*> Functions;UFunction* GetFunction(const char*,const char* Key){return Functions[Key];}};
struct FString {explicit FString(const wchar_t*){}};
struct AShooterPlayerController:UObject {UObject* Pawn=nullptr;void RemoteServerCheat(FString){assert(false && "Placement must not send admin commands");}};
struct UWorld:UObject {};
}
using DWORD=unsigned long;
DWORD Foreground=1;
void* GetForegroundWindow(){return nullptr;}
void GetWindowThreadProcessId(void*,DWORD* P){*P=Foreground;}
DWORD GetCurrentProcessId(){return 1;}
namespace g_Config {bool bFloatingStructures=true;}
namespace g_MDX12::g_MenuState {bool g_isOpen=false;}
namespace PhoenixShortcuts {bool GameplayInputReady(SDK::AShooterPlayerController* P,bool Menu,bool){return P && !Menu;}}
struct LanguageManager {static LanguageManager& Get(){static LanguageManager L;return L;}std::string TranslateCopy(const std::string& S){return S;}};
namespace g_Hack {
SDK::AShooterPlayerController* LocalPC=nullptr;
bool IsUsableObject(SDK::UObject* O){return O && O->Live;}
struct ExtensionObject {SDK::UObject* O=nullptr;int Serial=0;ExtensionObject()=default;explicit ExtensionObject(SDK::UObject* V):O(V),Serial(V?V->Serial:0){}SDK::UObject* Get()const{return IsUsableObject(O)&&O->Serial==Serial?O:nullptr;}};
SDK::AShooterPlayerController* GetLocalShooterController(SDK::UWorld*){return LocalPC;}
SDK::FProperty* FindReflectedProperty(SDK::UStruct* S,const char* Key){return S?S->Fields[Key]:nullptr;}
SDK::UObject* ReadReflectedObjectValue(SDK::UObject* O,const char* Key){return O?O->Refs[Key]:nullptr;}
bool IsFreeCameraActive(){return false;}
bool ExpansionHasClass(SDK::UObject*,const char*){return false;}
std::mutex ExtensionStatusMutex;
void ExtensionStatus(std::string& D,const std::string& V){D=V;}
template<class... T> std::string FormatLocalized(const char* S,T...){return S;}
using NativeGameplayEvent=void(*)(SDK::UObject*,SDK::UFunction*,void*);
#include "../src/internal/Hack/PlacementExperiment.inl"
}
int Calls=0;
void Engine(SDK::UObject*,SDK::UFunction*,void* P){
    auto* B=static_cast<uint8_t*>(P);assert(B[11]&4);B[11]|=2;++Calls;
}
int main(){
    using namespace SDK;using namespace g_Hack;
    FFieldClass StructType{{"StructProperty"}},BoolType{{"BoolProperty"}};
    UStruct Placement;Placement.Name="PlacementData";Placement.Size=16;
    FBoolProperty Flag;Flag.ClassPrivate=&BoolType;Flag.ElementSize=1;Flag.Offset=2;Flag.ByteOffset=1;Flag.FieldMask=4;
    Placement.Fields["bIgnoreGroundRequirement"]=&Flag;
    FStructProperty Param;Param.ClassPrivate=&StructType;Param.Offset=8;Param.ElementSize=16;Param.Struct=&Placement;
    UFunction Request,Check,Unrelated;Request.Size=Check.Size=32;
    Request.Fields["PlacementData"]=&Param;Check.Fields["OutPlacementData"]=&Param;
    UClass PCClass,PreviewClass;PCClass.Functions["ServerRequestPlaceStructure"]=&Request;PreviewClass.Functions["IsAllowedToBuild"]=&Check;
    AShooterPlayerController PC;PC.Class=&PCClass;LocalPC=&PC;
    UObject Placer,Preview,Other;Preview.Class=&PreviewClass;PC.Refs["StructurePlacer"]=&Placer;Placer.Refs["CurrentPlacingStructure"]=&Preview;
    UWorld World;PlacementExperimentTick(&World);
    std::array<uint8_t,32> Buffer{};Buffer.fill(0xA0);
    assert(!HandlePlacementExperiment(&Other,&Request,Buffer.data(),Engine));
    assert(!HandlePlacementExperiment(&PC,&Unrelated,Buffer.data(),Engine));
    assert(HandlePlacementExperiment(&PC,&Request,Buffer.data(),Engine));
    assert(Calls==1 && Buffer[11]==0xA2); // preserve native output; restore only owned bit
    for(size_t I=0;I<Buffer.size();++I)if(I!=11)assert(Buffer[I]==0xA0);
    Buffer[11]=0xA4;assert(HandlePlacementExperiment(&Preview,&Check,Buffer.data(),Engine));assert(Buffer[11]==0xA6);
    ++PC.Serial;assert(!HandlePlacementExperiment(&PC,&Request,Buffer.data(),Engine));
    PlacementExperimentTick(&World);g_MDX12::g_MenuState::g_isOpen=true;PlacementExperimentTick(&World);
    assert(!HandlePlacementExperiment(&PC,&Request,Buffer.data(),Engine));g_MDX12::g_MenuState::g_isOpen=false;
    Flag.Offset=16;assert(FindPlacementFlag(&Request,"PlacementData").Offset<0);Flag.Offset=2;
    Param.Offset=20;assert(FindPlacementFlag(&Request,"PlacementData").Offset<0);Param.Offset=8;
    Param.ArrayDim=2;assert(FindPlacementFlag(&Request,"PlacementData").Offset<0);Param.ArrayDim=1;
    Placement.Name="OtherPlacementData";assert(FindPlacementFlag(&Request,"PlacementData").Offset<0);Placement.Name="PlacementData";
    PlacementExperimentTick(&World);ResetPlacementExperiment();assert(!HandlePlacementExperiment(&PC,&Request,Buffer.data(),Engine));
    assert(Calls==2);
    std::cout<<"Placement dispatcher: owner isolation, buffer bounds, masked restoration, GC reuse and pause/reset passed.\n";
}
