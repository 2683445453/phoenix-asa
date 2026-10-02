// Included inside g_Hack after the shared inventory/object helpers.
namespace {
    struct ExtensionObject {
        SDK::UObject* Pointer = nullptr;
        int32_t Index = -1, Serial = -1;
        ExtensionObject() = default;
        explicit ExtensionObject(SDK::UObject* Object) {
            if (Object && TryGetLiveObjectSerial(Object, Object->Index, &Serial)) {
                Pointer = Object; Index = Object->Index;
            }
        }
        SDK::UObject* Get() const {
            int32_t Current = -1;
            return TryGetLiveObjectSerial(Pointer, Index, &Current) && Current == Serial &&
                IsUsableObject(Pointer) ? Pointer : nullptr;
        }
        uint64_t Key() const { return (uint64_t(uint32_t(Serial)) << 32) | uint32_t(Index); }
    };
    struct FloatTuning {
        ExtensionObject Owner;
        size_t Offset = 0;
        float Original = 0, Applied = 0;
        void RaiseTo(SDK::UObject* Object, float* Value, float Minimum) {
            if (!Object || !Value || !std::isfinite(*Value) || !std::isfinite(Minimum) || Minimum <= 0) {
                Restore(); return;
            }
            const auto NewOffset = reinterpret_cast<uint8_t*>(Value) - reinterpret_cast<uint8_t*>(Object);
            if (Owner.Get() != Object || Offset != NewOffset) {
                Restore(); Owner = ExtensionObject(Object); Offset = NewOffset;
                Original = Applied = *Value;
            }
            if (!Owner.Get()) return;
            if (*Value != Applied) Original = *Value;
            *Value = Applied = std::max(Original, Minimum);
        }
        void Restore() {
            if (auto* Object = Owner.Get()) {
                auto* Value = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(Object) + Offset);
                if (*Value == Applied) *Value = Original;
            }
            Owner = {};
        }
        void Update(SDK::UObject* Object, float* Value, float Multiplier, bool Divide = false) {
            if (Owner.Get() != Object || (Object && Offset != reinterpret_cast<uint8_t*>(Value) - reinterpret_cast<uint8_t*>(Object))) {
                Restore();
                if (Object && Value && std::isfinite(*Value) && *Value > 0) {
                    Owner = ExtensionObject(Object);
                    Offset = reinterpret_cast<uint8_t*>(Value) - reinterpret_cast<uint8_t*>(Object);
                    Original = Applied = *Value;
                }
            }
            if (!Owner.Get() || !Value || !std::isfinite(*Value) || *Value <= 0) { Restore(); return; }
            // Preserve native buff/equipment changes instead of compounding our multiplier.
            if (*Value != Applied) Original = *Value;
            // The multiplier is bounded by EffectiveFireRateMultiplier. A fixed
            // 20 ms floor would silently cap a 0.7 s weapon at 35x instead of 100x.
            Applied = Divide ? Original / Multiplier : Original * Multiplier;
            *Value = Applied;
        }
        void Set(SDK::UObject* Object, float* Value, float Seconds) {
            if (Owner.Get() != Object || (Object && Offset != reinterpret_cast<uint8_t*>(Value) - reinterpret_cast<uint8_t*>(Object))) {
                Restore();
                if (Object && Value && std::isfinite(*Value) && *Value > 0) {
                    Owner = ExtensionObject(Object);
                    Offset = reinterpret_cast<uint8_t*>(Value) - reinterpret_cast<uint8_t*>(Object);
                    Original = Applied = *Value;
                }
            }
            if (!Owner.Get() || !Value || !std::isfinite(*Value) || *Value <= 0) { Restore(); return; }
            if (*Value != Applied) Original = *Value;
            *Value = Applied = Seconds;
        }
    };
    FloatTuning FireIntervalTuning;
    ExtensionObject RapidCrossbowWeapon;
    uint64_t NextRapidCrossbowShot = 0;
    void ResetRapidCrossbow() {
        RapidCrossbowWeapon = {};
        NextRapidCrossbowShot = 0;
    }
    std::mutex ExtensionStatusMutex;
    std::string LootStatus = "自动吸包未启用";
    std::string LootPauseReason;
    std::string FireRateStatus = "射速修改未启用";
    bool ExtensionClassMatches(SDK::UObject* Object, const char* Name) {
        if (!IsUsableObject(Object)) return false;
        for (auto* Class = Object->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
            if (Class->GetName() == Name) return true;
        return false;
    }
    void ExtensionStatus(std::string& Destination, std::string Text) {
        std::scoped_lock Lock(ExtensionStatusMutex); Destination = std::move(Text);
    }
    float ExtensionLimit(float Value, float Low, float High, float Fallback) {
        return std::isfinite(Value) ? std::clamp(Value, Low, High) : Fallback;
    }
    // Keep the native view/transfer path, but make its local access limits match
    // the configured search radius. On a remote server these fields only affect
    // the client; the server still validates its own distance, trace and permissions.
    struct LootAccessTuning {
        FloatTuning ViewDistance, AccessDistance;
        ExtensionObject TraceInventory;
        bool OriginalPreventTrace = false;
        void Restore() {
            if (auto* Object = TraceInventory.Get()) {
                auto* Inventory = static_cast<SDK::UPrimalInventoryComponent*>(Object);
                if (Inventory->bPreventInventoryViewTrace)
                    Inventory->bPreventInventoryViewTrace = OriginalPreventTrace;
            }
            TraceInventory = {};
            ViewDistance.Restore(); AccessDistance.Restore();
        }
        void Apply(SDK::UPrimalInventoryComponent* Inventory, float RangeMeters) {
            if (TraceInventory.Get() != Inventory) {
                Restore();
                TraceInventory = ExtensionObject(Inventory);
                if (!TraceInventory.Get()) return;
                OriginalPreventTrace = Inventory->bPreventInventoryViewTrace;
            }
            const float DistanceCm = RangeMeters * 100.0f;
            ViewDistance.RaiseTo(Inventory, &Inventory->MaxRemoteInventoryViewingDistance, DistanceCm);
            AccessDistance.RaiseTo(Inventory, &Inventory->MaxInventoryAccessDistance, DistanceCm);
            Inventory->bPreventInventoryViewTrace = true;
        }
    } LootAccess;
    struct ScopedLootAccessTuning : LootAccessTuning {
        ~ScopedLootAccessTuning() { Restore(); }
    };
    struct LootRuntime {
        struct RetryEntry {
            std::chrono::steady_clock::time_point Until{};
            unsigned Failures = 0;
        };
        ExtensionObject World, Player, Controller, Pawn, Target, PendingEgg, ClosingTarget;
        std::chrono::steady_clock::time_point Next{}, CloseUntil{};
        std::unordered_map<uint64_t, RetryEntry> Retry;
        AutoLootTransferSession Transfer;
        int EggBeforeQuantity = 0;
        std::chrono::steady_clock::time_point EggRequested{};
    } Loot;
    void CloseLootTarget() {
        auto* PC = static_cast<SDK::AShooterPlayerController*>(Loot.Controller.Get());
        auto* Inventory = static_cast<SDK::UPrimalInventoryComponent*>(Loot.Target.Get());
        if (PC && Inventory) {
            // Closing a remote view is asynchronous. Do not mistake its late
            // replicated echo for somebody else's manually opened container.
            Loot.ClosingTarget = Loot.Target;
            Loot.CloseUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            PC->ServerActorCloseRemoteInventory(Inventory);
        }
        LootAccess.Restore();
        Loot.Target = {}; Loot.Transfer = {};
    }
    bool LootContainerEnabled(SDK::APrimalStructureItemContainer* Container) {
        if (!Container || Container->bHidden) return false;
        for (auto* Class = Container->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
            if (AutoLootBeaverDamAllowed(Class->GetName())) return g_Config::bAutoLootBeaverDams;
        for (auto* Class = Container->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
            if (AutoLootSpecialContainerAllowed(Class->GetName())) return g_Config::bAutoLootSupplyCrates;
        if (Container->IsA(SDK::APrimalStructureItemContainer_SupplyCrate::StaticClass()))
            return g_Config::bAutoLootSupplyCrates;
        if (!g_Config::bAutoLootBags) return false;
        if (Container->bUseDeathCacheCharacterID || Container->DeathCacheCreationTime > 0) return true;
        for (auto* Class = Container->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct)) {
            const auto Name = Class->GetName();
            if (Name.find("DeathItemCache") != std::string::npos ||
                Name.find("DroppedItemCache") != std::string::npos) return true;
        }
        return false;
    }
}

std::string GetWeaponFireRateStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(FireRateStatus); }
std::string GetAutoLootStatus() {
    std::scoped_lock Lock(ExtensionStatusMutex);
    const auto LastStatus = LanguageManager::Get().TranslateCopy(LootStatus);
    return LootPauseReason.empty() ? LastStatus : LanguageManager::Get().TranslateCopy(LootPauseReason) +
        "\n" + LanguageManager::Get().TranslateCopy("暂停前状态：") + LastStatus;
}

void ResetGameplayExtensions() {
    ResetSpiderZiplineExperiment();
    ResetPlacementExperiment();
    ResetTekEquipment();
    ResetFeatureExpansion();
    ResetInstantThrowables();
    ResetInstantReload();
    ResetAutoDeposit();
    ResetBackpackTools();
    PhoenixSession::Publish({});
    FireIntervalTuning.Restore();
    ResetRapidCrossbow();
    CloseLootTarget(); Loot = {};
}

void GameplayExtensionsTick(SDK::UWorld* World) {
    FeatureExpansionTick(World);
    SpiderZiplineExperimentTick(World);
    TekEquipmentTick(World);
    PlacementExperimentTick(World);
    // Called only by the serialized local camera/viewport callback.
    auto* PC = GetLocalShooterController(World);
    auto* Pawn = IsUsableObject(PC) && IsUsableObject(PC->Pawn) ? PC->Pawn : nullptr;
    auto* Mount = Pawn && Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())
        ? static_cast<SDK::APrimalDinoCharacter*>(Pawn) : nullptr;
    auto* Rider = Mount ? Mount->Rider.Get() : nullptr;
    if (Rider) {
        const auto& Ref = Mount->Rider;
        int32_t Serial = -1;
        if (!TryGetLiveObjectSerial(Rider, Ref.ObjectIndex, &Serial) ||
            Serial != Ref.ObjectSerialNumber || !IsUsableObject(Rider)) Rider = nullptr;
    }
    // Prefer the serial-validated rider, then a controller accessor whose
    // result is verified to be a survivor (a live possessed dino is not enough).
    auto* Human = GetLocalConsumableCharacter(World);
    auto* HumanMount = IsUsableObject(Human) ? Human->MountedDino.Get() : nullptr;
    if (HumanMount) {
        const auto& Ref = Human->MountedDino;
        int32_t Serial = -1;
        if (!TryGetLiveObjectSerial(HumanMount, Ref.ObjectIndex, &Serial) ||
            Serial != Ref.ObjectSerialNumber || !IsUsableObject(HumanMount)) HumanMount = nullptr;
    }
    DWORD ForegroundPID = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &ForegroundPID);
    const bool InteractionReady = !IsUploadTerminalActive() && !IsAutomaticDepositActive() && IsUsableObject(PC) && IsUsableObject(Human) && Pawn &&
        !Human->IsDead() && !g_MDX12::g_MenuState::g_isOpen && !PC->bShowMouseCursor &&
        !PC->IsViewingInventoryUI() && ForegroundPID == GetCurrentProcessId();
    // While riding, the possessed pawn is the dino but CurrentWeapon belongs
    // to its rider. Keep the same weapon path active for that rider.
    const bool Ready = InteractionReady && (Pawn == Human ||
        (Mount && !Mount->IsDead() &&
            (Rider == Human || Human->MountedDino.Get() == Mount)));
    auto* LootViewing = IsUsableObject(PC) ? PC->GetViewingInventory() : nullptr;
    const bool ClosePending = std::chrono::steady_clock::now() < Loot.CloseUntil;
    auto* OwnedView = LootViewing && ClosePending && LootViewing == Loot.ClosingTarget.Get()
        ? LootViewing : Loot.Target.Get();
    const bool LootInteractionReady = !IsUploadTerminalActive() && !IsAutomaticDepositActive() &&
        IsUsableObject(PC) && IsUsableObject(Human) && Pawn && !Human->IsDead() &&
        !g_MDX12::g_MenuState::g_isOpen && ForegroundPID == GetCurrentProcessId() &&
        !AutoLootInventoryUIBlocks(PC->IsViewingInventoryUI(), PC->bShowMouseCursor,
            LootViewing, OwnedView);
    const bool LootReady = LootInteractionReady && !IsHusbandryInventoryActive() && (!Mount || !Mount->IsDead()) &&
        AutoLootControlsHuman(Pawn, Human, Mount, Rider, HumanMount);
    auto* Weapon = IsUsableObject(Human) && IsUsableObject(Human->CurrentWeapon) ? Human->CurrentWeapon : nullptr;
    // Share one owner with the general fire-rate override: two patches of the
    // same field would otherwise compound their baselines and restore incorrectly.
    const bool FastTekGrenade = g_Config::bInstantThrowables && Weapon &&
        Weapon->IsA(SDK::APrimalWeaponGrenade::StaticClass()) &&
        ExtensionClassMatches(Weapon, "WeapTekGrenade_C");
    if ((g_Config::bWeaponFireRate || g_Config::bCrossbowHoldRapidFire || FastTekGrenade) && Weapon &&
        (Weapon->IsA(SDK::AShooterWeapon_Instant::StaticClass()) || Weapon->IsA(SDK::AShooterWeapon_Projectile::StaticClass()))) {
        const bool Authority = Weapon->Role == SDK::ENetRole::ROLE_Authority;
        const bool RapidCrossbow = g_Config::bCrossbowHoldRapidFire && Authority &&
            ClassifyFireRateWeapon(Weapon) == FireRateWeaponKind::Crossbow &&
            Ready && !IsFreeCameraActive();
        const bool RemoteTekSniper = !Authority && ExtensionClassMatches(Weapon, "WeapTekSniper_C");
        // The tested server rejects projectile RPCs below 0.67 * its interval.
        // Leave 10 ms at the stock 1 s interval for frame/network timing. This
        // changes client cadence only; reload/animation can still be limiting.
        const float Multiplier = FastTekGrenade ? (1.0f / 0.68f) :
            EffectiveFireRateMultiplier(Authority, g_Config::WeaponFireRateMultiplier);
        const bool TypeAllowed = RapidCrossbow || FastTekGrenade ||
            (g_Config::bWeaponFireRate && FireRateKindEnabled(ClassifyFireRateWeapon(Weapon)));
        auto* Interval = GetReflectedNestedFloatAddress(Weapon, "WeaponConfig", "TimeBetweenShots");
        if (RapidCrossbow && Interval && std::isfinite(*Interval) && *Interval > 0)
            FireIntervalTuning.Set(Weapon, Interval, 0.01f);
        else if (TypeAllowed && !RemoteTekSniper && Ready && !IsFreeCameraActive() && Interval && std::isfinite(*Interval) && *Interval > 0 && Multiplier != 1.0f)
            FireIntervalTuning.Update(Weapon, Interval, Multiplier, true);
        else FireIntervalTuning.Restore();
        if (!RapidCrossbow || !Interval || !FireIntervalTuning.Owner.Get()) ResetRapidCrossbow();
        else {
            if (RapidCrossbowWeapon.Get() != Weapon) {
                RapidCrossbowWeapon = ExtensionObject(Weapon);
                NextRapidCrossbowShot = GetTickCount64() + 10;
            }
            if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
                const uint64_t Now = GetTickCount64();
                // 原版弩的弹夹只有一支箭。authority 上直接调用游戏自己的
                // ReloadWeapon()，由它从 CurrentAmmo/背包转移一支箭并同步物品
                // 弹夹数据，跳过 2.3 秒装填动画；不直接伪造弹夹数。
                if (Now >= NextRapidCrossbowShot && Weapon->WeaponConfig.AmmoPerClip == 1 &&
                    Weapon->bConsumeAmmoItemOnReload && !Weapon->WeaponConfig.bDontTakeAmmoOnReload &&
                    Weapon->GetCurrentAmmoInClip() == 0 &&
                    Weapon->GetCurrentAmmo() > 0)
                    Weapon->ReloadWeapon();
                if (Now >= NextRapidCrossbowShot && Weapon->CurrentAmmoInClip > 0 &&
                    Weapon->BPGetCurrentState() == SDK::EWeaponState::Idle) {
                    NextRapidCrossbowShot = Now + 10;
                    // 原生开火仍决定是否发射、扣弹和造成伤害。
                    Weapon->StopFire();
                    Weapon->StartFire(false);
                }
            } else NextRapidCrossbowShot = GetTickCount64() + 10;
        }
        ExtensionStatus(FireRateStatus, RapidCrossbow ? "本地主机制式弩：按住左键以 10ms 间隔射击，空弹夹由原生弹药转移直接补箭" :
            g_Config::bCrossbowHoldRapidFire && ClassifyFireRateWeapon(Weapon) == FireRateWeaponKind::Crossbow && !Authority
                ? "远程服务器控制制式弩射速和装填；按住速射仅限本地主机" :
            !TypeAllowed ? "当前枪械类型未在射速筛选中启用" :
            RemoteTekSniper ? "泰克狙击枪射速由远程服务器控制，已保留原生间隔以避免无伤害空枪" :
            !Interval ? "未匹配武器开火间隔" : !Ready || IsFreeCameraActive() ? "射速暂时恢复，返回游戏后应用" :
            Authority ? "本地主机：原生开火间隔已调整" : "本地开火间隔已调整；联机效果以实际扣弹和伤害为准");
    }
    else {
        FireIntervalTuning.Restore();
        ResetRapidCrossbow();
        const std::string WeaponName = Weapon && Weapon->Class ? Weapon->Class->GetName() : "none";
        ExtensionStatus(FireRateStatus, !g_Config::bWeaponFireRate && !g_Config::bCrossbowHoldRapidFire ? "射速修改未启用" :
            !Ready ? "人物或界面未就绪：射速已恢复" :
            "当前武器没有受支持的枪械开火流程 (" + WeaponName + ")");
    }
    // 秒换弹与弓秒蓄力（Hack/InstantReload.inl）。两者按开关自行恢复，
    // 未启用时也会被调用以清掉上一次遗留的补丁。
    InstantReloadTick(World);
    InstantChargeTick(World);
    if (Loot.World.Get() != World || Loot.Player.Get() != Human || Loot.Controller.Get() != PC || Loot.Pawn.Get() != Pawn) {
        const bool SameController = Loot.World.Get() == World && Loot.Controller.Get() == PC;
        CloseLootTarget();
        const auto ClosingTarget = Loot.ClosingTarget;
        const auto CloseUntil = Loot.CloseUntil;
        Loot = {};
        if (SameController) { Loot.ClosingTarget = ClosingTarget; Loot.CloseUntil = CloseUntil; }
        Loot.World = ExtensionObject(World); Loot.Player = ExtensionObject(Human); Loot.Controller = ExtensionObject(PC);
        Loot.Pawn = ExtensionObject(Pawn);
    }
    if (!g_Config::bAutoLootContainers || !LootReady || !IsUsableObject(Human->MyCharacterStatusComponent)) {
        CloseLootTarget(); Loot.PendingEgg = {};
        if (!g_Config::bAutoLootContainers) {
            Loot.Retry.clear();
            ExtensionStatus(LootPauseReason, "");
            ExtensionStatus(LootStatus, "自动吸包未启用");
        } else {
            // Opening our panel must preserve the result the user came to inspect.
            ExtensionStatus(LootPauseReason, g_MDX12::g_MenuState::g_isOpen ? "面板打开，自动吸包已暂停" :
                "自动吸包已暂停，等待人物或骑乘就绪、关闭界面并返回游戏");
        }
        return;
    }
    ExtensionStatus(LootPauseReason, "");
    const auto Now = std::chrono::steady_clock::now();
    const float Range = ExtensionLimit(g_Config::AutoLootRangeMeters, 1, 30, 10);
    const float AccessRange = AutoLootAccessRange(Range, Mount ? Human->GetDistanceTo(Pawn) : 0.0);
    float Weight = Human->MyCharacterStatusComponent->BPGetPercentStatusValue(SDK::EPrimalCharacterStatusValue::Weight);
    if (Weight > 1.5f) Weight *= 0.01f;
    if (!std::isfinite(Weight) || Weight < 0 || Weight >= ExtensionLimit(g_Config::AutoLootMaxWeightPercent, 10, 100, 85) * 0.01f) {
        CloseLootTarget(); ExtensionStatus(LootStatus, "达到负重上限或负重无效，已暂停"); return;
    }
    auto* Target = static_cast<SDK::UPrimalInventoryComponent*>(Loot.Target.Get());
    auto* Viewing = PC->GetViewingInventory();
    if (AutoLootInventoryViewBusy(Viewing, Target, Loot.ClosingTarget.Get(), Now < Loot.CloseUntil) ||
        g_AutoGachaPullRuntime.TargetInventory) {
        CloseLootTarget(); ExtensionStatus(LootStatus, "手动或其他自动功能正在访问容器，已暂停"); return;
    }
    if (Target) {
        // Observing the current session is independent of the scan interval.
        // No RPC is sent by this 100 ms poll until the session selects an action.
        if (!Loot.Transfer.Ready(Now)) return;
        LootAccess.Apply(Target, AccessRange);
        auto* Owner = Target->GetOwner();
        if (!IsUsableObject(Owner) || !Owner->IsA(SDK::APrimalStructureItemContainer::StaticClass()) ||
            !LootContainerEnabled(static_cast<SDK::APrimalStructureItemContainer*>(Owner)) ||
            !AutoLootWithinRange(Pawn->GetDistanceTo(Owner), Range) ||
            !AutoLootCanRequestContainer(Mount != nullptr, [&] { return Target->RemoteInventoryAllowViewing(PC, 0); })) {
            CloseLootTarget(); ExtensionStatus(LootStatus, "目标失效、超距或无法访问，已取消"); return;
        }
        const auto Action = Loot.Transfer.Step(Now, Target->InventoryItems.Num());
        if (Action == AutoLootTransferAction::Transfer) {
            const SDK::FString Empty(L"");
            PC->ServerTransferAllFromRemoteInventory(Target, Empty, Empty, Empty, true);
            ExtensionStatus(LootStatus, "已请求取出目标全部物品，等待服务器同步"); return;
        }
        if (Action == AutoLootTransferAction::Refresh) {
            PC->ServerRequestActorItems(Target, true, false);
            ExtensionStatus(LootStatus, Loot.Transfer.TransferSent ?
                "已请求取出目标全部物品，等待服务器同步" : "正在自动刷新容器物品，等待同步后提取");
            return;
        }
        if (Action == AutoLootTransferAction::Complete) {
            Loot.Retry[Loot.Target.Key()] = {Now + std::chrono::seconds(2), 0};
            Loot.Next = Now;
            CloseLootTarget(); ExtensionStatus(LootStatus, "已观察到容器物品减少，继续寻找附近空投／包"); return;
        }
        if (Action == AutoLootTransferAction::Timeout) {
            auto& Retry = Loot.Retry[Loot.Target.Key()];
            Retry.Failures = std::min(Retry.Failures + 1, 4u);
            Retry.Until = Now + std::chrono::milliseconds(AutoLootFailureDelayMs(Retry.Failures));
            Loot.Next = Now;
            const bool Authority = Owner->Role == SDK::ENetRole::ROLE_Authority;
            CloseLootTarget(); ExtensionStatus(LootStatus, Authority ? "容器为空或转移未确认，稍后重试" :
                "吸取未确认；远程服务器可能限制距离、遮挡或访问权限");
        }
        return;
    }
    if (Now < Loot.Next) return;
    Loot.Next = Now + std::chrono::milliseconds(static_cast<int>(ExtensionLimit(g_Config::AutoLootIntervalMs, 250, 3000, 750)));
    for (auto It = Loot.Retry.begin(); It != Loot.Retry.end();) {
        // Retain a recent failure count for backoff; expired entries do not block selection.
        if (Now >= It->second.Until + std::chrono::seconds(30)) It = Loot.Retry.erase(It); else ++It;
    }
    Loot.Target = {};
    bool EggUnconfirmed = false;
    if (Loot.PendingEgg.Pointer) {
        auto* Egg = static_cast<SDK::ADroppedItem*>(Loot.PendingEgg.Get());
        // Disappearance alone may mean streaming/destruction, so do not claim inventory success.
        const bool Changed = !Egg || !IsUsableObject(Egg->MyItem) ||
            Egg->MyItem->ItemQuantity < Loot.EggBeforeQuantity;
        if (!Changed && Now - Loot.EggRequested < std::chrono::seconds(3)) {
            ExtensionStatus(LootStatus, "已请求拾取野生龙蛋，等待服务器同步"); return;
        }
        EggUnconfirmed = !Changed;
        Loot.PendingEgg = {};
    }
    SDK::APrimalStructureItemContainer* Best = nullptr;
    SDK::ADroppedItem* BestEgg = nullptr;
    SDK::UObject* BestEggUseObject = nullptr;
    double BestDistance = Range * 100.0;
    int ContainersCoolingDown = 0, ContainersAccessRejected = 0;
    std::unordered_set<SDK::ULevel*> Levels;
    if (World->PersistentLevel) Levels.insert(World->PersistentLevel);
    if (World->Levels.Num() >= 0 && World->Levels.Num() <= 4096)
        for (auto* Level : World->Levels) if (Level) Levels.insert(Level);
    for (auto* Level : Levels) {
        if (!IsUsableObject(Level) || !Level->Actors.IsValid() || Level->Actors.Num() > 200000) continue;
        for (auto* Actor : Level->Actors) {
            if (!IsUsableObject(Actor) || Actor->bHidden) continue;
            if (g_Config::bAutoLootWildEggs && Actor->IsA(SDK::ADroppedItem::StaticClass())) {
                auto* Egg = static_cast<SDK::ADroppedItem*>(Actor);
                if (Egg->bPreventPickup || !IsUsableObject(Egg->MyItem) || Egg->MyItem->ItemQuantity <= 0) continue;
                bool Allowed = false;
                for (auto* Class = Egg->MyItem->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct)) {
                    if (AutoLootWildEggAllowed(Egg->bIsWildEgg, Class->GetName())) { Allowed = true; break; }
                }
                if (!Allowed) continue;
                // Possessed mount is the interaction origin while riding; rider height
                // is not an on-foot pickup distance.
                const double Distance = Pawn->GetDistanceTo(Egg);
                const ExtensionObject Ref(Egg);
                const auto Retry = Loot.Retry.find(Ref.Key());
                if (!AutoLootWithinRange(Distance, Range) || Distance > BestDistance ||
                    !Ref.Get() || (Retry != Loot.Retry.end() && Now < Retry->second.Until)) continue;
                // Mounted UI availability and rider LOS can reject an egg before
                // any request reaches the server. Request native single pickup;
                // the server remains responsible for mounted eligibility/range.
                if (Mount) {
                    BestEggUseObject = Egg;
                    BestEgg = Egg; Best = nullptr; BestDistance = Distance;
                    continue;
                }
                const float PickupRange = Egg->GetMaxPickupDistance(PC);
                if (!AutoLootWithinRange(Distance, PickupRange * .01f) ||
                    !Egg->IsAllowedToPickupItem(PC) || !Egg->CanCharPickUpItem(Human, false) ||
                    !PC->LineOfSightTo(Egg, SDK::FVector{}, false)) continue;
                SDK::TArray<SDK::FMultiUseEntry> Entries;
                Egg->GetMultiUseEntries(PC, &Entries, -1);
                if (Entries.Num() < 0 || Entries.Num() > 128) continue;
                for (const auto& Entry : Entries) {
                    // Select the live native single-stack pickup entry, never pickup-all.
                    if (Entry.UseIndex != 100 || Entry.bHideFromUI || Entry.bDisableUse || Entry.bClientSideOnly) continue;
                    BestEggUseObject = IsUsableObject(Entry.ForComponent)
                        ? static_cast<SDK::UObject*>(Entry.ForComponent) : static_cast<SDK::UObject*>(Egg);
                    BestEgg = Egg; Best = nullptr; BestDistance = Distance;
                    break;
                }
                continue;
            }
            if (!Actor->IsA(SDK::APrimalStructureItemContainer::StaticClass())) continue;
            auto* Container = static_cast<SDK::APrimalStructureItemContainer*>(Actor);
            if (!LootContainerEnabled(Container) || !IsUsableObject(Container->MyInventoryComponent)) continue;
            const double Distance = Pawn->GetDistanceTo(Container);
            if (!AutoLootWithinRange(Distance, Range) || Distance > BestDistance) continue;
            auto* Inventory = Container->MyInventoryComponent;
            const ExtensionObject Ref(Inventory);
            if (!Ref.Get()) continue;
            const auto Retry = Loot.Retry.find(Ref.Key());
            if (Retry != Loot.Retry.end() && Now < Retry->second.Until) { ++ContainersCoolingDown; continue; }
            // Scoped candidate tuning must be restored even when permissions or
            // blueprint conditions reject the container. Only the winner stays tuned.
            ScopedLootAccessTuning CandidateAccess;
            CandidateAccess.Apply(Inventory, AccessRange);
            if (!AutoLootCanRequestContainer(Mount != nullptr,
                [&] { return Inventory->RemoteInventoryAllowViewing(PC, 0); })) {
                ++ContainersAccessRejected; continue;
            }
            Best = Container; BestEgg = nullptr; BestDistance = Distance;
        }
    }
    if (BestEgg) {
        // Snapshot before RPC: a listen server can destroy the egg synchronously.
        const ExtensionObject Ref(BestEgg);
        Loot.Retry[Ref.Key()] = {Now + std::chrono::seconds(10), 0};
        Loot.PendingEgg = Ref;
        Loot.EggBeforeQuantity = BestEgg->MyItem->ItemQuantity;
        Loot.EggRequested = Now;
        PC->ServerMultiUse(BestEggUseObject, 100);
        ExtensionStatus(LootStatus, "已请求拾取野生龙蛋，等待服务器同步"); return;
    }
    if (!Best) {
        ExtensionStatus(LootStatus, EggUnconfirmed ?
            "龙蛋拾取请求未确认；请靠近巢穴，服务器可能限制骑乘拾取" :
            ContainersCoolingDown > 0 ? "附近容器正在等待重试；上次吸取请求已结束或未确认" :
            ContainersAccessRejected > 0 ? "附近容器被原生访问检查拒绝，请检查等级、权限或实际距离" :
            "范围内没有可拾取的空投、掉落包或野生龙蛋"); return;
    }
    Loot.Target = ExtensionObject(Best->MyInventoryComponent);
    LootAccess.Apply(Best->MyInventoryComponent, AccessRange);
    // Failed targets enter backoff only after an actual timeout. Starting a
    // sync must not impose the old unconditional 10-second retry penalty.
    Loot.Transfer.Begin(Now, static_cast<int>(ExtensionLimit(g_Config::AutoLootIntervalMs, 250, 3000, 750)));
    PC->ServerActorViewRemoteInventory(Best->MyInventoryComponent);
    PC->ServerRequestActorItems(Best->MyInventoryComponent, true, false);
    ExtensionStatus(LootStatus, "正在同步附近空投／掉落包");
}
