// Included after ExtensionObject. All inventory uses run on the game thread.
namespace {
    struct GachaOpenRuntime {
        ExtensionObject World, Controller, Human, Inventory;
        GachaOpenScheduler Scheduler;
        size_t LastBatchCount = 0;
    } GachaOpen;
    std::string GachaOpenStatus = "自动打开嘎查水晶未启用";
    uint64_t GachaItemKey(const SDK::FItemNetID& ID) {
        return (uint64_t(ID.ItemID1) << 32) | ID.ItemID2;
    }
    void ResetGachaOpen() {
        if (auto* PC = static_cast<SDK::AShooterPlayerController*>(GachaOpen.Controller.Get()))
            ClearAutoGachaTarget(PC, true);
        GachaOpen = {};
    }
}

void AutoOpenGachaCrystals(SDK::UWorld* World) {
    if (IsHusbandryInventoryActive()) return;
    if (!g_Config::bAutoOpenGachaCrystals) {
        ResetGachaOpen();
        g_AutoGachaPullRuntime.RetryAfter.clear();
        ExtensionStatus(GachaOpenStatus, "自动打开嘎查水晶未启用");
        return;
    }
    auto* PC = GetLocalShooterController(World);
    auto* Human = IsUsableObject(PC) ? PC->GetPlayerCharacter() : nullptr;
    auto* Inventory = IsUsableObject(Human) ? Human->MyInventoryComponent : nullptr;
    if (!IsUsableObject(World) || !IsUsableObject(PC) || !IsUsableObject(Human) || Human->IsDead() ||
        !IsUsableObject(Inventory)) {
        ResetGachaOpen();
        ExtensionStatus(GachaOpenStatus, "等待人物和背包数据");
        return;
    }
    if (GachaOpen.World.Get() != World || GachaOpen.Controller.Get() != PC ||
        GachaOpen.Human.Get() != Human || GachaOpen.Inventory.Get() != Inventory) {
        ResetGachaOpen();
        GachaOpen.World = ExtensionObject(World); GachaOpen.Controller = ExtensionObject(PC);
        GachaOpen.Human = ExtensionObject(Human); GachaOpen.Inventory = ExtensionObject(Inventory);
    }
    if (IsUploadTerminalActive()) {
        ExtensionStatus(GachaOpenStatus, "方尖碑面板打开期间暂停开启水晶");
        return;
    }
    const auto& Items = Inventory->InventoryItems;
    if (Items.Num() < 0 || Items.Num() > 4096 || (Items.Num() > 0 && !Items.IsValid())) {
        ExtensionStatus(GachaOpenStatus, "等待有效的背包列表");
        return;
    }
    // A use can synchronously remove/reorder Items on a listen server. Finish
    // collecting serial-validated identities before sending any requests.
    std::vector<GachaStack> Stacks;
    std::unordered_map<uint64_t, ExtensionObject> Candidates;
    for (int Index = 0; Index < Items.Num(); ++Index) {
        auto* Item = Items[Index];
        if (!IsUsableObject(Item) || !IsGachaCrystal(Item)) continue;
        const auto Key = GachaItemKey(Item->ItemID);
        const ExtensionObject Ref(Item);
        if (!Key || !Ref.Get() || !Candidates.emplace(Key, Ref).second) continue;
        Stacks.push_back({Key, Item->ItemQuantity});
    }
    const auto Now = GetTickCount64();
    GachaOpen.Scheduler.Observe(Stacks, Now);
    if (!GachaOpen.Scheduler.Remaining()) {
        if (g_Config::bAutoPullGachaCrystals) {
            ProcessAutoGachaVaultPull(World, PC, Human);
            ExtensionStatus(GachaOpenStatus, LanguageManager::Get().TranslateCopy("背包内已无嘎查水晶；") + LanguageManager::Get().TranslateCopy(g_AutoGachaPullRuntime.Status));
        }
        else ExtensionStatus(GachaOpenStatus, "背包内嘎查水晶已处理完，等待新水晶");
        return;
    }
    const auto Requests = GachaOpen.Scheduler.Plan(Now, g_Config::AutoOpenGachaIntervalMs);
    size_t Sent = 0;
    for (const auto Key : Requests) {
        auto* Item = static_cast<SDK::UPrimalItem*>(Candidates.at(Key).Get());
        if (!g_Config::bAutoOpenGachaCrystals || GachaOpen.Controller.Get() != PC || GachaOpen.Human.Get() != Human ||
            GachaOpen.Inventory.Get() != Inventory || Human->IsDead() || !Item || !IsGachaCrystal(Item) ||
            GachaItemKey(Item->ItemID) != Key) {
            GachaOpen.Scheduler.Unsent(Key); continue;
        }
        PC->ServerRequestInventoryUseItem(Inventory, Item->ItemID, Item->SlotIndex);
        ++Sent;
    }
    if (!Requests.empty()) GachaOpen.LastBatchCount = Sent;
    ExtensionStatus(GachaOpenStatus, FormatLocalized("背包剩余 {} 颗；上批请求 {} 颗，待同步 {} 颗（每批最多 {} 颗）",
        GachaOpen.Scheduler.Remaining(), GachaOpen.LastBatchCount, GachaOpen.Scheduler.Pending(), GachaOpenScheduler::BatchLimit));
}

std::string GetAutoPullGachaStatus() {
    std::scoped_lock Lock(ExtensionStatusMutex);
    return LanguageManager::Get().TranslateCopy(GachaOpenStatus);
}
