// Menu buttons queue work here; native UI and inventory calls run on the game thread.
namespace {
    std::atomic<bool> OpenUploadRequested{false}, NoteBuffCancelRequested{false};
    NoteRequestGate NoteBuffGate;
    std::atomic<bool> UploadTerminalActive{false};
    std::string UploadTerminalStatus = "点击后锁定并打开已加载的方尖碑";
    std::string NoteBuffStatus = "点击后请求解锁可提供经验 Buff 的笔记";
    struct UploadTerminalRuntime {
        ExtensionObject World, Controller, Inventory, UI;
        FloatTuning ViewDistance, AccessDistance;
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
            if (auto* Inventory = static_cast<SDK::UPrimalInventoryComponent*>(UploadTerminal.Inventory.Get()); Inventory) {
                // A late close for an old container must not close a newly opened UI.
                auto* Viewing = PC->GetViewingInventory();
                if (!Viewing || Viewing == Inventory) PC->ServerActorCloseRemoteInventory(Inventory);
            }
        }
        UploadTerminal.ViewDistance.Restore();
        UploadTerminal.AccessDistance.Restore();
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

void RequestNoteXPBuff() { NoteBuffGate.Request(); }
void CancelNoteXPBuff() { NoteBuffCancelRequested.store(true); CancelNoteExperiment(); }
std::string GetNoteXPBuffStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(NoteBuffStatus); }
void RequestOpenUploadTerminal() { OpenUploadRequested.store(true); }
std::string GetUploadTerminalStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(UploadTerminalStatus); }
bool IsUploadTerminalActive() { return UploadTerminalActive.load(); }

void ResetWorldUtilities() {
    ResetNoteExperiment();
    OpenUploadRequested.store(false); NoteBuffGate.Reset(); NoteBuffCancelRequested.store(false);
    CloseUploadTerminal(true);
    NoteBuff = {};
}

void WorldUtilitiesTick(SDK::UWorld* World) {
    NoteExperimentTick(World, NoteBuff.Batch.Pending());
    auto* PC = GetLocalShooterController(World);
    auto* Human = IsUsableObject(PC) ? PC->GetPlayerCharacter() : nullptr;
    const bool Ready = IsUsableObject(World) && IsUsableObject(PC) && IsUsableObject(Human) && !Human->IsDead();
    if (UploadTerminalActive.load()) {
        auto* Inventory = static_cast<SDK::UPrimalInventoryComponent*>(UploadTerminal.Inventory.Get());
        auto* UI = static_cast<SDK::UPrimalUI*>(UploadTerminal.UI.Get());
        if (!Ready || !Inventory || UploadTerminal.World.Get() != World || UploadTerminal.Controller.Get() != PC) {
            CloseUploadTerminal(true);
            ExtensionStatus(UploadTerminalStatus, "人物、地图或方尖碑已变化，已释放上传面板");
        }
        else {
            auto* CurrentUI = PC->GetViewingInventoryUI();
            if (!UploadTerminal.Session.Keep(GetTickCount64(), UI && CurrentUI == UI,
                UploadPanelInViewport(UI), UI && UI->bIsClosing, CurrentUI && CurrentUI != UI)) {
                CloseUploadTerminal(false);
                ExtensionStatus(UploadTerminalStatus, "方尖碑面板已关闭；未自动重开，上传或取物结果以游戏为准");
            }
            else if (auto* Owner = Inventory->GetOwner(); IsUsableObject(Owner)) {
                const float Range = RemoteViewRange(Human->GetDistanceTo(Owner));
                UploadTerminal.ViewDistance.RaiseTo(Inventory, &Inventory->MaxRemoteInventoryViewingDistance, Range);
                UploadTerminal.AccessDistance.RaiseTo(Inventory, &Inventory->MaxInventoryAccessDistance, Range);
            }
        }
    }
    if (NoteBuff.Batch.Pending() && (!Ready || NoteBuff.World.Get() != World ||
        NoteBuff.Controller.Get() != PC || NoteBuff.Human.Get() != Human)) {
        NoteBuff = {};
        NoteBuffGate.Reset();
        ExtensionStatus(NoteBuffStatus, "人物或地图已变化，笔记解锁已停止");
    }
    if (NoteBuffCancelRequested.exchange(false)) {
        NoteBuffGate.Reset();
        NoteBuff = {};
        ExtensionStatus(NoteBuffStatus, "已停止笔记解锁");
    }

    if (OpenUploadRequested.exchange(false)) {
        if (!Ready) {
            ExtensionStatus(UploadTerminalStatus, "等待人物和地图加载完成后再打开");
        }
        else if (IsUploadTerminalActive()) {
            ExtensionStatus(UploadTerminalStatus, "已锁定当前方尖碑，请在已打开的面板操作");
        }
        else if (PC->IsViewingInventoryUI()) {
            ExtensionStatus(UploadTerminalStatus, "请先关闭当前容器界面，再打开上传终端");
        }
        else {
            SDK::UPrimalInventoryComponent* Best = nullptr;
            SDK::APrimalStructureItemContainer* BestActor = nullptr;
            double BestDistance = std::numeric_limits<double>::max();
            std::unordered_set<SDK::ULevel*> Levels;
            if (World->PersistentLevel) Levels.insert(World->PersistentLevel);
            if (World->Levels.IsValid() && World->Levels.Num() <= 4096)
                for (auto* Level : World->Levels) if (Level) Levels.insert(Level);
            for (auto* Level : Levels) {
                if (!IsUsableObject(Level) || !Level->Actors.IsValid() || Level->Actors.Num() > 200000) continue;
                for (auto* Actor : Level->Actors) {
                    if (!IsUsableObject(Actor) || Actor->bHidden ||
                        !Actor->IsA(SDK::APrimalStructureItemContainer::StaticClass())) continue;
                    auto* Container = static_cast<SDK::APrimalStructureItemContainer*>(Actor);
                    auto* Inventory = Container->MyInventoryComponent;
                    if (Container->bIsPreviewStructure || !IsUsableObject(Inventory) || !Inventory->bIsTributeInventory) continue;
                    std::string Identity = Container->GetDescriptiveName().ToString();
                    for (auto* Object : {static_cast<SDK::UObject*>(Container), static_cast<SDK::UObject*>(Inventory)}) {
                        for (auto* Class = Object->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
                            Identity += " " + Class->GetName();
                    }
                    if (!IsObeliskTerminalIdentity(Identity)) continue;
                    const double Distance = Human->GetDistanceTo(Actor);
                    if (std::isfinite(Distance) && Distance >= 0 && Distance < BestDistance) {
                        BestDistance = Distance; Best = Inventory; BestActor = Container;
                    }
                }
            }
            auto* HUD = PC->GetShooterHud();
            if (!Best) {
                ExtensionStatus(UploadTerminalStatus, "当前已加载范围没有方尖碑；请先让方尖碑终端进入地图加载范围");
            }
            else if (!IsUsableObject(HUD)) {
                ExtensionStatus(UploadTerminalStatus, "游戏界面尚未就绪");
            }
            else {
                ResetFreeCamera();
                // Release background sessions before opening the obelisk. Their
                // late close/sync requests must not race the new native panel.
                UploadTerminalActive.store(true);
                ReleaseAutomaticInventoryViews(PC);
                UploadTerminal.World = ExtensionObject(World);
                UploadTerminal.Controller = ExtensionObject(PC);
                UploadTerminal.Inventory = ExtensionObject(Best);
                UploadTerminal.Session.Open(GetTickCount64());
                const float Range = RemoteViewRange(BestDistance);
                UploadTerminal.ViewDistance.RaiseTo(Best, &Best->MaxRemoteInventoryViewingDistance, Range);
                UploadTerminal.AccessDistance.RaiseTo(Best, &Best->MaxInventoryAccessDistance, Range);
                // Use a real replicated terminal, never a fabricated local inventory.
                // The native interface owns confirmation, server selection and transfers.
                PC->ServerActorViewRemoteInventory(Best);
                PC->ServerRequestActorItems(Best, true, false);
                auto* UI = HUD->ShowInventory(Best);
                if (IsUsableObject(UI)) {
                    UploadTerminal.UI = ExtensionObject(UI);
                    g_MDX12::g_MenuState::g_isOpen = false;
                    ExtensionStatus(UploadTerminalStatus, FormatLocalized("已锁定方尖碑 {}（{:.0f}m）；在原生界面选择传服或存取物资",
                        BestActor->GetDescriptiveName().ToString(), BestDistance * 0.01));
                }
                else {
                    CloseUploadTerminal(false);
                    ExtensionStatus(UploadTerminalStatus, "终端界面未能打开，未执行上传或物资转移");
                }
            }
        }
    }

    const uint64_t Now = GetTickCount64();
    // Repeated clicks must not restart an in-flight round and resend its prefix.
    if (NoteBuffGate.Begin()) {
        NoteBuff = {};
        const bool ExperimentBusy = GetNoteExperimentView().Running;
        auto* Data = Ready && !ExperimentBusy ? SDK::UPrimalGameData::BPGetGameData() : nullptr;
        if (ExperimentBusy) {
            NoteBuffGate.Reset();
            ExtensionStatus(NoteBuffStatus, "笔记实验正在发送或观察，请先停止实验");
        }
        else if (!IsUsableObject(Data) || !Data->ExplorerNoteEntries.IsValid() ||
            Data->ExplorerNoteEntries.Num() <= 0 || Data->ExplorerNoteEntries.Num() > 16384) {
            NoteBuffGate.Reset();
            ExtensionStatus(NoteBuffStatus, "人物或笔记数据尚未就绪");
        }
        else {
            std::vector<int> Notes;
            Notes.reserve(Data->ExplorerNoteEntries.Num());
            for (int I = 0; I < Data->ExplorerNoteEntries.Num(); ++I) {
                if (!Data->ExplorerNoteEntries[I].bPreventXPBuff &&
                    !SDK::UPrimalGameData::LocalIsPerMapExplorerNoteUnlocked(I)) Notes.push_back(I);
            }
            NoteBuff.World = ExtensionObject(World); NoteBuff.Controller = ExtensionObject(PC); NoteBuff.Human = ExtensionObject(Human);
            NoteBuff.Batch.Start(std::move(Notes), Now);
            if (!NoteBuff.Batch.Pending()) {
                NoteBuffGate.Reset();
                ExtensionStatus(NoteBuffStatus, "没有尚未解锁且可提供经验 Buff 的笔记");
            }
        }
    }
    if (Ready && NoteBuff.Batch.Pending()) {
        const auto Indices = NoteBuff.Batch.NextBatch(Now);
        if (!Indices.empty()) {
            // bAvoidBuff=false requests the native XP buff. SavePlayerData=true
            // persists confirmed unlocks; there is no forced note-popup loop.
            for (const int Index : Indices)
                PC->ServerUnlockPerMapExplorerNote(Index, false, true);
            ExtensionStatus(NoteBuffStatus, FormatLocalized("已请求 {}/{} 条笔记{}；经验 Buff 以服务器实际授予为准",
                NoteBuff.Batch.Sent(), NoteBuff.Batch.Total(), NoteBuff.Batch.Pending() ? std::string{} : LanguageManager::Get().TranslateCopy("，本轮完成")));
            if (!NoteBuff.Batch.Pending()) NoteBuffGate.Reset();
        }
    }
}
