// Menu buttons queue work here; native UI and inventory calls run on the game thread.
namespace {
    std::atomic<bool> OpenUploadRequested{false}, NoteBuffRequested{false}, NoteBuffCancelRequested{false};
    std::atomic<bool> UploadTerminalActive{false};
    std::string UploadTerminalStatus = "点击后尝试在当前位置打开原生上传面板（实验）";
    std::string NoteBuffStatus = "点击后请求解锁可提供经验 Buff 的笔记";
    struct UploadTerminalRuntime {
        ExtensionObject World, Controller, Inventory, UI;
        bool OriginalTribute = false;
        bool OriginalAllowAdding = false;
        RemotePanelSession Session;
    } UploadTerminal;
    struct NoteBuffRuntime {
        ExtensionObject World, Controller, Human;
        NoteRequestBatch Batch;
    } NoteBuff;

    bool UploadPanelInViewport(SDK::UPrimalUI* UI) {
        if (!IsUsableObject(UI)) return false;
        auto* Function = UI->Class->GetFunction("Widget", "IsInViewport");
        struct VisibilityResult { bool ReturnValue = false; } Result;
        if (!IsUsableObject(Function) || Function->Size != sizeof(Result)) return false;
        UI->ProcessEvent(Function, &Result);
        return Result.ReturnValue;
    }

    void CloseUploadTerminal(bool CloseUI) {
        if (auto* PC = static_cast<SDK::AShooterPlayerController*>(UploadTerminal.Controller.Get())) {
            auto* UI = static_cast<SDK::UPrimalUI*>(UploadTerminal.UI.Get());
            if (CloseUI && UI && PC->GetViewingInventoryUI() == UI) UI->CloseWithAnimation();
        }
        if (auto* Inventory = static_cast<SDK::UPrimalInventoryComponent*>(UploadTerminal.Inventory.Get())) {
            Inventory->bIsTributeInventory = UploadTerminal.OriginalTribute;
            Inventory->bAllowAddingToArkTribute = UploadTerminal.OriginalAllowAdding;
        }
        UploadTerminal = {};
        UploadTerminalActive.store(false);
    }
    void ReleaseAutomaticInventoryViews(SDK::AShooterPlayerController* PC) {
        ResetAutoDeposit();
        CloseLootTarget();
        ClearAutoGachaTarget(PC, true);
        auto& Craft = g_AutoCraftRuntime;
        int32_t Serial = -1;
        if (Craft.OwnsCraftRemoteView && TryGetLiveObjectSerial(Craft.SelectedInventory,
            Craft.SelectedInventoryObjectIndex, &Serial) && Serial == Craft.SelectedInventorySerial)
            PC->ServerActorCloseRemoteInventory(Craft.SelectedInventory);
        Craft.OwnsCraftRemoteView = false;
        if (auto* Discovery = ResolveCachedObject(Craft.DiscoveryInventory, Craft.DiscoveryInventoryObjectIndex))
            PC->ServerActorCloseRemoteInventory(Discovery);
        Craft.DiscoveryInventory = nullptr;
        Craft.DiscoveryInventoryObjectIndex = -1;
    }
}

void RequestNoteXPBuff() { NoteBuffRequested.store(true); }
void CancelNoteXPBuff() { NoteBuffCancelRequested.store(true); }
std::string GetNoteXPBuffStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(NoteBuffStatus); }
void RequestOpenUploadTerminal() { OpenUploadRequested.store(true); }
std::string GetUploadTerminalStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(UploadTerminalStatus); }
bool IsUploadTerminalActive() { return UploadTerminalActive.load(); }

void ResetWorldUtilities() {
    OpenUploadRequested.store(false); NoteBuffRequested.store(false); NoteBuffCancelRequested.store(false);
    CloseUploadTerminal(true);
    NoteBuff = {};
}

void WorldUtilitiesTick(SDK::UWorld* World) {
    auto* PC = GetLocalShooterController(World);
    auto* Human = IsUsableObject(PC) ? PC->GetPlayerCharacter() : nullptr;
    const bool Ready = IsUsableObject(World) && IsUsableObject(PC) && IsUsableObject(Human) && !Human->IsDead();
    if (UploadTerminalActive.load()) {
        auto* Inventory = static_cast<SDK::UPrimalInventoryComponent*>(UploadTerminal.Inventory.Get());
        auto* UI = static_cast<SDK::UPrimalUI*>(UploadTerminal.UI.Get());
        if (!Ready || !Inventory || Inventory != Human->MyInventoryComponent ||
            UploadTerminal.World.Get() != World || UploadTerminal.Controller.Get() != PC) {
            CloseUploadTerminal(true);
            ExtensionStatus(UploadTerminalStatus, "人物、地图或背包已变化，已释放上传面板");
        }
        else {
            auto* CurrentUI = PC->GetViewingInventoryUI();
            if (!UploadTerminal.Session.Keep(GetTickCount64(), UI && CurrentUI == UI,
                UploadPanelInViewport(UI), UI && UI->bIsClosing, CurrentUI && CurrentUI != UI)) {
                CloseUploadTerminal(false);
                ExtensionStatus(UploadTerminalStatus, "原地上传面板已关闭；实际上传结果以服务器为准");
            }
        }
    }
    if (NoteBuff.Batch.Pending() && (!Ready || NoteBuff.World.Get() != World ||
        NoteBuff.Controller.Get() != PC || NoteBuff.Human.Get() != Human)) {
        NoteBuff = {};
        ExtensionStatus(NoteBuffStatus, "人物或地图已变化，笔记解锁已停止");
    }
    if (NoteBuffCancelRequested.exchange(false)) {
        NoteBuffRequested.store(false);
        NoteBuff = {};
        ExtensionStatus(NoteBuffStatus, "已停止笔记解锁");
    }

    if (OpenUploadRequested.exchange(false)) {
        if (!Ready) {
            ExtensionStatus(UploadTerminalStatus, "等待人物和地图加载完成后再打开");
        }
        else if (IsUploadTerminalActive()) {
            ExtensionStatus(UploadTerminalStatus, "原地上传面板已打开");
        }
        else if (PC->IsViewingInventoryUI()) {
            ExtensionStatus(UploadTerminalStatus, "请先关闭当前容器界面，再打开上传终端");
        }
        else {
            // The native inventory widget renders its tribute tabs from the
            // inventory mode. Use the player's own inventory as the associated
            // object, so opening the panel never needs a world structure.
            auto* Inventory = Human->MyInventoryComponent;
            auto* HUD = PC->GetShooterHud();
            if (!IsUsableObject(Inventory)) {
                ExtensionStatus(UploadTerminalStatus, "人物背包尚未就绪");
            }
            else if (!IsUsableObject(HUD)) {
                ExtensionStatus(UploadTerminalStatus, "游戏界面尚未就绪");
            }
            else {
                ResetFreeCamera();
                // Release background sessions before opening the native panel.
                UploadTerminalActive.store(true);
                ReleaseAutomaticInventoryViews(PC);
                UploadTerminal.World = ExtensionObject(World);
                UploadTerminal.Controller = ExtensionObject(PC);
                UploadTerminal.Inventory = ExtensionObject(Inventory);
                UploadTerminal.OriginalTribute = Inventory->bIsTributeInventory;
                UploadTerminal.OriginalAllowAdding = Inventory->bAllowAddingToArkTribute;
                UploadTerminal.Session.Open(GetTickCount64());
                Inventory->bIsTributeInventory = true;
                Inventory->bAllowAddingToArkTribute = true;
                // No fabricated world actor and no remote-inventory RPC. The
                // server still decides whether each native upload RPC is valid.
                auto* UI = HUD->ShowInventory(Inventory);
                if (IsUsableObject(UI)) {
                    UploadTerminal.UI = ExtensionObject(UI);
                    g_MDX12::g_MenuState::g_isOpen = false;
                    ExtensionStatus(UploadTerminalStatus, "已尝试原地打开原生上传面板；物资、恐龙、人物传服须以服务器确认结果为准");
                }
                else {
                    CloseUploadTerminal(false);
                    ExtensionStatus(UploadTerminalStatus, "原生上传面板未能打开，未执行上传或物资转移");
                }
            }
        }
    }

    const uint64_t Now = GetTickCount64();
    if (NoteBuffRequested.exchange(false)) {
        NoteBuff = {};
        auto* Data = Ready ? SDK::UPrimalGameData::BPGetGameData() : nullptr;
        if (!IsUsableObject(Data) || !Data->ExplorerNoteEntries.IsValid() ||
            Data->ExplorerNoteEntries.Num() <= 0 || Data->ExplorerNoteEntries.Num() > 16384) {
            ExtensionStatus(NoteBuffStatus, "人物或笔记数据尚未就绪");
        }
        else {
            std::vector<int> Notes;
            for (int I = 0; I < Data->ExplorerNoteEntries.Num(); ++I) {
                if (!Data->ExplorerNoteEntries[I].bPreventXPBuff &&
                    !SDK::UPrimalGameData::LocalIsPerMapExplorerNoteUnlocked(I)) Notes.push_back(I);
            }
            NoteBuff.World = ExtensionObject(World); NoteBuff.Controller = ExtensionObject(PC); NoteBuff.Human = ExtensionObject(Human);
            NoteBuff.Batch.Start(std::move(Notes), Now);
            if (!NoteBuff.Batch.Pending()) ExtensionStatus(NoteBuffStatus, "没有尚未解锁且可提供经验 Buff 的笔记");
        }
    }
    if (Ready && NoteBuff.Batch.Pending()) {
        if (const auto Index = NoteBuff.Batch.Next(Now)) {
            // bAvoidBuff=false requests the native XP buff. SavePlayerData=true
            // persists confirmed unlocks; there is no forced note-popup loop.
            PC->ServerUnlockPerMapExplorerNote(*Index, false, true);
            ExtensionStatus(NoteBuffStatus, FormatLocalized("已请求 {}/{} 条笔记{}；经验 Buff 以服务器实际授予为准",
                NoteBuff.Batch.Sent(), NoteBuff.Batch.Total(), NoteBuff.Batch.Pending() ? std::string{} : LanguageManager::Get().TranslateCopy("，本轮完成")));
        }
    }
}
