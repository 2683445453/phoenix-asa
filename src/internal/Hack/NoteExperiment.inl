// Included inside g_Hack, after GameplayExtensions.inl and before WorldUtilities.inl.
namespace {
    struct NoteExperimentCommand {
        bool Pending = false;
        NoteExperimentScope Scope = NoteExperimentScope::TidesOfFortune;
        int Selected = -1;
        uint64_t Generation = 0;
    };
    std::mutex NoteExperimentMutex;
    bool NoteExperimentOpen = false, NoteExperimentRefresh = false, NoteExperimentCancel = false;
    uint64_t NoteExperimentPublishedGeneration = 0;
    NoteExperimentCommand NoteExperimentQueued;
    NoteExperimentView NoteExperimentPublished;

    struct NoteExperimentRuntime {
        ExtensionObject World, Controller, Human, Data;
        const SDK::FExplorerNoteEntry* EntryBuffer = nullptr;
        int EntryCount = 0;
        uint64_t Generation = 0, Started = 0, ObserveStarted = 0, NextSample = 0;
        NoteRequestBatch Batch;
        NoteExperimentView View;
        std::ofstream Log;
    } NoteExperiment;

    // The engine owns this output buffer. Reusing the parameter storage lets the
    // next native call recycle it with the same allocator; never free/delete it.
    SDK::Params::PrimalCharacter_GetAllBuffs NoteExperimentBuffScratch{};

    void PublishNoteExperiment() {
        std::scoped_lock Lock(NoteExperimentMutex);
        NoteExperimentPublished = NoteExperiment.View;
        NoteExperimentPublishedGeneration = NoteExperiment.Generation;
    }

    SDK::UFunction* NoteExperimentFunction(SDK::UObject* Object, const char* Owner,
        const char* Name, int Size) {
        if (!IsUsableObject(Object) || !IsUsableObject(Object->Class)) return nullptr;
        auto* Function = Object->Class->GetFunction(Owner, Name);
        return IsUsableObject(Function) && Function->Size == Size ? Function : nullptr;
    }

    bool NoteExperimentParameter(SDK::UFunction* Function, const char* Name,
        const char* Type, int Offset, int Size) {
        if (!Function) return false;
        auto* Property = FindReflectedProperty(Function, Name);
        return Property && Property->ClassPrivate && Property->ArrayDim == 1 && Property->Offset == Offset &&
            Property->ElementSize == Size &&
            EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), Type);
    }

    std::string NoteExperimentText(std::string Text) {
        for (auto& C : Text) if (C == '\n' || C == '\r' || C == '\t') C = ' ';
        if (Text.size() > 1024) Text.resize(1024);
        return Text;
    }

    std::string NoteExperimentName(SDK::UPrimalGameData* Data, int Index) {
        SDK::Params::PrimalGameData_GetExplorerNoteName Params{};
        auto* Function = NoteExperimentFunction(Data, "PrimalGameData", "GetExplorerNoteName", sizeof(Params));
        if (Function) {
            Params.NoteIndex = Index;
            Data->ProcessEvent(Function, &Params);
            const auto Name = NoteExperimentText(Params.ReturnValue.ToString());
            if (!Name.empty() && Name != "None") return Name;
        }
        const auto& Description = Data->ExplorerNoteEntries[Index].ExplorerNoteDescription;
        if (Description.IsValid() && Description.Num() <= 8192)
            return NoteExperimentText(Description.ToString());
        return "Note " + std::to_string(Index);
    }

    NoteExperimentSample CaptureNoteExperiment(SDK::AShooterCharacter* Human) {
        NoteExperimentSample Sample;
        if (!IsUsableObject(Human) || Human->IsDead()) return Sample;
        Sample.Valid = true;
        auto* Status = Human->MyCharacterStatusComponent;
        if (IsUsableObject(Status) && std::isfinite(Status->ReplicatedExperiencePoints)) {
            Sample.HasExperience = true;
            Sample.Experience = Status->ReplicatedExperiencePoints;
        }
        std::vector<SDK::APrimalBuff*> Buffs;
        SDK::UObject** Raw = nullptr;
        int Count = 0;
        if (ReadReflectedObjectArray(Human, "MyBuffs", &Raw, &Count) && Count <= 4096) {
            Sample.HasBuffs = true;
            for (int I = 0; I < Count; ++I)
                if (IsUsableObject(Raw[I]) && Raw[I]->IsA(SDK::APrimalBuff::StaticClass()))
                    Buffs.push_back(static_cast<SDK::APrimalBuff*>(Raw[I]));
        }
        else {
            auto* Function = NoteExperimentFunction(Human, "PrimalCharacter", "GetAllBuffs",
                sizeof(NoteExperimentBuffScratch));
            if (NoteExperimentParameter(Function, "AllBuffs", "ArrayProperty", 0, 16)) {
                Human->ProcessEvent(Function, &NoteExperimentBuffScratch);
                const auto& Native = NoteExperimentBuffScratch.AllBuffs;
                if (Native.Num() == 0 || (Native.IsValid() && Native.Num() <= 4096)) {
                    Sample.HasBuffs = true;
                    for (int I = 0; I < Native.Num(); ++I) Buffs.push_back(Native[I]);
                }
            }
        }
        std::unordered_set<int> Seen;
        for (auto* Buff : Buffs) {
            if (!IsUsableObject(Buff) || !IsUsableObject(Buff->Class) || Buff->bDeactivated ||
                !Seen.insert(Buff->Index).second) continue;
            NoteExperimentBuff Item;
            Item.Name = NoteExperimentText(Buff->Class->GetName());
            Item.ObjectIndex = Buff->Index;
            if (std::isfinite(Buff->DeactivateAfterTime) && Buff->DeactivateAfterTime > 0) {
                SDK::Params::PrimalBuff_GetTimeLeft Params{};
                auto* Function = NoteExperimentFunction(Buff, "PrimalBuff", "GetTimeLeft", sizeof(Params));
                if (NoteExperimentParameter(Function, "ReturnValue", "FloatProperty", 0, 4)) {
                    Buff->ProcessEvent(Function, &Params);
                    Item.RemainingSeconds = Params.ReturnValue;
                    Item.HasRemainingTime = std::isfinite(Params.ReturnValue) && Params.ReturnValue >= 0;
                }
                else {
                    SDK::Params::PrimalBuff_GetRemainingTime Remaining{};
                    Function = NoteExperimentFunction(Buff, "PrimalBuff", "GetRemainingTime", sizeof(Remaining));
                    if (NoteExperimentParameter(Function, "DeactivateAfterTimeOverride", "FloatProperty", 0, 4) &&
                        NoteExperimentParameter(Function, "ReturnValue", "FloatProperty", 4, 4)) {
                        Remaining.DeactivateAfterTimeOverride = -1.0f;
                        Buff->ProcessEvent(Function, &Remaining);
                        Item.RemainingSeconds = Remaining.ReturnValue;
                        Item.HasRemainingTime = std::isfinite(Remaining.ReturnValue) && Remaining.ReturnValue >= 0;
                    }
                }
            }
            Sample.Buffs.push_back(std::move(Item));
        }
        std::sort(Sample.Buffs.begin(), Sample.Buffs.end(), [](const auto& A, const auto& B) {
            return A.Name == B.Name ? A.ObjectIndex < B.ObjectIndex : A.Name < B.Name;
        });
        return Sample;
    }

    void LogNoteExperimentSample(const char* Event, const NoteExperimentSample& Sample, uint64_t Now) {
        auto& Log = NoteExperiment.Log;
        Log << "sample " << Event << " elapsed_ms=" << Now - NoteExperiment.Started
            << " valid=" << Sample.Valid << " has_xp=" << Sample.HasExperience
            << " replicated_xp=" << Sample.Experience << " has_buffs=" << Sample.HasBuffs
            << " buff_count=" << Sample.Buffs.size() << '\n';
        for (const auto& Buff : Sample.Buffs)
            Log << "buff class=" << std::quoted(Buff.Name) << " object_index=" << Buff.ObjectIndex
                << " has_remaining=" << Buff.HasRemainingTime << " remaining_seconds="
                << (Buff.HasRemainingTime ? Buff.RemainingSeconds : 0) << '\n';
        Log.flush();
    }

    bool OpenNoteExperimentLog(uint64_t Now, NoteExperimentScope Scope) {
        HMODULE Module = nullptr;
        wchar_t Path[32768]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&NoteExperiment), &Module)) return false;
        const auto Length = GetModuleFileNameW(Module, Path, 32768);
        if (!Length || Length >= 32768) return false;
        auto Directory = std::filesystem::path(Path).parent_path() / L"logs";
        std::error_code Error;
        std::filesystem::create_directories(Directory, Error);
        if (Error) return false;
        SYSTEMTIME Time{};
        GetLocalTime(&Time);
        auto Destination = Directory / std::format("note-experiment-{:04}{:02}{:02}-{:02}{:02}{:02}-{}-{}-{}.txt",
            Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond,
            GetCurrentProcessId(), Now, NoteExperiment.View.Round);
        NoteExperiment.Log.clear();
        NoteExperiment.Log.open(Destination, std::ios::out | std::ios::trunc);
        if (!NoteExperiment.Log) return false;
        NoteExperiment.Log.imbue(std::locale::classic());
        const auto Utf8 = Destination.u8string();
        NoteExperiment.View.LogPath.assign(reinterpret_cast<const char*>(Utf8.data()), Utf8.size());
        NoteExperiment.Log << std::setprecision(9)
            << "client_observation_only; unlock bits, XP and buff changes do not prove server acceptance or causation\n"
            << "rpc=ServerUnlockPerMapExplorerNote bAvoidBuff=false SavePlayerData=true\n"
            << "round=" << NoteExperiment.View.Round << " scope=" << static_cast<int>(Scope)
            << " generation=" << NoteExperiment.Generation << " world=" << NoteExperiment.World.Key()
            << " controller=" << NoteExperiment.Controller.Key() << " human=" << NoteExperiment.Human.Key()
            << " data=" << NoteExperiment.Data.Key() << '\n';
        return NoteExperiment.Log.good();
    }

    void FinishNoteExperiment(const char* Reason, SDK::AShooterCharacter* Human, bool SameContext, uint64_t Now) {
        auto& View = NoteExperiment.View;
        if (View.Running) {
            if (SameContext) {
                View.After = CaptureNoteExperiment(Human);
                for (auto& Attempt : View.Attempts) {
                    Attempt.UnlockedAfter = SDK::UPrimalGameData::LocalIsPerMapExplorerNoteUnlocked(Attempt.Index);
                    Attempt.HasAfter = true;
                    if (Attempt.Index >= 0 && Attempt.Index < static_cast<int>(View.Entries.size()))
                        View.Entries[Attempt.Index].Unlocked = Attempt.UnlockedAfter;
                }
            }
            LogNoteExperimentSample(SameContext ? "after" : "retained_last_observation", View.After, Now);
            for (const auto& Attempt : View.Attempts)
                NoteExperiment.Log << "attempt id=" << Attempt.Index << " name=" << std::quoted(Attempt.Name)
                    << " unlocked_before=" << Attempt.UnlockedBefore << " has_after=" << Attempt.HasAfter
                    << " unlocked_after=" << Attempt.UnlockedAfter << '\n';
            NoteExperiment.Log << "end reason=" << std::quoted(Reason) << " sent=" << View.Sent
                << " total=" << View.Total << " same_context=" << SameContext << '\n';
            NoteExperiment.Log.flush();
            NoteExperiment.Log.close();
        }
        View.Running = View.Observing = false;
        View.Status = Reason;
        NoteExperiment.Batch.Cancel();
    }

    void BuildNoteExperimentCatalog(SDK::UWorld* World, SDK::AShooterPlayerController* PC,
        SDK::AShooterCharacter* Human, SDK::UPrimalGameData* Data) {
        auto& Runtime = NoteExperiment;
        Runtime.World = ExtensionObject(World); Runtime.Controller = ExtensionObject(PC);
        Runtime.Human = ExtensionObject(Human); Runtime.Data = ExtensionObject(Data);
        Runtime.EntryBuffer = Data->ExplorerNoteEntries.GetDataPtr();
        Runtime.EntryCount = Data->ExplorerNoteEntries.Num();
        ++Runtime.Generation;
        Runtime.View.CatalogGeneration = Runtime.Generation;
        Runtime.View.Entries.clear();
        Runtime.View.Entries.reserve(Runtime.EntryCount);
        for (int I = 0; I < Runtime.EntryCount; ++I) {
            const auto& Native = Data->ExplorerNoteEntries[I];
            Runtime.View.Entries.push_back({I, NoteExperimentName(Data, I),
                SDK::UPrimalGameData::LocalIsPerMapExplorerNoteUnlocked(I), Native.bPreventXPBuff != 0,
                Native.ExplorerNoteType == 16 && Native.bIsGenesis1});
        }
        Runtime.View.Ready = true;
    }

    void StartNoteExperiment(const NoteExperimentCommand& Command, SDK::AShooterPlayerController* PC,
        SDK::AShooterCharacter* Human, uint64_t Now) {
        auto& View = NoteExperiment.View;
        std::vector<int> Indices;
        for (const auto& Entry : View.Entries) {
            const bool Include = Command.Scope == NoteExperimentScope::TidesOfFortune ? Entry.TidesOfFortune && !Entry.PreventXPBuff :
                Command.Scope == NoteExperimentScope::AllXPNotes ? !Entry.PreventXPBuff : Entry.Index == Command.Selected;
            if (Include) Indices.push_back(Entry.Index); // Deliberately includes already-unlocked notes.
        }
        if (Indices.empty()) { View.Status = "没有符合范围的笔记"; return; }
        if (!NoteExperimentFunction(PC, "ShooterPlayerController", "ServerUnlockPerMapExplorerNote",
            sizeof(SDK::Params::ShooterPlayerController_ServerUnlockPerMapExplorerNote))) {
            View.Status = "笔记请求接口校验失败，实验未发送请求"; return;
        }
        ++View.Round;
        View.Sent = 0; View.Total = static_cast<int>(Indices.size());
        View.Attempts.clear(); View.Before = {}; View.After = {}; View.LogPath.clear();
        NoteExperiment.Started = Now;
        if (!OpenNoteExperimentLog(Now, Command.Scope)) {
            NoteExperiment.Log.close(); View.Status = "日志无法创建，实验未发送请求"; return;
        }
        View.Before = CaptureNoteExperiment(Human);
        LogNoteExperimentSample("before", View.Before, Now);
        for (const int Index : Indices) {
            const auto& Entry = View.Entries[Index];
            NoteExperiment.Log << "plan id=" << Index << " name=" << std::quoted(Entry.Name)
                << " catalog_unlocked=" << Entry.Unlocked << " prevent_xp_buff=" << Entry.PreventXPBuff
                << " tides=" << Entry.TidesOfFortune << '\n';
        }
        NoteExperiment.Log.flush();
        if (!NoteExperiment.Log.good()) {
            NoteExperiment.Log.close(); View.Status = "日志无法创建，实验未发送请求"; return;
        }
        NoteExperiment.Batch.Start(std::move(Indices), Now);
        View.Running = true; View.Observing = false;
        View.Status = "正在请求笔记；随后观察本地状态 5 秒";
    }
}

void SetNoteExperimentOpen(bool Open) {
    std::scoped_lock Lock(NoteExperimentMutex);
    if (Open && !NoteExperimentOpen) NoteExperimentRefresh = true;
    NoteExperimentOpen = Open;
    if (!Open) { NoteExperimentCancel = true; NoteExperimentQueued = {}; }
}
void RefreshNoteExperiment() {
    std::scoped_lock Lock(NoteExperimentMutex);
    if (NoteExperimentOpen) NoteExperimentRefresh = true;
}
void RequestNoteExperiment(NoteExperimentScope Scope, int SelectedIndex, std::uint64_t CatalogGeneration) {
    std::scoped_lock Lock(NoteExperimentMutex);
    if (!NoteExperimentOpen || NoteExperimentCancel || !NoteExperimentPublished.Ready ||
        NoteExperimentPublished.Running || NoteExperimentQueued.Pending) return;
    // Bind the list actually displayed by the UI, not a newer list published
    // between rendering that list and the click reaching this function.
    if (CatalogGeneration && CatalogGeneration != NoteExperimentPublishedGeneration) return;
    if (Scope != NoteExperimentScope::TidesOfFortune && Scope != NoteExperimentScope::AllXPNotes &&
        Scope != NoteExperimentScope::Selected) return;
    NoteExperimentQueued = {true, Scope, SelectedIndex, NoteExperimentPublishedGeneration};
}
void CancelNoteExperiment() {
    std::scoped_lock Lock(NoteExperimentMutex);
    NoteExperimentCancel = true; NoteExperimentQueued = {};
}
NoteExperimentView GetNoteExperimentView() {
    std::scoped_lock Lock(NoteExperimentMutex);
    auto View = NoteExperimentPublished;
    if (NoteExperimentQueued.Pending) {
        View.Running = true; View.Observing = false; View.Status = "实验已排队";
    }
    return View;
}

namespace {
    void ResetNoteExperiment() {
        FinishNoteExperiment("实验已停止，已保留当前观测", nullptr, false, GetTickCount64());
        NoteExperiment.World = {}; NoteExperiment.Controller = {}; NoteExperiment.Human = {}; NoteExperiment.Data = {};
        NoteExperiment.EntryBuffer = nullptr; NoteExperiment.EntryCount = 0;
        ++NoteExperiment.Generation;
        NoteExperiment.View.Ready = false;
        NoteExperiment.View.Entries.clear();
        std::scoped_lock Lock(NoteExperimentMutex);
        NoteExperimentQueued = {}; NoteExperimentCancel = false;
        NoteExperimentRefresh = NoteExperimentOpen;
        NoteExperimentPublished = NoteExperiment.View;
        NoteExperimentPublishedGeneration = NoteExperiment.Generation;
    }

    void NoteExperimentTick(SDK::UWorld* World, bool OrdinaryNotesBusy) {
        auto& Runtime = NoteExperiment;
        auto& View = Runtime.View;
        bool Open, Cancel, Refresh;
        NoteExperimentCommand Command;
        {
            std::scoped_lock Lock(NoteExperimentMutex);
            Open = NoteExperimentOpen; Cancel = NoteExperimentCancel; Refresh = NoteExperimentRefresh;
            NoteExperimentCancel = NoteExperimentRefresh = false;
            Command = NoteExperimentQueued; NoteExperimentQueued = {};
            if (Command.Pending) NoteExperimentPublished.Running = true;
        }
        const uint64_t Now = GetTickCount64();
        if (!Open && !View.Running && !Command.Pending) return;
        auto* PC = IsUsableObject(World) ? GetLocalShooterController(World) : nullptr;
        auto* Human = IsUsableObject(PC) ? PC->GetPlayerCharacter() : nullptr;
        auto* Data = IsUsableObject(Human) && !Human->IsDead() && IsUsableObject(PC->PlayerState)
            ? SDK::UPrimalGameData::BPGetGameData() : nullptr;
        const bool Ready = IsUsableObject(Data) && Data->ExplorerNoteEntries.IsValid() &&
            Data->ExplorerNoteEntries.Num() > 0 && Data->ExplorerNoteEntries.Num() <= 16384;
        const bool SameContext = Ready && Runtime.World.Get() == World && Runtime.Controller.Get() == PC &&
            Runtime.Human.Get() == Human && Runtime.Data.Get() == Data &&
            Runtime.EntryBuffer == Data->ExplorerNoteEntries.GetDataPtr() && Runtime.EntryCount == Data->ExplorerNoteEntries.Num();
        if (View.Running && !SameContext)
            FinishNoteExperiment("人物、地图或笔记数据变化，实验已停止", nullptr, false, Now);
        if (Cancel || !Open) {
            FinishNoteExperiment(Open ? "实验已停止，已保留当前观测" : "实验窗口已关闭，实验已停止",
                Human, SameContext, Now);
            Command.Pending = false;
            if (!Open) { View.Ready = false; PublishNoteExperiment(); return; }
        }
        if (!Ready) {
            if (Runtime.EntryBuffer) ++Runtime.Generation;
            Runtime.EntryBuffer = nullptr; Runtime.EntryCount = 0;
            View.Ready = false; View.Entries.clear();
            if (!View.Running && !Cancel) View.Status = "等待人物与笔记数据就绪";
            PublishNoteExperiment(); return;
        }
        if (!SameContext || (Refresh && !View.Running)) {
            BuildNoteExperimentCatalog(World, PC, Human, Data);
            if (View.Status.empty()) View.Status = "实验待命；不会跳过已解锁笔记。";
        }
        if (OrdinaryNotesBusy && View.Running)
            FinishNoteExperiment("普通笔记请求正在运行，实验已停止", Human, true, Now);
        if (Command.Pending) {
            if (OrdinaryNotesBusy) View.Status = "普通笔记请求正在运行，实验未启动";
            else if (Command.Generation != Runtime.Generation) View.Status = "笔记列表已变化，请刷新后重试";
            else if (!View.Running) StartNoteExperiment(Command, PC, Human, Now);
        }
        if (View.Running && !View.Observing) {
            const auto Batch = Runtime.Batch.NextBatch(Now);
            for (const int Index : Batch) {
                bool Stop;
                { std::scoped_lock Lock(NoteExperimentMutex); Stop = !NoteExperimentOpen || NoteExperimentCancel; }
                if (Stop) {
                    FinishNoteExperiment("实验已停止，已保留当前观测", Human, true, Now); break;
                }
                NoteExperimentAttempt Attempt;
                Attempt.Index = Index; Attempt.Name = View.Entries[Index].Name;
                Attempt.UnlockedBefore = SDK::UPrimalGameData::LocalIsPerMapExplorerNoteUnlocked(Index);
                PC->ServerUnlockPerMapExplorerNote(Index, false, true);
                View.Attempts.push_back(std::move(Attempt));
                View.Sent = static_cast<int>(View.Attempts.size());
                Runtime.Log << "sent elapsed_ms=" << Now - Runtime.Started << " id=" << Index
                    << " unlocked_before=" << View.Attempts.back().UnlockedBefore << '\n';
            }
            if (!Batch.empty()) Runtime.Log.flush();
            if (View.Running && !Runtime.Log.good())
                FinishNoteExperiment("日志写入失败，实验已停止", Human, true, Now);
            if (View.Running && !Runtime.Batch.Pending()) {
                View.Observing = true; Runtime.ObserveStarted = GetTickCount64(); Runtime.NextSample = Runtime.ObserveStarted;
                View.Status = "请求已发完；正在观察本地状态 5 秒";
            }
        }
        if (View.Running && View.Observing && Now >= Runtime.NextSample) {
            View.After = CaptureNoteExperiment(Human);
            LogNoteExperimentSample("observe", View.After, Now);
            Runtime.NextSample = Now + 250; // No catch-up sampling after a slow frame.
            if (!Runtime.Log.good()) FinishNoteExperiment("日志写入失败，实验已停止", Human, true, Now);
            else if (Now - Runtime.ObserveStarted >= 5000)
                FinishNoteExperiment("本轮观察完成；本地变化不代表服务器确认或因果", Human, true, Now);
        }
        PublishNoteExperiment();
    }
}
