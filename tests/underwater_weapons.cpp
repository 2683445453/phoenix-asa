// Exercise the actual production tuning lifecycle. The guarded resolver itself
// is covered separately against real SDK layouts by weapon_water_access.cpp.
#include "../src/internal/Hack/BackpackRules.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace SDK {
    struct UClass;
    enum class EObjectFlags : uint32_t { None=0,ClassDefaultObject=1,BeginDestroyed=2,FinishDestroyed=4,MirroredGarbage=8 };
    inline bool operator&(EObjectFlags A,EObjectFlags B){return (uint32_t(A)&uint32_t(B))!=0;}
    struct UObject {int32_t Index=-1;UClass* Class=nullptr;EObjectFlags Flags=EObjectFlags::None;
        bool IsDefaultObject()const{return Flags&EObjectFlags::ClassDefaultObject;}};
    // Intentionally no ClassDefaultObject, SuperStruct, IsA or SoftPtr::Get:
    // production scanning must cross the checked WaterAccess boundary.
    struct UClass:UObject {bool Supported=true;};
    struct AShooterWeapon:UObject {bool Values[2]{false,true};bool FlagsAvailable=true;double Created=-1;};
    struct UPrimalItem:UObject {UClass* Template=nullptr;int32_t TemplateSerial=0;bool Eligible=true;};
    template<class T> struct TArray {
        std::vector<T> Values;
        TArray()=default;TArray(std::initializer_list<T> V):Values(V){}
        bool IsValid()const{return true;}int32_t Num()const{return int32_t(Values.size());}
        const T& operator[](int32_t Index)const{return Values.at(size_t(Index));}
        const T* GetDataPtr()const{return Values.data();}
        auto begin()const{return Values.begin();}auto end()const{return Values.end();}
    };
}

namespace FixtureAccess {
    struct Entry {SDK::UObject* Object=nullptr;int32_t Serial=0;bool Live=true;};
    inline std::vector<Entry> Registry;
    inline std::vector<SDK::UClass*> Classes;
    inline std::map<SDK::UClass*,SDK::AShooterWeapon*> Defaults;
    inline int ResolvedItems=0,DefaultLookups=0,Writes=0,ClearCacheCalls=0,ValidationDepth=0,ValidationPasses=0;
    inline std::function<void(SDK::UPrimalItem*)> BeforeItemResolve;
    struct WriteEvent {SDK::UObject* Object;size_t Offset;bool Value;};
    inline std::vector<WriteEvent> WriteEvents;
    int32_t Register(SDK::UObject& O,SDK::UClass* C){
        O.Index=int32_t(Registry.size());O.Class=C;Registry.push_back({&O,100+O.Index,true});return Registry.back().Serial;
    }
    void Class(SDK::UClass& C){Register(C,&C);Classes.push_back(&C);}
    Entry* Find(SDK::UObject* O){
        auto I=std::find_if(Registry.begin(),Registry.end(),[&](auto& E){return E.Object==O;});
        return I==Registry.end()?nullptr:&*I;
    }
}
namespace g_Hack {
    namespace WaterAccess {
        struct ValidationPass {
            ValidationPass(){++FixtureAccess::ValidationDepth;++FixtureAccess::ValidationPasses;}
            ~ValidationPass(){--FixtureAccess::ValidationDepth;}
        };
        void ClearCaches(){++FixtureAccess::ClearCacheCalls;}
        bool Registry(int32_t Index,SDK::UObject*& Object,int32_t& Serial){
            Object=nullptr;Serial=0;
            if(Index<0||size_t(Index)>=FixtureAccess::Registry.size())return false;
            const auto& E=FixtureAccess::Registry[size_t(Index)];if(!E.Live)return false;
            Object=E.Object;Serial=E.Serial;return Object&&Serial>=0;
        }
        bool IsLive(SDK::UObject* O,bool AllowDefault=false){
            auto* E=FixtureAccess::Find(O);if(!E||!E->Live||!O->Class)return false;
            return !(O->Flags&SDK::EObjectFlags::BeginDestroyed)&&!(O->Flags&SDK::EObjectFlags::FinishDestroyed)&&
                !(O->Flags&SDK::EObjectFlags::MirroredGarbage)&&(AllowDefault||!O->IsDefaultObject());
        }
        bool IsClass(SDK::UClass* C){return IsLive(C)&&std::find(FixtureAccess::Classes.begin(),FixtureAccess::Classes.end(),C)!=FixtureAccess::Classes.end();}
        bool IsSupported(SDK::AShooterWeapon* W){return IsLive(W,true)&&IsClass(W->Class)&&W->Class->Supported;}
        bool CreationTime(SDK::AShooterWeapon* W,double& Time){
            Time=-1;if(!IsLive(W)||!IsSupported(W)||!std::isfinite(W->Created)||W->Created<=0)return false;
            Time=W->Created;return true;
        }
        SDK::UClass* ItemWeaponClass(SDK::UPrimalItem* I){
            ++FixtureAccess::ResolvedItems;
            if(FixtureAccess::BeforeItemResolve)FixtureAccess::BeforeItemResolve(I);
            if(!IsLive(I)||!I->Eligible||!IsClass(I->Template)||I->TemplateSerial<=0)return nullptr;
            auto* E=FixtureAccess::Find(I->Template);
            return E&&E->Serial==I->TemplateSerial?I->Template:nullptr;
        }
        SDK::AShooterWeapon* DefaultWeapon(SDK::UClass* C){
            ++FixtureAccess::DefaultLookups;if(!IsClass(C))return nullptr;
            auto I=FixtureAccess::Defaults.find(C);if(I==FixtureAccess::Defaults.end())return nullptr;
            auto* W=I->second;return IsLive(W,true)&&W->IsDefaultObject()&&W->Class==C?W:nullptr;
        }
        bool CapturedWeapon(SDK::AShooterWeapon* W,SDK::UClass* C,int32_t Index,int32_t Serial,
            int32_t ClassIndex,int32_t ClassSerial,bool Default){
            SDK::UObject* O=nullptr;int32_t CurrentSerial=-1;
            if(!W||!Registry(Index,O,CurrentSerial)||O!=W||(Serial>0&&CurrentSerial!=Serial)||
                !Registry(ClassIndex,O,CurrentSerial)||O!=C||(ClassSerial>0&&CurrentSerial!=ClassSerial)||
                !IsLive(W,true)||W->Class!=C||!IsSupported(W))return false;
            if(Default?DefaultWeapon(C)!=W:W->IsDefaultObject())return false;
            return true;
        }
        struct Flag {
            size_t Offset=0;uint8_t Mask=0;
            bool Accessible(SDK::UObject* O,bool=false)const{return Offset<2&&Mask==1&&IsLive(O,true);}
            bool Read(SDK::UObject* O)const{assert(Accessible(O));return static_cast<SDK::AShooterWeapon*>(O)->Values[Offset];}
            void Write(SDK::UObject* O,bool V)const{assert(Accessible(O,true));static_cast<SDK::AShooterWeapon*>(O)->Values[Offset]=V;++FixtureAccess::Writes;FixtureAccess::WriteEvents.push_back({O,Offset,V});}
            bool AccessValidated(SDK::UObject* O,bool Writable=false)const{return Accessible(O,Writable);}
            bool ReadValidated(SDK::UObject* O)const{return Read(O);}
            void WriteValidated(SDK::UObject* O,bool V)const{Write(O,V);}
        };
        bool GetFlags(SDK::AShooterWeapon* W,Flag& F,Flag& E){
            F={};E={};if(!IsSupported(W)||!W->FlagsAvailable)return false;F={0,1};E={1,1};return true;
        }
    }
    void ExtensionStatus(std::string& Target,const char* Value){Target=Value;}
#include "../src/internal/Hack/UnderwaterWeapons.inl"
}

namespace {
    struct Fixture {
        SDK::UClass RifleClass,ItemClass;
        SDK::AShooterWeapon Default,Current;
        SDK::UPrimalItem Item;
        SDK::TArray<SDK::UPrimalItem*> Items;
        uint64_t Now=1000;
        double WorldTime=40;
        Fixture(){
            FixtureAccess::Registry.clear();FixtureAccess::Classes.clear();FixtureAccess::Defaults.clear();
            FixtureAccess::ResolvedItems=FixtureAccess::DefaultLookups=FixtureAccess::Writes=0;
            FixtureAccess::ClearCacheCalls=FixtureAccess::ValidationDepth=FixtureAccess::ValidationPasses=0;
            FixtureAccess::BeforeItemResolve={};FixtureAccess::WriteEvents.clear();
            FixtureAccess::Class(RifleClass);FixtureAccess::Class(ItemClass);
            FixtureAccess::Register(Default,&RifleClass);FixtureAccess::Register(Current,&RifleClass);
            FixtureAccess::Register(Item,&ItemClass);Default.Flags=SDK::EObjectFlags::ClassDefaultObject;
            FixtureAccess::Defaults[&RifleClass]=&Default;Item.Template=&RifleClass;
            Item.TemplateSerial=FixtureAccess::Find(&RifleClass)->Serial;Items.Values.push_back(&Item);
        }
        ~Fixture(){g_Hack::ResetUnderwaterWeapons();}
        void Frame(SDK::AShooterWeapon* W,uint64_t Advance=16){
            const auto Before=FixtureAccess::ResolvedItems;
            g_Hack::UpdateUnderwaterWeapons(W,Items,Now,WorldTime);
            assert(FixtureAccess::ResolvedItems-Before<=4);
            assert(FixtureAccess::ValidationDepth==0);
            Now+=Advance;WorldTime+=double(Advance)/1000.;
        }
        void Tick(SDK::AShooterWeapon* W){Frame(W,500);}
        void CompletePass(SDK::AShooterWeapon* W){
            if(!g_Hack::WaterInventoryScanning&&Now<g_Hack::NextWaterInventoryScan){
                WorldTime+=double(g_Hack::NextWaterInventoryScan-Now)/1000.;Now=g_Hack::NextWaterInventoryScan;
            }
            const auto Target=g_Hack::WaterInventoryScanning?g_Hack::WaterInventoryPass:g_Hack::WaterInventoryPass+1;
            unsigned Frames=0;
            do{Frame(W);assert(++Frames<=4096);}while(g_Hack::WaterCompletedPass<Target);
        }
        void Check(const SDK::AShooterWeapon& W,bool F,bool E){assert(W.Values[0]==F&&W.Values[1]==E);}
    };
    struct WeaponKit {
        SDK::UClass Class;
        SDK::AShooterWeapon Default,Current;
        SDK::UPrimalItem Item;
        explicit WeaponKit(Fixture& F){
            FixtureAccess::Class(Class);
            FixtureAccess::Register(Default,&Class);FixtureAccess::Register(Current,&Class);
            FixtureAccess::Register(Item,&F.ItemClass);Default.Flags=SDK::EObjectFlags::ClassDefaultObject;
            FixtureAccess::Defaults[&Class]=&Default;Item.Template=&Class;
            Item.TemplateSerial=FixtureAccess::Find(&Class)->Serial;
        }
    };
    void AddResources(Fixture& F,std::vector<SDK::UPrimalItem>& Resources){
        for(auto& Item:Resources){
            FixtureAccess::Register(Item,&F.ItemClass);Item.Eligible=false;F.Items.Values.push_back(&Item);
        }
    }
    void FirstScanPreservesInstanceOverrides(){
        for(bool Firing:{false,true})for(bool Equipping:{false,true}){
            Fixture F;F.Current.Values[0]=Firing;F.Current.Values[1]=Equipping;
            F.Tick(&F.Current);F.Check(F.Current,true,false);F.Check(F.Default,true,false);
            g_Hack::ResetUnderwaterWeapons();F.Check(F.Current,Firing,Equipping);F.Check(F.Default,false,true);
        }
    }
    void NewInstancesInheritOwnedDefaultOriginals(){
        for(bool OriginalFiring:{false,true})for(bool OriginalEquipping:{false,true}){
            Fixture F;F.Default.Values[0]=OriginalFiring;F.Default.Values[1]=OriginalEquipping;
            F.Tick(nullptr);F.Check(F.Default,true,false);
            SDK::AShooterWeapon Spawned;FixtureAccess::Register(Spawned,&F.RifleClass);
            Spawned.Values[0]=F.Default.Values[0];Spawned.Values[1]=F.Default.Values[1];
            Spawned.Created=F.WorldTime-.125; // strictly after the first CDO patch, before this scan
            F.Tick(&Spawned);F.Check(Spawned,true,false);
            g_Hack::ResetUnderwaterWeapons();F.Check(Spawned,OriginalFiring,OriginalEquipping);
            F.Check(F.Default,OriginalFiring,OriginalEquipping);
        }
    }
    void SafeTemplateBoundary(){
        Fixture F;
        SDK::UPrimalItem Null,Poison,Stale,Zero,Ineligible;
        for(auto* I:{&Null,&Poison,&Stale,&Zero,&Ineligible})FixtureAccess::Register(*I,&F.ItemClass);
        Poison.Template=reinterpret_cast<SDK::UClass*>(uintptr_t(0x1000000000));Poison.TemplateSerial=5;
        Stale.Template=Zero.Template=Ineligible.Template=&F.RifleClass;
        Stale.TemplateSerial=F.Item.TemplateSerial+1;Zero.TemplateSerial=0;
        Ineligible.TemplateSerial=F.Item.TemplateSerial;Ineligible.Eligible=false;
        F.Items={{nullptr,&Null,&Poison,&Stale,&Zero,&Ineligible}};F.CompletePass(nullptr);
        F.Check(F.Default,false,true);assert(FixtureAccess::Writes==0);
        F.Items.Values.push_back(&F.Item);F.CompletePass(nullptr);F.Check(F.Default,true,false);
        assert(FixtureAccess::ResolvedItems>0&&FixtureAccess::DefaultLookups>0);
        g_Hack::ResetUnderwaterWeapons();F.Check(F.Default,false,true);
    }
    void SwitchingAndExternalOwnership(){
        Fixture F;F.Tick(&F.Current);F.Check(F.Current,true,false);
        F.Tick(nullptr);F.Check(F.Current,false,true);F.Check(F.Default,true,false);
        F.Items.Values.clear();F.Tick(nullptr);F.Check(F.Default,false,true);
        F.Current.Values[0]=true;F.Current.Values[1]=false;F.Tick(&F.Current);
        F.Current.Values[0]=false;F.Current.Values[1]=true;
        g_Hack::ResetUnderwaterWeapons();F.Check(F.Current,false,true); // preserve another owner's changes
        auto Writes=FixtureAccess::Writes;g_Hack::ResetUnderwaterWeapons();assert(FixtureAccess::Writes==Writes);
    }
    void RecycledObjectsAndClasses(){
        {
            Fixture F;F.Items.Values.clear();FixtureAccess::Defaults.clear();F.Tick(&F.Current);
            ++FixtureAccess::Find(&F.Current)->Serial;
            auto Writes=FixtureAccess::Writes;g_Hack::ResetUnderwaterWeapons();assert(FixtureAccess::Writes==Writes);
        }
        {
            Fixture F;F.Items.Values.clear();FixtureAccess::Defaults.clear();F.Tick(&F.Current);
            ++FixtureAccess::Find(&F.RifleClass)->Serial;
            auto Writes=FixtureAccess::Writes;g_Hack::ResetUnderwaterWeapons();assert(FixtureAccess::Writes==Writes);
        }
        {
            Fixture F;F.Items.Values.clear();FixtureAccess::Defaults.clear();F.Tick(&F.Current);SDK::AShooterWeapon Replacement;
            Replacement.Index=F.Current.Index;Replacement.Class=&F.RifleClass;
            FixtureAccess::Registry[size_t(F.Current.Index)].Object=&Replacement;
            auto Writes=FixtureAccess::Writes;g_Hack::ResetUnderwaterWeapons();assert(FixtureAccess::Writes==Writes);
            F.Check(Replacement,false,true);
        }
    }
    void RepeatedTogglesAndScanCadence(){
        Fixture F;
        for(int I=0;I<12;++I){
            F.Tick(&F.Current);F.Check(F.Current,true,false);F.Check(F.Default,true,false);
            auto Writes=FixtureAccess::Writes;g_Hack::UpdateUnderwaterWeapons(&F.Current,F.Items,F.Now-249);
            assert(FixtureAccess::Writes==Writes);
            g_Hack::ResetUnderwaterWeapons();F.Check(F.Current,false,true);F.Check(F.Default,false,true);
        }
    }
    void LazySerialAnchors(){
        {
            Fixture F;FixtureAccess::Find(&F.Default)->Serial=0;
            F.Tick(nullptr);F.Check(F.Default,true,false);
            FixtureAccess::Find(&F.Default)->Serial=901; // allocation of a formerly lazy weak serial
            g_Hack::ResetUnderwaterWeapons();F.Check(F.Default,false,true);
        }
        {
            Fixture F;F.Items.Values.clear();FixtureAccess::Find(&F.RifleClass)->Serial=0;
            F.Tick(&F.Current);F.Check(F.Current,true,false);F.Check(F.Default,false,true);
            FixtureAccess::Find(&F.RifleClass)->Serial=902;
            g_Hack::ResetUnderwaterWeapons();F.Check(F.Current,false,true);
        }
        {
            Fixture F;F.Items.Values.clear();FixtureAccess::Defaults.clear();FixtureAccess::Find(&F.Current)->Serial=0;
            F.Tick(&F.Current);F.Check(F.Current,false,true);assert(FixtureAccess::Writes==0);
        }
        {
            Fixture F;F.Items.Values.clear();FixtureAccess::Find(&F.RifleClass)->Serial=0;
            FixtureAccess::Find(&F.Current)->Serial=0;F.Tick(&F.Current);
            F.Check(F.Current,false,true);F.Check(F.Default,false,true);assert(FixtureAccess::Writes==0);
        }
        {
            Fixture F;FixtureAccess::Find(&F.Default)->Serial=0;F.Tick(nullptr);
            ++FixtureAccess::Find(&F.RifleClass)->Serial;
            auto Writes=FixtureAccess::Writes;g_Hack::ResetUnderwaterWeapons();assert(FixtureAccess::Writes==Writes);
        }
        {
            Fixture F;FixtureAccess::Find(&F.Default)->Serial=0;F.Tick(nullptr);
            SDK::AShooterWeapon Replacement;FixtureAccess::Register(Replacement,&F.RifleClass);
            Replacement.Flags=SDK::EObjectFlags::ClassDefaultObject;FixtureAccess::Defaults[&F.RifleClass]=&Replacement;
            auto Writes=FixtureAccess::Writes;g_Hack::ResetUnderwaterWeapons();assert(FixtureAccess::Writes==Writes);
            F.Check(Replacement,false,true);
        }
    }
    void CreationProvenancePreservesExistingOverrides(){
        // Merely observing an instance after a CDO patch cannot prove it was
        // constructed from our temporary defaults. Equal timestamps are ambiguous.
        for(double Creation:{-1.,0.,39.,40.,std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity()}){
            Fixture F;F.Tick(nullptr);SDK::AShooterWeapon Existing;
            FixtureAccess::Register(Existing,&F.RifleClass);Existing.Created=Creation;
            Existing.Values[0]=true;Existing.Values[1]=false;
            F.Tick(&Existing);F.Check(Existing,true,false);g_Hack::ResetUnderwaterWeapons();
            F.Check(Existing,true,false);F.Check(F.Default,false,true);
        }
        // An unavailable world timestamp at first patch must not be invented
        // later merely because a subsequent scan has a valid clock.
        {
            Fixture F;F.WorldTime=-1;F.Tick(nullptr);F.WorldTime=100;F.Tick(nullptr);
            SDK::AShooterWeapon Unknown;FixtureAccess::Register(Unknown,&F.RifleClass);Unknown.Created=90;
            Unknown.Values[0]=true;Unknown.Values[1]=false;F.Tick(&Unknown);g_Hack::ResetUnderwaterWeapons();
            F.Check(Unknown,true,false);F.Check(F.Default,false,true);
        }
        {
            Fixture F;F.Tick(nullptr);F.Tick(nullptr); // another scan must retain the original patch time
            SDK::AShooterWeapon Spawned;FixtureAccess::Register(Spawned,&F.RifleClass);
            Spawned.Created=40.125;Spawned.Values[0]=true;Spawned.Values[1]=false;
            F.Tick(&Spawned);g_Hack::ResetUnderwaterWeapons();F.Check(Spawned,false,true);
        }
        {
            Fixture F;F.WorldTime=0;F.Tick(nullptr);
            SDK::AShooterWeapon Spawned;FixtureAccess::Register(Spawned,&F.RifleClass);
            Spawned.Created=.125;Spawned.Values[0]=true;Spawned.Values[1]=false;
            F.Tick(&Spawned);g_Hack::ResetUnderwaterWeapons();
            assert(Spawned.Values[0]&&!Spawned.Values[1]&&"Zero world time conservatively preserves observed instance originals");
        }
    }
    void BoundedScanPrioritizesEquippedWeapon(){
        Fixture F;WeaponKit Tail(F);std::vector<SDK::UPrimalItem> Resources(37);
        F.Items.Values.clear();AddResources(F,Resources);F.Items.Values.push_back(&Tail.Item);
        FixtureAccess::BeforeItemResolve=[&](SDK::UPrimalItem*){
            F.Check(F.Current,true,false); // equipped weapon must work before a single backpack entry is resolved
        };
        F.Frame(&F.Current);F.Check(F.Current,true,false);
        F.Check(Tail.Default,false,true);assert(g_Hack::WaterInventoryScanning);
        F.CompletePass(&F.Current);F.Check(Tail.Default,true,false);
        assert(FixtureAccess::ResolvedItems==int(Resources.size()+1));
        assert(FixtureAccess::WriteEvents.front().Object==&F.Current);
        const auto Resolved=FixtureAccess::ResolvedItems;
        const auto Writes=FixtureAccess::Writes;
        // A complete inventory pass sleeps for 500ms; equipped refreshes must not rewrite equal bits.
        while(F.Now<g_Hack::NextWaterInventoryScan)F.Frame(&F.Current);
        assert(FixtureAccess::ResolvedItems==Resolved&&FixtureAccess::Writes==Writes);
        FixtureAccess::BeforeItemResolve={};g_Hack::ResetUnderwaterWeapons();
        F.Check(Tail.Default,false,true);F.Check(F.Current,false,true);
    }
    void LazyClockAndImmediateSwitch(){
        Fixture F;WeaponKit Other(F);int ClockReads=0;
        const auto Run=[&](SDK::AShooterWeapon* W){
            const auto Resolved=FixtureAccess::ResolvedItems;
            g_Hack::UpdateUnderwaterWeaponsWithClock(W,F.Items,F.Now,[&]{++ClockReads;return F.WorldTime;});
            assert(FixtureAccess::ResolvedItems-Resolved<=4&&FixtureAccess::ValidationDepth==0);
            F.Now+=16;F.WorldTime+=.016;
        };
        Run(&F.Current);assert(ClockReads==1);F.Check(F.Current,true,false);
        const auto Writes=FixtureAccess::Writes;
        for(unsigned I=0;I<80;++I)Run(&F.Current);
        assert(FixtureAccess::Writes==Writes&&ClockReads==1);
        // Swap one frame after an unchanged weapon check, without waiting for any inventory deadline.
        Run(&Other.Current);F.Check(F.Current,false,true);F.Check(Other.Current,true,false);
        assert(ClockReads==2);
        Run(nullptr);F.Check(Other.Current,false,true);
        g_Hack::ResetUnderwaterWeapons();
        const auto Cleared=FixtureAccess::ClearCacheCalls;
        assert(Cleared>0&&g_Hack::WaterWeapons.empty()&&!g_Hack::WaterInventoryScanning&&
            !g_Hack::WaterInventoryPass&&!g_Hack::WaterCompletedPass&&!g_Hack::WaterCurrent&&
            g_Hack::WaterInventorySnapshot.empty());
        const auto ResetWrites=FixtureAccess::Writes;
        g_Hack::ResetUnderwaterWeapons();
        assert(FixtureAccess::ClearCacheCalls==Cleared+1&&FixtureAccess::Writes==ResetWrites);
    }
    void UnvisitedDefaultsSurviveRollingPassAndRemoval(){
        Fixture F;WeaponKit Tail(F);std::vector<SDK::UPrimalItem> Resources(12);
        F.Items.Values.clear();AddResources(F,Resources);F.Items.Values.push_back(&Tail.Item);
        F.CompletePass(nullptr);F.Check(Tail.Default,true,false);
        F.Now=g_Hack::NextWaterInventoryScan;F.WorldTime+=.5;
        F.Frame(nullptr);assert(g_Hack::WaterInventoryScanning);F.Check(Tail.Default,true,false);
        F.Frame(nullptr);assert(g_Hack::WaterInventoryScanning);F.Check(Tail.Default,true,false);
        F.CompletePass(nullptr);F.Check(Tail.Default,true,false);
        // Removing the actual weapon may restore it only after a complete later inventory pass.
        F.Items.Values.pop_back();F.CompletePass(nullptr);
        for(unsigned I=0;I<4;++I)F.Frame(nullptr);
        F.Check(Tail.Default,false,true);g_Hack::ResetUnderwaterWeapons();
    }
    void InventoryChurnDoesNotSkipRetainedWeapon(){
        Fixture F;WeaponKit Tail(F);std::vector<SDK::UPrimalItem> Resources(7);
        F.Items.Values.clear();AddResources(F,Resources);F.Items.Values.push_back(&Tail.Item);
        F.CompletePass(nullptr);F.Check(Tail.Default,true,false);
        F.Now=g_Hack::NextWaterInventoryScan;F.WorldTime+=.5;
        F.Frame(nullptr);assert(g_Hack::WaterInventoryScanning);F.Check(Tail.Default,true,false);
        // Consuming or moving already visited front entries shifts an unvisited weapon behind the cursor.
        F.Items.Values.erase(F.Items.Values.begin(),F.Items.Values.begin()+4);
        for(unsigned I=0;I<8;++I){F.Frame(nullptr);F.Check(Tail.Default,true,false);}
        F.CompletePass(nullptr);F.Check(Tail.Default,true,false);
        g_Hack::ResetUnderwaterWeapons();F.Check(Tail.Default,false,true);
    }
    void SameSizeReorderAndReplacementKeepIncompletePassConservative(){
        Fixture F;WeaponKit Tail(F),Added(F);std::vector<SDK::UPrimalItem> Resources(7);
        F.Items.Values.clear();AddResources(F,Resources);F.Items.Values.push_back(&Tail.Item);
        F.CompletePass(nullptr);F.Check(Tail.Default,true,false);
        const auto Completed=g_Hack::WaterCompletedPass;
        F.Now=g_Hack::NextWaterInventoryScan;F.WorldTime+=.5;F.Frame(nullptr);
        assert(g_Hack::WaterInventoryScanning);
        // Sorting changes positions without changing the count. Replacing one unseen resource
        // simultaneously adds a new weapon while the snapshot still contains the removed resource.
        std::swap(F.Items.Values.front(),F.Items.Values.back());
        auto* Removed=F.Items.Values[5];F.Items.Values[5]=&Added.Item;
        FixtureAccess::Find(Removed)->Live=false;
        FixtureAccess::BeforeItemResolve=[&](SDK::UPrimalItem* I){assert(I!=Removed);};
        unsigned Frames=0;
        while(g_Hack::WaterInventoryScanning){F.Frame(nullptr);F.Check(Tail.Default,true,false);assert(++Frames<16);}
        assert(g_Hack::WaterCompletedPass==Completed); // a changed pointer sequence cannot prove removals
        F.Check(Added.Default,false,true);
        F.CompletePass(nullptr);F.Check(Tail.Default,true,false);F.Check(Added.Default,true,false);
        assert(g_Hack::WaterCompletedPass>Completed);
        FixtureAccess::BeforeItemResolve={};g_Hack::ResetUnderwaterWeapons();
        F.Check(Tail.Default,false,true);F.Check(Added.Default,false,true);
    }
    void FullCacheStillPrioritizesEquippedWeaponAndPreservesInheritance(){
        for(bool Inherited:{false,true}){
            Fixture F;std::vector<WeaponKit> Backlog;
            Backlog.reserve(g_Hack::WaterMaximumTunings);F.Items.Values.clear();
            for(size_t I=0;I<g_Hack::WaterMaximumTunings;++I){
                Backlog.emplace_back(F);F.Items.Values.push_back(&Backlog.back().Item);
            }
            F.CompletePass(nullptr);
            assert(g_Hack::WaterWeapons.size()==g_Hack::WaterMaximumTunings);
            for(const auto& Kit:Backlog)F.Check(Kit.Default,true,false);
            WeaponKit Invalid(F);Invalid.Class.Supported=false;
            const auto Writes=FixtureAccess::Writes;
            F.Frame(&Invalid.Current);F.Check(Invalid.Current,false,true);
            assert(FixtureAccess::Writes==Writes&&g_Hack::WaterWeapons.size()==g_Hack::WaterMaximumTunings);
            auto* Equipped=Inherited?&Backlog.front().Current:&F.Current;
            if(Inherited){
                // The first CDO is the natural eviction candidate. A newly constructed instance
                // needs its pre-patch originals even if capacity pressure evicts another record.
                Equipped->Values[0]=true;Equipped->Values[1]=false;Equipped->Created=F.WorldTime-.001;
            }
            F.Frame(Equipped);F.Check(*Equipped,true,false);
            assert(g_Hack::WaterWeapons.size()<=g_Hack::WaterMaximumTunings);
            assert(std::any_of(g_Hack::WaterWeapons.begin(),g_Hack::WaterWeapons.end(),
                [&](const auto& T){return T.Pointer==Equipped&&!T.Default;}));
            size_t RestoredDefaults=0;
            for(const auto& Kit:Backlog)if(!Kit.Default.Values[0]&&Kit.Default.Values[1])++RestoredDefaults;
            assert(RestoredDefaults==1); // exactly one background default yields capacity and is restored
            g_Hack::ResetUnderwaterWeapons();
            assert(!Equipped->Values[0]&&Equipped->Values[1]&&
                "Full-cache eviction must preserve a newly equipped weapon's native or inherited originals");
            for(const auto& Kit:Backlog)F.Check(Kit.Default,false,true);
        }
    }
}
int main(){
    FirstScanPreservesInstanceOverrides();NewInstancesInheritOwnedDefaultOriginals();SafeTemplateBoundary();
    SwitchingAndExternalOwnership();RecycledObjectsAndClasses();RepeatedTogglesAndScanCadence();LazySerialAnchors();
    CreationProvenancePreservesExistingOverrides();
    BoundedScanPrioritizesEquippedWeapon();LazyClockAndImmediateSwitch();
    UnvisitedDefaultsSurviveRollingPassAndRemoval();InventoryChurnDoesNotSkipRetainedWeapon();
    SameSizeReorderAndReplacementKeepIncompletePassConservative();
    FullCacheStillPrioritizesEquippedWeaponAndPreservesInheritance();
    std::cout<<"Underwater runtime: bounded 4-item frames, current-weapon priority/switches at full capacity, lazy clock, no unchanged writes, rolling-pass retention/churn, cache reset, guarded templates and all ownership/creation-time regressions passed.\n";
}
