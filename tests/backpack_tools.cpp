#include "../src/internal/Hack/BackpackRules.h"
#include "../src/internal/Hack/InventoryTransferRules.h"
#include "../src/internal/Menu/InterfaceMotion.h"
#include "../src/internal/Lua/SessionSnapshot.h"
#include <cassert>
#include <iostream>
#include <limits>
int main() {
    using namespace BackpackRules;
    const char* Names[]={"PrimalItemResource_Metal_C","PrimalItemResource_Stone_C","PrimalItemConsumable_Berry_Amarberry_C",
        "PrimalItemConsumable_Mushroom_Aquatic_C","PrimalItemResource_Crystal_C","PrimalItemResource_Fibers_C",
        "PrimalItemResource_Thatch_C","PrimalItemResource_Wood_C","PrimalItemResource_Ambergris_C","PrimalItemResource_Flint_C","PrimalItemResource_Obsidian_C"};
    for (int I=0;I<11;++I) assert(Classify(Names[I])==(1u<<I));
    assert(Classify("PrimalItemResource_RareMushroom_C")==Mushrooms);
    assert(Classify("PrimalItemResource_FungalWood_C")==Wood);
    for (const char* N:{"PrimalItemResource_MetalIngot_C","PrimalItemConsumable_Seed_Amarberry_C","PrimalItemStructure_MetalFloor_C",
        "PrimalItemResource_GachaCrystal_C","PrimalItem_WeaponMetalPick_C","PrimalItemResource_Crystal_C_12"}) assert(!Classify(N));
    assert(CanDrop(Metal,Metal|Stone,300,false,false,false,false,true));
    assert(!CanDrop(Crystal,Metal|Stone,300,false,false,false,false,true));
    assert(!CanDrop(Metal,0,300,false,false,false,false,true));
    assert(!CanDrop(Metal,All,300,true,false,false,false,true));
    assert(!CanDrop(Metal,All,300,false,true,false,false,true));
    assert(!CanDrop(Metal,All,300,false,false,true,false,true));
    assert(!CanDrop(Metal,All,300,false,false,false,true,true));
    assert(!CanDrop(Metal,All,300,false,false,false,false,false));
    assert(!CanDrop(Metal,All,0,false,false,false,false,true));
    using namespace g_Hack::TransferRules;
    Item Gear{}; Gear.Type=1; Gear.Quantity=1; Gear.Removable=true;
    assert(Eligible(Gear,Destination::Grinder));
    for(int T:{2,4}) {Gear.Type=T;assert(Eligible(Gear,Destination::Grinder));}
    Gear.Type=1;
    for (auto Member:{&Item::Equipped,&Item::Hotbar,&Item::Skin,&Item::Engram,&Item::Blueprint,&Item::PreventGrinding}) {
        Gear.*Member=true;assert(!Eligible(Gear,Destination::Grinder));Gear.*Member=false;
    }
    Gear.Blueprint=true;assert(Eligible(Gear,Destination::Vault));
    Gear.Hotbar=true;assert(!Eligible(Gear,Destination::Vault));Gear.Hotbar=false;
    Gear.Engram=true;assert(!Eligible(Gear,Destination::Vault));Gear.Engram=false;
    Gear.Blueprint=false;assert(!Eligible(Gear,Destination::Vault));
    assert(IsGrinderClass("BP_GrinderPlus_C") && IsGrinderClass("StorageBox_IndustrialGrinder_C"));
    assert(!IsGrinderClass("BP_GrinderPlus_C_23") && !IsGrinderClass("PrimalInventory_GrinderPlus_C"));
    assert(IsVaultClass("StorageBox_Huge_C") && !IsVaultClass("StorageBox_Small_C"));
    for(bool Original:{false,true}) for(bool Desired:{false,true}) {
        OwnedFlag Flag;assert(Flag.Apply(Original,Desired)==Desired);assert(Flag.Apply(Desired,Desired)==Desired);
        assert(Flag.Restore(Desired)==Original);assert(!Flag.Owned);
    }
    OwnedFlag Flag;Flag.Apply(false,true);assert(!Flag.Restore(false)); // preserve external changes
    OwnedFlag Default;Default.Apply(false,true);OwnedFlag Inherited=Default;Inherited.Apply(true,true);assert(!Inherited.Restore(true));
    PhoenixMotion::PageFade Fade;Fade.Request(2);Fade.Tick(.06f);assert(Fade.Current==0 && Fade.Alpha>0 && Fade.Alpha<1);
    Fade.Tick(.06f);assert(Fade.Current==2 && Fade.Alpha==0);Fade.Tick(.06f);assert(Fade.Alpha>0 && Fade.Alpha<1);
    Fade.Request(3);Fade.Request(4);for(int I=0;I<30;++I)Fade.Tick(1.f/60);assert(Fade.Current==4 && Fade.Alpha==1);
    Fade.Snap(1);assert(Fade.Current==1 && Fade.Alpha==1);
    assert(PhoenixMotion::Approach(1,0,std::numeric_limits<float>::quiet_NaN())==1);
    PhoenixSession::Snapshot S;S.Valid=S.InputReady=true;S.Updated=100;S.Session=1;PhoenixSession::Publish(S);
    assert(PhoenixSession::Read(200).Valid);assert(!PhoenixSession::Read(1200).Valid);assert(!PhoenixSession::Read(1200).InputReady);
    PhoenixSession::Publish({});assert(!PhoenixSession::Read(1300).Valid);
    std::cout<<"Resource categories and exclusions, grinder/vault protection, owned flag restoration, page fades and stale snapshots passed.\n";
}
