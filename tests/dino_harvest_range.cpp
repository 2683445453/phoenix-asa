// Execute production reflection and radius ownership against the real SDK layout.
// Class/name lookup and object registry are fixtures; no game process is used.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "../src/external/CppSDK/SDK.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

struct LanguageManager {
    static LanguageManager& Get(){static LanguageManager M;return M;}
    std::string TranslateCopy(const std::string& S){return S;}
};
namespace g_Hack {
#include "../src/internal/Hack/WeaponWaterAccess.inl"
#include "../src/internal/Hack/DinoHarvestRange.inl"
}
namespace {
    SDK::UClass Meta{},ObjectClass{},DinoClass{},RexClass{},StriderClass{},ScriptClass{};
    SDK::UScriptStruct AttackStruct{};
    SDK::TUObjectArray Objects{};
    SDK::FUObjectItem Entries[64]{};
    SDK::FUObjectItem* Chunks[]{Entries};
    std::vector<std::wstring> Names{L"None"};
    SDK::FName Name(const wchar_t* Text){
        for(size_t I=0;I<Names.size();++I)if(Names[I]==Text)return SDK::FName(int32_t(I));
        Names.emplace_back(Text);return SDK::FName(int32_t(Names.size()-1));
    }
    const void* NameEntry(uint32_t I){assert(I<Names.size());return Names[I].c_str();}
    void AppendName(const void* Entry,SDK::FString& Out){Out=SDK::FString(static_cast<const wchar_t*>(Entry));}
    void Serial(int32_t I,int32_t V){std::memcpy(reinterpret_cast<uint8_t*>(&Entries[I])+0x10,&V,sizeof(V));}
    void Register(SDK::UObject& Object,SDK::UClass* Class){
        Object.Index=Objects.NumElements++;Object.Class=Class;Entries[Object.Index].Object=&Object;Serial(Object.Index,100+Object.Index);
    }
    void Class(SDK::UClass& Type,const wchar_t* Text,SDK::UClass* Base,int32_t Size){
        Type={};Register(Type,&Meta);Type.Name=Name(Text);Type.SuperStruct=Base;Type.Size=Size;
    }
    void Property(SDK::FProperty& P,SDK::FFieldClass& Type,const wchar_t* Text,int32_t Offset,int32_t Size){
        P.Name=Name(Text);P.ClassPrivate=&Type;P.ArrayDim=1;P.ElementSize=Size;P.Offset=Offset;
    }
    template<class T> void Array(SDK::TArray<T>& Out,T* Data,int32_t Count,int32_t Capacity=-1){
        struct Header{T* Data;int32_t Num,Max;} H{Data,Count,Capacity<0?Count:Capacity};
        static_assert(sizeof(H)==sizeof(Out));std::memcpy(&Out,&H,sizeof(H));
    }
    struct Fixture {
        SDK::APrimalDinoCharacter Dino{},Other{};
        std::array<SDK::FDinoAttackInfo,3> Attacks{},OtherAttacks{};
        std::array<SDK::FName,3> Sockets{};
        SDK::FFieldClass ArrayType{},StructType{},FloatType{},NameType{},IntType{},ClassType{},BoolType{};
        SDK::FArrayProperty AttackArray{},SocketArray{};
        SDK::FStructProperty AttackElement{};
        SDK::FProperty Radius{},AttackName{},Damage{},Projectile{},SocketElement{};
        SDK::FBoolProperty Basic{};
        Fixture(){
            Objects={};std::memset(Entries,0,sizeof(Entries));
            Objects.Objects=Chunks;Objects.NumElements=1;Objects.MaxElements=64;Objects.NumChunks=Objects.MaxChunks=1;
            SDK::UObject::GObjects.InitManually(&Objects);
            SDK::FName::GetNameEntryFromName=reinterpret_cast<void*>(&NameEntry);
            SDK::FName::AppendString=reinterpret_cast<void*>(&AppendName);
            Class(Meta,L"Class",nullptr,sizeof(SDK::UClass));Class(ObjectClass,L"Object",nullptr,sizeof(SDK::UObject));
            Class(ScriptClass,L"ScriptStruct",&ObjectClass,sizeof(SDK::UScriptStruct));
            Class(DinoClass,L"PrimalDinoCharacter",&ObjectClass,sizeof(SDK::APrimalDinoCharacter));
            Class(RexClass,L"Rex_Character_BP_C",&DinoClass,sizeof(SDK::APrimalDinoCharacter));
            Class(StriderClass,L"TekStriderCharacterBP_C",&DinoClass,sizeof(SDK::APrimalDinoCharacter));
            AttackStruct={};Register(AttackStruct,&ScriptClass);AttackStruct.Name=Name(L"DinoAttackInfo");AttackStruct.Size=sizeof(SDK::FDinoAttackInfo);
            Register(Dino,&RexClass);Register(Other,&RexClass);
            ArrayType.Name=Name(L"ArrayProperty");StructType.Name=Name(L"StructProperty");FloatType.Name=Name(L"FloatProperty");
            NameType.Name=Name(L"NameProperty");IntType.Name=Name(L"IntProperty");ClassType.Name=Name(L"ClassProperty");BoolType.Name=Name(L"BoolProperty");
            Property(AttackArray,ArrayType,L"AttackInfos",offsetof(SDK::APrimalDinoCharacter,AttackInfos),sizeof(Dino.AttackInfos));
            Property(AttackElement,StructType,L"AttackInfos",0,sizeof(SDK::FDinoAttackInfo));AttackElement.Struct=&AttackStruct;
            AttackArray.InnerProperty=&AttackElement;DinoClass.ChildProperties=&AttackArray;
            Property(Radius,FloatType,L"MeleeSwingRadius",offsetof(SDK::FDinoAttackInfo,MeleeSwingRadius),sizeof(float));
            Property(AttackName,NameType,L"AttackName",offsetof(SDK::FDinoAttackInfo,AttackName),sizeof(SDK::FName));
            Property(Damage,IntType,L"MeleeDamageAmount",offsetof(SDK::FDinoAttackInfo,MeleeDamageAmount),sizeof(int32_t));
            Property(Projectile,ClassType,L"ProjectileClass",offsetof(SDK::FDinoAttackInfo,ProjectileClass),sizeof(void*));
            Property(SocketArray,ArrayType,L"MeleeSwingSockets",offsetof(SDK::FDinoAttackInfo,MeleeSwingSockets),sizeof(SDK::TArray<SDK::FName>));
            Property(SocketElement,NameType,L"MeleeSwingSockets",0,sizeof(SDK::FName));SocketArray.InnerProperty=&SocketElement;
            Property(Basic,BoolType,L"bBasicAttack",0x128,1);Basic.FieldSize=1;Basic.ByteOffset=0;Basic.FieldMask=Basic.ByteMask=4;
            AttackStruct.ChildProperties=&Radius;Radius.Next=&AttackName;AttackName.Next=&Damage;Damage.Next=&Projectile;
            Projectile.Next=&SocketArray;SocketArray.Next=&Basic;
            Sockets={Name(L"jaw"),Name(L"claw"),Name(L"tail")};
            for(size_t I=0;I<Attacks.size();++I){
                auto& A=Attacks[I];A.AttackName=Sockets[I];A.MeleeDamageAmount=40;A.bBasicAttack=true;
                A.MeleeSwingRadius=100.f*float(I+1);A.AttackRange=777;A.AttackInterval=.8f;
                Array(A.MeleeSwingSockets,&Sockets[I],1);
            }
            OtherAttacks=Attacks;Array(Dino.AttackInfos,Attacks.data(),int32_t(Attacks.size()));
            Array(Other.AttackInfos,OtherAttacks.data(),int32_t(OtherAttacks.size()));
        }
        ~Fixture(){g_Hack::ResetDinoHarvestRange();}
        void Apply(float Scale=2,uint64_t Now=1000){g_Hack::UpdateDinoHarvestRange(&Dino,true,Scale,Now);}
    };
}
namespace SDK {
    uintptr_t InSDKUtils::GetImageBase(){assert(false);return 0;}
    UClass* BasicFilesImplUtils::FindClassByName(const std::string& Name,bool){
        if(Name=="Class")return &Meta;
        if(Name=="PrimalDinoCharacter")return &DinoClass;
        if(Name=="ScriptStruct")return &ScriptClass;
        assert(false);return nullptr;
    }
}
int main(){
    using namespace g_Hack;
    {
        Fixture F;float Reach=0;
        assert(GetDinoHarvestReachMeters(&F.Dino,Reach)&&Reach==1);
        F.Apply();assert(F.Attacks[0].MeleeSwingRadius==200&&F.Attacks[2].MeleeSwingRadius==600);
        assert(F.Attacks[0].AttackRange==777&&F.Attacks[0].AttackInterval==.8f);
        assert(GetDinoHarvestReachMeters(&F.Dino,Reach)&&Reach==2);
        F.Apply(3,1010);assert(F.Attacks[0].MeleeSwingRadius==300);
        F.Attacks[0].MeleeSwingRadius=175;F.Apply(3,1300);assert(F.Attacks[0].MeleeSwingRadius==525);
        ResetDinoHarvestRange();assert(F.Attacks[0].MeleeSwingRadius==175&&F.Attacks[2].MeleeSwingRadius==300);
    }
    {
        Fixture F;F.Attacks[0].MeleeSwingRadius=800;F.Attacks[1].MeleeSwingRadius=2500;
        F.Apply(100);assert(F.Attacks[0].MeleeSwingRadius==2000&&F.Attacks[1].MeleeSwingRadius==2500);
        ResetDinoHarvestRange();assert(F.Attacks[0].MeleeSwingRadius==800);
        F.Apply(-3,2000);assert(F.Attacks[0].MeleeSwingRadius==800);
        ResetDinoHarvestRange();F.Apply(std::numeric_limits<float>::quiet_NaN(),3000);assert(F.Attacks[0].MeleeSwingRadius==1600);
    }
    {
        Fixture F;F.Attacks[1].bBasicAttack=false;
        SDK::UClass* Projectile=&ObjectClass;std::memcpy(&F.Attacks[2].ProjectileClass,&Projectile,sizeof(Projectile));
        F.Apply();assert(F.Attacks[0].MeleeSwingRadius==200&&F.Attacks[1].MeleeSwingRadius==200&&F.Attacks[2].MeleeSwingRadius==300);
        UpdateDinoHarvestRange(&F.Other,true,2,1001);assert(F.Attacks[0].MeleeSwingRadius==100&&F.OtherAttacks[0].MeleeSwingRadius==200);
        UpdateDinoHarvestRange(&F.Other,false,2,1002);assert(F.OtherAttacks[0].MeleeSwingRadius==100);
        F.Dino.Class=&StriderClass;F.Apply(2,2000);assert(F.Attacks[0].MeleeSwingRadius==100);
    }
    {
        Fixture F;F.Apply();auto Relocated=F.Attacks;std::swap(Relocated[0],Relocated[2]);
        Array(F.Dino.AttackInfos,Relocated.data(),int32_t(Relocated.size()));
        ResetDinoHarvestRange();assert(Relocated[0].MeleeSwingRadius==300&&Relocated[2].MeleeSwingRadius==100);
        assert(F.Attacks[0].MeleeSwingRadius==200); // never restore through an old allocation
        Array(F.Dino.AttackInfos,F.Attacks.data(),int32_t(F.Attacks.size()));
    }
    {
        Fixture F;F.Attacks[1].AttackName=F.Attacks[0].AttackName;
        Array(F.Attacks[1].MeleeSwingSockets,&F.Sockets[0],1);
        Array(F.Dino.AttackInfos,F.Attacks.data(),2);F.Apply();
        assert(F.Attacks[0].MeleeSwingRadius==100&&F.Attacks[1].MeleeSwingRadius==200);
        float Reach=0;assert(!GetDinoHarvestReachMeters(&F.Dino,Reach));
    }
    {
        Fixture F;F.Apply();Serial(F.Dino.Index,999);ResetDinoHarvestRange();assert(F.Attacks[0].MeleeSwingRadius==200);
        Serial(F.Dino.Index,100+F.Dino.Index);F.Attacks[0].MeleeSwingRadius=100;
        F.Apply(2,2000);F.Attacks[0].MeleeSwingRadius=70;ResetDinoHarvestRange();assert(F.Attacks[0].MeleeSwingRadius==70);
    }
    for(int EligibilityChange=0;EligibilityChange<3;++EligibilityChange)for(bool DirectReset:{false,true}){
        Fixture F;Array(F.Dino.AttackInfos,F.Attacks.data(),1);F.Apply();
        assert(F.Attacks[0].MeleeSwingRadius==200);
        if(EligibilityChange==0)F.Attacks[0].bBasicAttack=false;
        if(EligibilityChange==1){SDK::UClass* Projectile=&ObjectClass;std::memcpy(&F.Attacks[0].ProjectileClass,&Projectile,sizeof(Projectile));}
        if(EligibilityChange==2)F.Attacks[0].MeleeDamageAmount=0;
        if(DirectReset)ResetDinoHarvestRange();else F.Apply(2,1300);
        assert(F.Attacks[0].MeleeSwingRadius==100&&DinoHarvestRange::Patches.empty());
        float Reach=0;assert(!GetDinoHarvestReachMeters(&F.Dino,Reach));
    }
    for(int EligibilityChange=0;EligibilityChange<3;++EligibilityChange){
        Fixture F;Array(F.Dino.AttackInfos,F.Attacks.data(),1);F.Apply();
        F.Attacks[0].MeleeSwingRadius=73; // an independent writer now owns this value
        if(EligibilityChange==0)F.Attacks[0].bBasicAttack=false;
        if(EligibilityChange==1){SDK::UClass* Projectile=&ObjectClass;std::memcpy(&F.Attacks[0].ProjectileClass,&Projectile,sizeof(Projectile));}
        if(EligibilityChange==2)F.Attacks[0].MeleeDamageAmount=0;
        F.Apply(2,1300);ResetDinoHarvestRange();assert(F.Attacks[0].MeleeSwingRadius==73);
    }
    {
        Fixture F;Array(F.Dino.AttackInfos,F.Attacks.data(),2);F.Apply();
        F.Attacks[1].AttackName=F.Attacks[0].AttackName;Array(F.Attacks[1].MeleeSwingSockets,&F.Sockets[0],1);
        F.Attacks[0].bBasicAttack=false;F.Apply(2,1300);ResetDinoHarvestRange();
        assert(F.Attacks[0].MeleeSwingRadius==200&&F.Attacks[1].MeleeSwingRadius==400);
        // Identical attack identities are ambiguous even if only one is currently eligible.
    }
    {
        Fixture F;auto* Poison=reinterpret_cast<SDK::APrimalDinoCharacter*>(uintptr_t(0x1000000000));
        float Reach=0;assert(!GetDinoHarvestReachMeters(Poison,Reach));UpdateDinoHarvestRange(Poison,true,2,1000);
        F.Radius.ClassPrivate=&F.IntType;F.Apply();assert(F.Attacks[0].MeleeSwingRadius==100);F.Radius.ClassPrivate=&F.FloatType;
        ++F.Radius.Offset;F.Apply(2,2000);assert(F.Attacks[0].MeleeSwingRadius==100);--F.Radius.Offset;
        F.AttackElement.ElementSize--;F.Apply(2,3000);assert(F.Attacks[0].MeleeSwingRadius==100);F.AttackElement.ElementSize++;
        Array(F.Dino.AttackInfos,F.Attacks.data(),65);F.Apply(2,4000);assert(F.Attacks[0].MeleeSwingRadius==100);
        Array(F.Dino.AttackInfos,F.Attacks.data(),3,2);F.Apply(2,5000);assert(F.Attacks[0].MeleeSwingRadius==100);
        Array(F.Dino.AttackInfos,reinterpret_cast<SDK::FDinoAttackInfo*>(Poison),1);F.Apply(2,6000);
        Array(F.Dino.AttackInfos,F.Attacks.data(),3);F.Basic.FieldMask=F.Basic.ByteMask=3;F.Apply(2,7000);assert(F.Attacks[0].MeleeSwingRadius==100);
    }
    {
        Fixture F;void* Buffer=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(Buffer);
        std::memcpy(Buffer,F.Attacks.data(),sizeof(F.Attacks));Array(F.Dino.AttackInfos,static_cast<SDK::FDinoAttackInfo*>(Buffer),3);
        DWORD Old=0;assert(VirtualProtect(Buffer,4096,PAGE_READONLY,&Old));F.Apply();
        assert(static_cast<SDK::FDinoAttackInfo*>(Buffer)[0].MeleeSwingRadius==100);
        ResetDinoHarvestRange();Array(F.Dino.AttackInfos,F.Attacks.data(),3);assert(VirtualFree(Buffer,0,MEM_RELEASE));
    }
    std::cout<<"Dino harvest range: real SDK reflection/array validation, local basic-melee-only radius, bounds, native reach, per-owner restoration, external changes, reorder/reallocation, duplicate keys, stale identity and invalid/read-only memory passed.\n";
}
