// Execute production override code against a small reflection/GC fixture.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <string>
namespace SDK {
    struct FName { std::string Value; std::string ToString() const { return Value; } };
    struct FFieldClass { FName Name; };
    struct FProperty { FFieldClass* ClassPrivate = nullptr; int Offset = 0, ElementSize = 4, ArrayDim = 1; };
    struct FBoolProperty : FProperty { uint8_t ByteOffset = 0, ByteMask = 0, FieldMask = 0; };
    struct UClass { int Size = 0; std::map<std::string,FProperty*> Fields; };
    struct FStructProperty : FProperty { UClass* Struct = nullptr; };
    struct UObject { UClass* Class = nullptr; bool Live = true; int Serial = 1; };
}
struct TestObject : SDK::UObject { float Value = 10; uint8_t Bits = 2; double Timer = 2.000000000123; double ChargeRange[2] = {.25,1.25}; };
bool IsUsableObject(SDK::UObject* O) { return O && O->Live; }
struct ExtensionObject {
    SDK::UObject* Pointer = nullptr; int Serial = 0;
    ExtensionObject() = default;
    explicit ExtensionObject(SDK::UObject* O) : Pointer(O), Serial(O ? O->Serial : 0) {}
    SDK::UObject* Get() const { return IsUsableObject(Pointer) && Pointer->Serial == Serial ? Pointer : nullptr; }
};
SDK::FProperty* FindReflectedProperty(SDK::UClass* C,const char* Name) {
    const auto I = C->Fields.find(Name); return I == C->Fields.end() ? nullptr : I->second;
}
#include "../src/internal/Hack/FeatureExpansionPatch.inl"
int main() {
    SDK::FFieldClass Float{{"FloatProperty"}}, Double{{"DoubleProperty"}}, Bool{{"BoolProperty"}}, Wrong{{"IntProperty"}};
    TestObject A,B; SDK::UClass Class; Class.Size = sizeof(TestObject); A.Class = B.Class = &Class;
    SDK::FProperty Value; Value.ClassPrivate=&Float;
    Value.Offset=int(reinterpret_cast<uint8_t*>(&A.Value)-reinterpret_cast<uint8_t*>(&A));
    SDK::FBoolProperty Bits; Bits.ClassPrivate=&Bool; Bits.ElementSize=1; Bits.FieldMask=Bits.ByteMask=8;
    Bits.Offset=int(reinterpret_cast<uint8_t*>(&A.Bits)-reinterpret_cast<uint8_t*>(&A));
    Class.Fields={{"Value",&Value},{"Bits",&Bits}};
    ExpansionPatch P;
    P.Set(&A,"Value",2,true); assert(A.Value==20);
    P.Set(&A,"Value",2,true); assert(A.Value==20); // no compounded multiplier
    A.Value=7; P.Set(&A,"Value",2,true); assert(A.Value==14);
    P.Restore(); assert(A.Value==7); // native updates become the restoration baseline
    P.Set(&A,"Value",0); A.Value=33; P.Restore(); assert(A.Value==33); // external owner wins
    P.Set(&A,"Value",5); P.Set(&B,"Value",9); assert(A.Value==33 && B.Value==9);
    P.Restore(); assert(B.Value==10); // weapon/mount change restores the previous object
    P.Set(&A,"Bits",1); assert(A.Bits==10); A.Bits|=4; P.Restore(); assert(A.Bits==6);
    P.Set(&A,"Value",2); A.Serial++; A.Value=88; P.Restore(); assert(A.Value==88); // GC address reuse
    P.Set(&A,"Value",2); A.Live=false; P.Restore(); assert(A.Value==2); A.Live=true;
    Value.ClassPrivate=&Wrong; A.Value=10; P.Set(&A,"Value",0); assert(A.Value==10 && P.Offset<0); P.Restore();
    Value.ClassPrivate=&Float; Value.ArrayDim=2; P.Set(&A,"Value",0); assert(A.Value==10 && P.Offset<0); P.Restore();
    Value.ArrayDim=1; Class.Size=Value.Offset+1; P.Set(&A,"Value",0); assert(A.Value==10 && P.Offset<0); P.Restore();
    Class.Size=sizeof(TestObject); A.Value=std::numeric_limits<float>::max();
    P.Set(&A,"Value",5,true); assert(A.Value==std::numeric_limits<float>::max());
    A.Value=NAN; P.Set(&A,"Value",0); assert(std::isnan(A.Value));
    P.Restore(); P.Set(nullptr,"Value",0);
    SDK::FProperty Timer; Timer.ClassPrivate=&Double; Timer.ElementSize=sizeof(double);
    Timer.Offset=int(reinterpret_cast<uint8_t*>(&A.Timer)-reinterpret_cast<uint8_t*>(&A)); Class.Fields["Timer"]=&Timer;
    const double Original=A.Timer; P.Set(&A,"Timer",0); assert(A.Timer==0); P.Restore(); assert(A.Timer==Original);
    // Double-valued jump speed fields: repeat ticks and slider changes must
    // scale the captured original, then restore it without precision loss.
    P.Set(&A,"Timer",2,true); P.Set(&A,"Timer",2,true); assert(A.Timer==Original*2);
    P.Set(&A,"Timer",3,true); assert(A.Timer==Original*3);
    P.Restore(); assert(A.Timer==Original);
    P.Set(&A,"Timer",0); A.Timer=1.234567890123; P.Restore(); assert(A.Timer==1.234567890123);
    Class.Size=Timer.Offset+4; P.Set(&A,"Timer",0); assert(P.Offset<0 && A.Timer==1.234567890123); P.Restore();
    Class.Size=sizeof(TestObject);
    SDK::FFieldClass StructType{{"StructProperty"}};
    SDK::UClass Range; Range.Size=16;
    SDK::FProperty X,Y; X.ClassPrivate=Y.ClassPrivate=&Double;
    X.ElementSize=Y.ElementSize=8; X.Offset=0; Y.Offset=8;
    Range.Fields={{"X",&X},{"Y",&Y}};
    SDK::FStructProperty Charge; Charge.ClassPrivate=&StructType; Charge.Struct=&Range;
    Charge.ElementSize=16; Charge.Offset=int(reinterpret_cast<uint8_t*>(&A.ChargeRange)-reinterpret_cast<uint8_t*>(&A));
    Class.Fields["Charge"]=&Charge;
    ExpansionPatch PX,PY;
    PX.Set(&A,"Charge",.2,true,"X"); PY.Set(&A,"Charge",.2,true,"Y");
    PX.Set(&A,"Charge",.2,true,"X"); PY.Set(&A,"Charge",.2,true,"Y");
    assert(A.ChargeRange[0]==.25*.2 && A.ChargeRange[1]==1.25*.2);
    PX.Set(&A,"Charge",.5,true,"X"); assert(A.ChargeRange[0]==.125);
    PX.Restore(); assert(A.ChargeRange[0]==.25 && A.ChargeRange[1]==.25);
    PY.Set(&B,"Charge",.2,true,"Y"); assert(A.ChargeRange[1]==1.25 && B.ChargeRange[1]==.25);
    PY.Restore(); assert(B.ChargeRange[1]==1.25);
    Y.Offset=12; PY.Set(&A,"Charge",.2,true,"Y"); assert(PY.Offset<0 && A.ChargeRange[1]==1.25); PY.Restore(); Y.Offset=8;
    Range.Size=8; PX.Set(&A,"Charge",.2,true,"X"); assert(PX.Offset<0); PX.Restore(); Range.Size=16;
    PX.Set(&A,"Charge",.2,true,"missing"); assert(PX.Offset<0); PX.Restore();
    Charge.Offset=Class.Size-8; PX.Set(&A,"Charge",.2,true,"X"); assert(PX.Offset<0); PX.Restore();
    std::cout << "Production overrides: restore, ownership, bit masks, GC reuse, reflection bounds and invalid numbers passed.\n";
}
