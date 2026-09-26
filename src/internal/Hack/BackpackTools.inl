// All mutations run from the serialized game-thread viewport callback.
namespace {
    std::string DropStatus = "自动丢弃未启用";
    struct DropRuntime {
        ExtensionObject World, Human, Inventory, Pending;
        uint64_t Next=0, Sent=0;
        int Before=0;
    } AutoDrop;
    uint32_t DropCategory(SDK::UPrimalItem* Item) {
        for (auto* C=Item->Class; C; C=static_cast<SDK::UClass*>(C->SuperStruct))
            if (auto Category=BackpackRules::Classify(C->GetName())) return Category;
        return 0;
    }
    ExtensionObject WaterWorld, WaterHuman;
}
std::string GetAutoDropStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(DropStatus); }
std::string GetUnderwaterWeaponStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(UnderwaterStatus); }
void ResetBackpackTools() {
    AutoDrop={};
    ResetUnderwaterWeapons();
    WaterWorld={}; WaterHuman={};
}
void BackpackToolsTick(SDK::UWorld* World) {
    if (!g_Config::bAutoDropResources && !g_Config::bUnderwaterWeapons) { ResetBackpackTools(); return; }
    auto* PC=GetLocalShooterController(World);
    auto* Human=IsUsableObject(PC)?PC->GetPlayerCharacter():nullptr;
    auto* Inventory=IsUsableObject(Human)?Human->MyInventoryComponent:nullptr;
    if (!IsUsableObject(World) || !IsUsableObject(PC) || !IsUsableObject(Human) || Human->IsDead() ||
        !IsUsableObject(Inventory) || !ValidDepositArray(Inventory->InventoryItems)) { ResetBackpackTools(); return; }
    const auto Now=GetTickCount64();
    if (WaterWorld.Get()!=World || WaterHuman.Get()!=Human) {
        ResetBackpackTools(); WaterWorld=ExtensionObject(World); WaterHuman=ExtensionObject(Human);
    }
    if (!g_Config::bUnderwaterWeapons) ResetUnderwaterWeapons();
    else UpdateUnderwaterWeaponsWithClock(Human->CurrentWeapon, Inventory->InventoryItems, Now, [World] {
        double WorldTime = -1;
        WaterAccess::WorldTime(World, WorldTime);
        return WorldTime;
    });
    if (!g_Config::bAutoDropResources) { AutoDrop={}; return; }
    if (AutoDrop.World.Get()!=World || AutoDrop.Human.Get()!=Human || AutoDrop.Inventory.Get()!=Inventory) {
        AutoDrop={}; AutoDrop.World=ExtensionObject(World); AutoDrop.Human=ExtensionObject(Human); AutoDrop.Inventory=ExtensionObject(Inventory);
    }
    if (!(g_Config::AutoDropResourceMask & BackpackRules::All)) { ExtensionStatus(DropStatus,"请先勾选要丢弃的物资；未选择时不会丢弃"); return; }
    DWORD ForegroundPID=0; GetWindowThreadProcessId(GetForegroundWindow(),&ForegroundPID);
    if (g_MDX12::g_MenuState::g_isOpen || PC->bShowMouseCursor || PC->IsViewingInventoryUI() || IsUploadTerminalActive() ||
        IsAutomaticDepositActive() || ForegroundPID!=GetCurrentProcessId()) {
        ExtensionStatus(DropStatus,"自动丢弃已暂停：关闭界面并返回游戏后继续"); return;
    }
    if (Now<AutoDrop.Next) return;
    AutoDrop.Next=Now+500;
    if (AutoDrop.Pending.Pointer) {
        auto* Item=static_cast<SDK::UPrimalItem*>(AutoDrop.Pending.Get());
        if (!Item || !DepositContains(Inventory->InventoryItems,Item) || Item->ItemQuantity<AutoDrop.Before) AutoDrop.Pending={};
        else if (Now-AutoDrop.Sent<2500) return;
        else { AutoDrop.Pending={}; AutoDrop.Next=Now+5000; ExtensionStatus(DropStatus,"丢弃未同步，稍后重试"); return; }
    }
    for (auto* Item:Inventory->InventoryItems) {
        if (!IsUsableObject(Item) || !GachaItemKey(Item->ItemID)) continue;
        if (!BackpackRules::CanDrop(DropCategory(Item),g_Config::AutoDropResourceMask,Item->ItemQuantity,
            Item->bIsBlueprint,Item->bIsEngram,Item->bEquippedItem || DepositContains(Inventory->EquippedItems,Item),
            Item->SlotIndex>=0 || DepositContains(Inventory->ItemSlots,Item),Item->bAllowRemovalFromInventory)) continue;
        AutoDrop.Pending=ExtensionObject(Item); AutoDrop.Before=Item->ItemQuantity; AutoDrop.Sent=Now;
        PC->ServerDropFromRemoteInventory(Inventory,Item->ItemID);
        ExtensionStatus(DropStatus,"已请求丢弃一组所选物资，等待服务器同步"); return;
    }
    ExtensionStatus(DropStatus,"背包内没有可丢弃的所选物资");
}
