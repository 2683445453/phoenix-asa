// Serial-validated backpack -> native / QoL+ storage transfers on the game thread.
namespace {
    std::atomic<bool> DepositActive{false};
    std::string GrinderDepositStatus = "装备与建筑自动转移未启用";
    std::string BlueprintDepositStatus = "蓝图自动转移未启用";
    struct DepositRuntime {
        ExtensionObject World, Controller, Human, Backpack, Target, Item;
        TransferRules::Destination Kind = TransferRules::Destination::Grinder;
        uint64_t OpenedAt = 0, NextAction = 0, SentAt = 0;
        int BeforeQuantity = 0;
        bool Sent = false, PreferVault = false;
        std::unordered_map<uint64_t, uint64_t> Retry;
    } Deposit;
    std::string& DepositStatus(TransferRules::Destination Kind) {
        return Kind == TransferRules::Destination::Grinder ? GrinderDepositStatus : BlueprintDepositStatus;
    }
    bool DepositEnabled(TransferRules::Destination Kind) {
        return Kind == TransferRules::Destination::Grinder ? g_Config::bAutoDepositGachaGear : g_Config::bAutoDepositFishingBlueprints;
    }
    float DepositRange(TransferRules::Destination Kind) {
        return ExtensionLimit(Kind == TransferRules::Destination::Grinder ? g_Config::AutoDepositGrinderRangeMeters :
            g_Config::AutoDepositVaultRangeMeters, 2, 30, 12);
    }
    template<class T> bool ValidDepositArray(const SDK::TArray<T>& Items) {
        return Items.Num() >= 0 && Items.Num() <= 4096 && (Items.Num() == 0 || Items.IsValid());
    }
    bool DepositContains(const SDK::TArray<SDK::UPrimalItem*>& Items, SDK::UPrimalItem* Item) {
        if (!ValidDepositArray(Items)) return true; // uncertain protection state: keep the item
        for (auto* Entry : Items) if (IsUsableObject(Entry) &&
            (Entry == Item || GachaItemKey(Entry->ItemID) == GachaItemKey(Item->ItemID))) return true;
        return false;
    }
    bool CanDeposit(SDK::UPrimalItem* Item, SDK::UPrimalInventoryComponent* Backpack, TransferRules::Destination Kind) {
        if (!IsUsableObject(Item) || !IsUsableObject(Backpack) || !GachaItemKey(Item->ItemID)) return false;
        TransferRules::Item Value;
        Value.Type = static_cast<int>(Item->MyItemType); Value.Quantity = Item->ItemQuantity;
        Value.Blueprint = Item->bIsBlueprint; Value.Engram = Item->bIsEngram; Value.Skin = Item->bIsItemSkin;
        Value.Equipped = Item->bEquippedItem || DepositContains(Backpack->EquippedItems, Item);
        Value.Hotbar = Item->SlotIndex >= 0 || DepositContains(Backpack->ItemSlots, Item);
        Value.Removable = Item->bAllowRemovalFromInventory;
        Value.PreventGrinding = Item->bForcePreventGrinding;
        return TransferRules::Eligible(Value, Kind);
    }
    bool IsDepositContainer(SDK::APrimalStructureItemContainer* Container, TransferRules::Destination Kind) {
        for (auto* Class = Container->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct)) {
            if (Kind == TransferRules::Destination::Grinder ? TransferRules::IsGrinderClass(Class->GetName()) :
                TransferRules::IsVaultClass(Class->GetName())) return true;
        }
        return false;
    }
    void CloseDepositTarget(bool Retry) {
        auto* PC = static_cast<SDK::AShooterPlayerController*>(Deposit.Controller.Get());
        auto* Target = static_cast<SDK::UPrimalInventoryComponent*>(Deposit.Target.Get());
        if (PC && Target) {
            auto* Viewing = PC->GetViewingInventory();
            if (!PC->IsViewingInventoryUI() && (!Viewing || Viewing == Target)) PC->ServerActorCloseRemoteInventory(Target);
        }
        if (Retry && Deposit.Target.Pointer) Deposit.Retry[Deposit.Target.Key()] = GetTickCount64() + 10000;
        Deposit.Target = {}; Deposit.Item = {}; Deposit.Sent = false;
        Deposit.PreferVault = !Deposit.PreferVault;
        Deposit.NextAction = GetTickCount64() + 350;
        DepositActive.store(false);
    }
}
bool IsAutomaticDepositActive() { return DepositActive.load(); }
std::string GetGrinderDepositStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(GrinderDepositStatus); }
std::string GetBlueprintDepositStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(BlueprintDepositStatus); }
void ResetAutoDeposit() { CloseDepositTarget(false); Deposit = {}; }

void AutoDepositItems(SDK::UWorld* World) {
    if (IsHusbandryInventoryActive()) return;
    if (!g_Config::bAutoDepositGachaGear && !g_Config::bAutoDepositFishingBlueprints) {
        ResetAutoDeposit(); return;
    }
    auto* PC = GetLocalShooterController(World);
    auto* Human = IsUsableObject(PC) ? PC->GetPlayerCharacter() : nullptr;
    auto* Backpack = IsUsableObject(Human) ? Human->MyInventoryComponent : nullptr;
    if (!IsUsableObject(World) || !IsUsableObject(PC) || !IsUsableObject(Human) || Human->IsDead() ||
        !IsUsableObject(Backpack) || !ValidDepositArray(Backpack->InventoryItems)) {
        ResetAutoDeposit(); return;
    }
    if (Deposit.World.Get() != World || Deposit.Controller.Get() != PC || Deposit.Human.Get() != Human || Deposit.Backpack.Get() != Backpack) {
        ResetAutoDeposit(); Deposit.World = ExtensionObject(World); Deposit.Controller = ExtensionObject(PC);
        Deposit.Human = ExtensionObject(Human); Deposit.Backpack = ExtensionObject(Backpack);
    }
    DWORD ForegroundPID = 0; GetWindowThreadProcessId(GetForegroundWindow(), &ForegroundPID);
    if (IsUploadTerminalActive() || PC->IsViewingInventoryUI() || g_MDX12::g_MenuState::g_isOpen ||
        ForegroundPID != GetCurrentProcessId()) {
        CloseDepositTarget(false);
        if (g_Config::bAutoDepositGachaGear) ExtensionStatus(GrinderDepositStatus, "转移已暂停：请关闭界面并返回游戏");
        if (g_Config::bAutoDepositFishingBlueprints) ExtensionStatus(BlueprintDepositStatus, "转移已暂停：请关闭界面并返回游戏");
        return;
    }
    const auto Now = GetTickCount64();
    if (Now < Deposit.NextAction) return;
    Deposit.NextAction = Now + 350;
    auto* Target = static_cast<SDK::UPrimalInventoryComponent*>(Deposit.Target.Get());
    if (Deposit.Target.Pointer && !Target) { CloseDepositTarget(false); return; }
    if (Target) {
        auto* Owner = Target->GetOwner();
        const float Distance = IsUsableObject(Owner) ? Human->GetDistanceTo(Owner) * .01f : INFINITY;
        auto* Viewing = PC->GetViewingInventory();
        if (!DepositEnabled(Deposit.Kind) || !std::isfinite(Distance) || Distance > DepositRange(Deposit.Kind) ||
            (Viewing && Viewing != Target)) { CloseDepositTarget(false); return; }
        auto* Item = static_cast<SDK::UPrimalItem*>(Deposit.Item.Get());
        if (Deposit.Sent) {
            const bool StillInBackpack = Item && DepositContains(Backpack->InventoryItems, Item);
            if (!StillInBackpack || Item->ItemQuantity < Deposit.BeforeQuantity) {
                ExtensionStatus(DepositStatus(Deposit.Kind), "转移请求已同步，继续整理背包");
                CloseDepositTarget(false); return;
            }
            if (Now - Deposit.SentAt >= 2500) {
                ExtensionStatus(DepositStatus(Deposit.Kind), "转移未同步：目标可能已满或访问被拒绝，稍后重试");
                CloseDepositTarget(true);
            }
            return;
        }
        if (Now - Deposit.OpenedAt < 700) return;
        // Recheck after opening the session: the item may have been equipped,
        // put on the hotbar, or consumed while inventory data was in flight.
        if (!Item || !DepositContains(Backpack->InventoryItems, Item) || !CanDeposit(Item, Backpack, Deposit.Kind)) {
            CloseDepositTarget(false); return;
        }
        if (!Target->IsAllowedInventoryAccess(PC) || !Target->RemoteInventoryAllowViewing(PC, 0)) {
            ExtensionStatus(DepositStatus(Deposit.Kind), "容器访问或距离检查未通过，正在寻找其他目标");
            CloseDepositTarget(true); return;
        }
        Deposit.BeforeQuantity = Item->ItemQuantity; Deposit.SentAt = Now; Deposit.Sent = true;
        PC->ServerTransferToRemoteInventory(Target, Item->ItemID, false, Item->ItemQuantity);
        ExtensionStatus(DepositStatus(Deposit.Kind), "已请求转移一组物品，等待服务器同步");
        return;
    }
    // Share the remote-inventory channel with loot, crafting and crystal pulls.
    // An existing automation completes its session before deposits start.
    if (PC->GetViewingInventory() || Loot.Target.Get() || g_AutoGachaPullRuntime.TargetInventory ||
        g_AutoCraftRuntime.OwnsCraftRemoteView || g_AutoCraftRuntime.DiscoveryInventory) return;
    if (Deposit.Retry.size() > 256) Deposit.Retry.clear();
    for (int Pass = 0; Pass < 2; ++Pass) {
        const auto Kind = ((Pass == 0) == Deposit.PreferVault) ? TransferRules::Destination::Vault : TransferRules::Destination::Grinder;
        if (!DepositEnabled(Kind)) continue;
        SDK::UPrimalItem* Candidate = nullptr;
        for (auto* Item : Backpack->InventoryItems) if (CanDeposit(Item, Backpack, Kind)) { Candidate = Item; break; }
        if (!Candidate) { ExtensionStatus(DepositStatus(Kind), "背包内没有符合条件的待转移物品"); continue; }
        SDK::UPrimalInventoryComponent* Best = nullptr;
        float BestDistance = DepositRange(Kind);
        auto* ContainerClass = SDK::APrimalStructureItemContainer::StaticClass();
        if (!ContainerClass) return;
        SDK::TArray<SDK::AActor*> Actors;
        SDK::UGameplayStatics::GetAllActorsOfClass(World, ContainerClass, &Actors);
        if (Actors.Num() < 0 || Actors.Num() > 100000 || (Actors.Num() && !Actors.IsValid())) return;
        for (auto* Actor : Actors) {
            if (!IsUsableObject(Actor) || !Actor->IsA(ContainerClass)) continue;
            auto* Container = static_cast<SDK::APrimalStructureItemContainer*>(Actor);
            auto* Inventory = Container->MyInventoryComponent;
            if (Container->bIsPreviewStructure || !IsUsableObject(Inventory) || !IsDepositContainer(Container, Kind)) continue;
            const float Distance = Human->GetDistanceTo(Container) * .01f;
            if (!std::isfinite(Distance) || Distance < 0 || Distance > BestDistance) continue;
            const ExtensionObject Ref(Inventory);
            if (!Ref.Get() || Deposit.Retry[Ref.Key()] > Now) continue;
            if (!Inventory->IsAllowedInventoryAccess(PC) || !Inventory->RemoteInventoryAllowViewing(PC, 0)) continue;
            Best = Inventory; BestDistance = Distance;
        }
        if (!Best) { ExtensionStatus(DepositStatus(Kind), "附近没有可访问的目标容器，请检查距离、权限或容量"); continue; }
        Deposit.Kind = Kind; Deposit.Target = ExtensionObject(Best); Deposit.Item = ExtensionObject(Candidate);
        Deposit.OpenedAt = Now; DepositActive.store(true);
        PC->ServerActorViewRemoteInventory(Best); PC->ServerRequestActorItems(Best, true, false);
        ExtensionStatus(DepositStatus(Kind), "正在连接附近容器，等待物品同步");
        return;
    }
}
