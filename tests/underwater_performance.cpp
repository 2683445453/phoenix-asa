// Real SDK objects, reflected properties and native memory checks; no game process.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "../src/external/CppSDK/SDK.hpp"
#include "../src/internal/Hack/BackpackRules.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Perf {
    uint64_t Queries=0,NameReads=0,SupportCalls=0,DefaultCalls=0,ItemCalls=0;
    SIZE_T WINAPI Query(LPCVOID P,PMEMORY_BASIC_INFORMATION I,SIZE_T L){++Queries;return ::VirtualQuery(P,I,L);}
    void Clear(){Queries=NameReads=SupportCalls=DefaultCalls=ItemCalls=0;}
}
namespace g_Hack {
#define VirtualQuery Perf::Query
#ifdef WATER_BENCH_BASELINE
#include "../release/water-performance/baseline-source/WeaponWaterAccess.inl"
#else
#include "../src/internal/Hack/WeaponWaterAccess.inl"
#endif
#undef VirtualQuery
    namespace WaterAccess {
        bool BenchIsSupported(SDK::AShooterWeapon* W){++Perf::SupportCalls;return IsSupported(W);}
        SDK::AShooterWeapon* BenchDefaultWeapon(SDK::UClass* C){++Perf::DefaultCalls;return DefaultWeapon(C);}
        SDK::UClass* BenchItemWeaponClass(SDK::UPrimalItem* I){++Perf::ItemCalls;return ItemWeaponClass(I);}
    }
    void ExtensionStatus(std::string& S,const char* V){S=V;}
#define IsSupported BenchIsSupported
#define DefaultWeapon BenchDefaultWeapon
#define ItemWeaponClass BenchItemWeaponClass
#ifdef WATER_BENCH_BASELINE
#include "../release/water-performance/baseline-source/UnderwaterWeapons.inl"
#else
#include "../src/internal/Hack/UnderwaterWeapons.inl"
#endif
#undef IsSupported
#undef DefaultWeapon
#undef ItemWeaponClass
}

namespace {
    SDK::UClass Meta{},ObjectClass{},ItemClass{},ResourceClass{},GunItemClass{},WeaponClass{},InstantClass{},WorldClass{};
    SDK::TUObjectArray Objects{};
    constexpr int Capacity=2048;
    SDK::FUObjectItem Entries[Capacity]{};
    SDK::FUObjectItem* Chunks[]{Entries};
    std::vector<std::wstring> Names{L"None"};
    SDK::FName Name(const wchar_t* Text){
        for(size_t I=0;I<Names.size();++I)if(Names[I]==Text)return SDK::FName(int32_t(I));
        Names.emplace_back(Text);return SDK::FName(int32_t(Names.size()-1));
    }
    const void* NameEntry(uint32_t Index){++Perf::NameReads;assert(Index<Names.size());return Names[Index].c_str();}
    void AppendName(const void* E,SDK::FString& O){O=SDK::FString(static_cast<const wchar_t*>(E));}
    void Register(SDK::UObject& O,SDK::UClass* C){
        assert(Objects.NumElements<Capacity);O.Index=Objects.NumElements++;O.Class=C;Entries[O.Index].Object=&O;
        int32_t Serial=100+O.Index;std::memcpy(reinterpret_cast<uint8_t*>(&Entries[O.Index])+0x10,&Serial,sizeof(Serial));
    }
    void Class(SDK::UClass& C,const wchar_t* N,SDK::UClass* Base,int32_t Size){Register(C,&Meta);C.Name=Name(N);C.SuperStruct=Base;C.Size=Size;}
    void Bool(SDK::FBoolProperty& F,SDK::FFieldClass* T,const wchar_t* N,int32_t Offset,uint8_t Mask){
        F.Name=Name(N);F.ClassPrivate=T;F.ArrayDim=1;F.ElementSize=1;F.Offset=Offset;
        F.FieldSize=1;F.ByteOffset=0;F.ByteMask=F.FieldMask=Mask;
    }
    void Report(const char* Scenario,const char* Phase,std::vector<double>& Samples){
        const auto Count=Samples.size();double Sum=0;for(auto V:Samples)Sum+=V;std::sort(Samples.begin(),Samples.end());
        std::cout<<Scenario<<','<<Phase<<','<<Count<<','<<std::fixed<<std::setprecision(4)<<Sum/Count<<','
            <<Samples[size_t((Count-1)*.95)]<<','<<Samples.back()<<','
            <<double(Perf::Queries)/Count<<','<<double(Perf::NameReads)/Count<<','<<double(Perf::SupportCalls)/Count<<','
            <<double(Perf::DefaultCalls)/Count<<','<<double(Perf::ItemCalls)/Count<<'\n'<<std::flush;
    }
    template<class Fn> void Measure(const char* Scenario,const char* Phase,int Count,Fn&& Operation){
        Perf::Clear();std::vector<double> Samples;Samples.reserve(Count);
        for(int I=0;I<Count;++I){
            auto Started=std::chrono::steady_clock::now();Operation();
            Samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Started).count());
        }
        Report(Scenario,Phase,Samples);
    }
#ifndef WATER_BENCH_BASELINE
    template<class Fn> void Warm(const char* Scenario,Fn&& Operation){
        Perf::Clear();std::vector<double> Samples;
        do{
            auto Started=std::chrono::steady_clock::now();Operation();
            Samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Started).count());
            assert(Samples.size()<4096&&"Bounded scan must complete the inventory pass");
        }while(!g_Hack::WaterCompletedPass);
        Report(Scenario,"warm_inventory_pass",Samples);
    }
#endif
}
namespace SDK {
    uintptr_t InSDKUtils::GetImageBase(){assert(false&&"Benchmark must never resolve the running game");return 0;}
    UClass* BasicFilesImplUtils::FindClassByName(const std::string& N,bool){
        if(N=="Class")return &Meta;if(N=="PrimalItem")return &ItemClass;
        if(N=="ShooterWeapon")return &WeaponClass;if(N=="ShooterWeapon_Instant")return &InstantClass;
        if(N=="World")return &WorldClass;assert(false);return nullptr;
    }
}
int main(){
    Objects.Objects=Chunks;Objects.MaxElements=Capacity;Objects.NumElements=1;Objects.MaxChunks=Objects.NumChunks=1;
    SDK::UObject::GObjects.InitManually(&Objects);SDK::FName::GetNameEntryFromName=reinterpret_cast<void*>(&NameEntry);
    SDK::FName::AppendString=reinterpret_cast<void*>(&AppendName);
    Class(Meta,L"Class",nullptr,sizeof(SDK::UClass));Class(ObjectClass,L"Object",nullptr,sizeof(SDK::UObject));
    Class(ItemClass,L"PrimalItem",&ObjectClass,sizeof(SDK::UPrimalItem));
    Class(ResourceClass,L"PrimalItemResource_Stone_C",&ItemClass,sizeof(SDK::UPrimalItem));
    Class(GunItemClass,L"PrimalItem_WeaponRifle_C",&ItemClass,sizeof(SDK::UPrimalItem));
    Class(WeaponClass,L"ShooterWeapon",&ObjectClass,sizeof(SDK::AShooterWeapon));
    Class(InstantClass,L"ShooterWeapon_Instant",&WeaponClass,sizeof(SDK::AShooterWeapon_Instant));
    Class(WorldClass,L"World",&ObjectClass,sizeof(SDK::UWorld));
    static SDK::UWorld World{};Register(World,&WorldClass);World.TimeSeconds=100;
    SDK::FFieldClass SoftType{},BoolType{},DoubleType{};
    SoftType.Name=Name(L"SoftClassProperty");BoolType.Name=Name(L"BoolProperty");DoubleType.Name=Name(L"DoubleProperty");
    SDK::FProperty Template{},Time{},Created{};SDK::FBoolProperty Firing{},Equipping{};
    static SDK::UPrimalItem LayoutItem{};static SDK::AShooterWeapon_Instant LayoutWeapon{};
    Template.Name=Name(L"WeaponTemplate");Template.ClassPrivate=&SoftType;Template.ArrayDim=1;Template.ElementSize=sizeof(LayoutItem.WeaponTemplate);
    Template.Offset=int32_t(reinterpret_cast<uint8_t*>(&LayoutItem.WeaponTemplate)-reinterpret_cast<uint8_t*>(&LayoutItem));
    ItemClass.ChildProperties=&Template;
    Bool(Firing,&BoolType,L"bAllowSubmergedFiring",0xCC8,1);Bool(Equipping,&BoolType,L"bPreventEquippingUnderwater",0xDD5,2);
    WeaponClass.ChildProperties=&Firing;Firing.Next=&Equipping;Equipping.Next=&Created;
    Time.Name=Name(L"TimeSeconds");Time.ClassPrivate=&DoubleType;Time.ArrayDim=1;Time.ElementSize=sizeof(double);
    Time.Offset=int32_t(reinterpret_cast<uint8_t*>(&World.TimeSeconds)-reinterpret_cast<uint8_t*>(&World));WorldClass.ChildProperties=&Time;
    Created.Name=Name(L"CreationTime");Created.ClassPrivate=&DoubleType;Created.ArrayDim=1;Created.ElementSize=sizeof(double);
    Created.Offset=int32_t(reinterpret_cast<uint8_t*>(&LayoutWeapon.CreationTime)-reinterpret_cast<uint8_t*>(&LayoutWeapon));
    std::array<SDK::UClass,30> GunClasses{};
    std::array<SDK::AShooterWeapon_Instant,30> Defaults{};
    for(size_t I=0;I<GunClasses.size();++I){
        auto N=L"WeapPerf"+std::to_wstring(I)+L"_C";
        Class(GunClasses[I],N.c_str(),&InstantClass,sizeof(SDK::AShooterWeapon_Instant));Register(Defaults[I],&GunClasses[I]);
        Defaults[I].Flags=SDK::EObjectFlags::ClassDefaultObject;GunClasses[I].ClassDefaultObject=&Defaults[I];
        Defaults[I].bPreventEquippingUnderwater=true;
    }
    static std::array<SDK::UPrimalItem,550> Items{};
    std::vector<SDK::UPrimalItem*> Resources,Mixed;
    for(size_t I=0;I<Items.size();++I){
        auto& Item=Items[I];Register(Item,I<500?&ResourceClass:&GunItemClass);
        Item.MyItemType=I<500?SDK::EPrimalItemType::Resource:SDK::EPrimalItemType::Weapon;
        if(I<500)Resources.push_back(&Item);
        else{
            auto* C=&GunClasses[(I-500)%GunClasses.size()];Item.WeaponTemplate.WeakPtr.ObjectIndex=C->Index;
            Item.WeaponTemplate.WeakPtr.ObjectSerialNumber=100+C->Index;
        }
        Mixed.push_back(&Item);
    }
    static SDK::AShooterWeapon_Instant Current{};Register(Current,&GunClasses[0]);Current.bPreventEquippingUnderwater=true;Current.CreationTime=20;
    auto R=SDK::TArray<SDK::UPrimalItem*>(Resources.data(),int32_t(Resources.size()),int32_t(Resources.size()));
    auto M=SDK::TArray<SDK::UPrimalItem*>(Mixed.data(),int32_t(Mixed.size()),int32_t(Mixed.size()));
    auto G=SDK::TArray<SDK::UPrimalItem*>(Mixed.data()+500,50,50);
    uint64_t Now=1000;
    const auto Update=[&](const SDK::TArray<SDK::UPrimalItem*>& A,SDK::AShooterWeapon* W){
#ifdef WATER_BENCH_BASELINE
        double T=-1;assert(g_Hack::WaterAccess::WorldTime(&World,T));g_Hack::UpdateUnderwaterWeapons(W,A,Now,T);
        Now+=250;World.TimeSeconds+=.25;
#else
        g_Hack::UpdateUnderwaterWeaponsWithClock(W,A,Now,[&]{double T=-1;assert(g_Hack::WaterAccess::WorldTime(&World,T));return T;});
        Now+=16;World.TimeSeconds+=.016;
#endif
    };
    std::cout<<"scenario,phase,iterations,avg_ms,p95_ms,max_ms,virtualquery_per_call,name_reads_per_call,runtime_supported_per_call,runtime_default_per_call,runtime_item_per_call\n";
    const auto Scenario=[&](const char* N,const SDK::TArray<SDK::UPrimalItem*>& A,SDK::AShooterWeapon* W){
        g_Hack::ResetUnderwaterWeapons();Measure(N,"first",1,[&]{Update(A,W);});
#ifdef WATER_BENCH_BASELINE
        Measure(N,"steady",5,[&]{Update(A,W);});
#else
        Warm(N,[&]{Update(A,W);});
        if(W){assert(Current.bAllowSubmergedFiring&&!Current.bPreventEquippingUnderwater);
            for(auto& D:Defaults)assert(D.bAllowSubmergedFiring&&!D.bPreventEquippingUnderwater);}
        Measure(N,"steady_600_frames",600,[&]{Update(A,W);});
#endif
        Measure(N,"reset",1,[&]{g_Hack::ResetUnderwaterWeapons();});
        assert(!Current.bAllowSubmergedFiring&&Current.bPreventEquippingUnderwater);
        for(auto& D:Defaults)assert(!D.bAllowSubmergedFiring&&D.bPreventEquippingUnderwater);
    };
    Scenario("500_resources",R,nullptr);Scenario("50_weapons_30_classes",G,&Current);Scenario("500_resources_50_weapons_30_classes",M,&Current);
}
