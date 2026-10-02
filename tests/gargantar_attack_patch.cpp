#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <iostream>
namespace SDK {
struct FName { std::string Value; std::string ToString() const { return Value; } };
struct FFieldClass { FName Name{"FloatProperty"}; };
struct FProperty { FFieldClass* ClassPrivate=nullptr; int ArrayDim=1,ElementSize=4,Offset=8; };
struct UStruct { int Size=304; std::string GetName() const { return "DinoAttackInfo"; } };
struct UObject { bool Live=true; int Serial=1, Num=9,Stride=304; uint8_t* Data=nullptr; UStruct* Type=nullptr; };
}
struct ExtensionObject {
    SDK::UObject* Pointer=nullptr; int Serial=0;
    ExtensionObject()=default;
    explicit ExtensionObject(SDK::UObject* O):Pointer(O),Serial(O?O->Serial:0){}
    SDK::UObject* Get() const { return Pointer && Pointer->Live && Pointer->Serial==Serial?Pointer:nullptr; }
};
struct ReflectedStructArrayView { uint8_t* Data=nullptr; int Num=0, ElementSize=0; SDK::UStruct* ElementStruct=nullptr; };
SDK::FFieldClass Float;
SDK::FProperty Interval{&Float};
SDK::FProperty* FindReflectedProperty(SDK::UStruct*,const char*) { return &Interval; }
bool GetReflectedStructArray(SDK::UObject* O,const char*,int,ReflectedStructArrayView* V) {
    if(!O || !O->Live || !O->Data) return false;
    *V={O->Data,O->Num,O->Stride,O->Type}; return true;
}
#include "../src/internal/Hack/GargantarAttackPatch.inl"
int main() {
    alignas(float) std::array<uint8_t,9*304> A{},B{};
    A.fill(0xa5); B.fill(0xa5);
    auto Value=[](auto& Data,int I)->float& { return *reinterpret_cast<float*>(Data.data()+304*I+8); };
    for(int I=0;I<9;++I) Value(A,I)=Value(B,I)=float(I);
    SDK::UStruct Type; SDK::UObject O; O.Data=A.data(); O.Type=&Type;
    GargantarAttackOverrides Patch;
    assert(Patch.Set(&O,.5f)); assert(Patch.Set(&O,.5f));
    for(int I=0;I<9;++I) assert(Value(A,I)==I*.5f);
    assert(A[0]==0xa5 && A[12]==0xa5 && A[303]==0xa5);
    Value(A,2)=8; assert(Patch.Set(&O,.5f)); assert(Value(A,2)==4);
    Patch.Restore(); assert(Value(A,2)==8 && Value(A,3)==3);
    assert(Patch.Set(&O,.5f)); Value(A,3)=7; Patch.Restore(); assert(Value(A,3)==7);
    assert(Patch.Set(&O,.5f)); O.Data=B.data(); Patch.Restore();
    assert(Value(A,4)==2 && Value(B,4)==4); // relocated storage is never dereferenced to restore
    assert(Patch.Set(&O,.5f)); ++O.Serial; Patch.Restore(); assert(Value(B,4)==2);
    O.Num=8; assert(!Patch.Set(&O,.5f)); O.Num=9;
    O.Stride=300; assert(!Patch.Set(&O,.5f)); O.Stride=304;
    Interval.Offset=303; assert(!Patch.Set(&O,.5f)); Interval.Offset=8;
    assert(!Patch.Set(&O,std::numeric_limits<float>::quiet_NaN()));
    O.Live=false; Patch.Restore();
    std::cout<<"Gargantar production attack patch: non-compounding, game refresh, guarded restore, relocated arrays, GC identity and layout refusal passed.\n";
}
