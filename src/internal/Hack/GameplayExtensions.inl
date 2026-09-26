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
    };
    FloatTuning FireIntervalTuning;
    std::mutex ExtensionStatusMutex;
    std::string LootStatus = "自动吸包未启用";
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
    struct LootRuntime {
        ExtensionObject World, Player, Controller, Pawn, Target, PendingEgg;
        std::chrono::steady_clock::time_point Next{}, Opened{};
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> Retry;
        bool TransferSent = false;
        int EggBeforeQuantity = 0;
        std::chrono::steady_clock::time_point EggRequested{};
        int BeforeCount = 0;
    } Loot;
    void CloseLootTarget() {
        auto* PC = static_cast<SDK::AShooterPlayerController*>(Loot.Controller.Get());
        auto* Inventory = static_cast<SDK::UPrimalInventoryComponent*>(Loot.Target.Get());
        if (PC && Inventory) PC->ServerActorCloseRemoteInventory(Inventory);
        Loot.Target = {}; Loot.TransferSent = false;
    }
    bool LootContainerEnabled(SDK::APrimalStructureItemContainer* Container) {
        if (!Container || Container->bHidden) return false;
        for (auto* Class = Container->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
            if (AutoLootBeaverDamAllowed(Class->GetName())) return g_Config::bAutoLootBeaverDams;
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
std::string GetAutoLootStatus() { std::scoped_lock Lock(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(LootStatus); }

void ResetGameplayExtensions() {
    ResetPlacementExperiment();
    ResetTekEquipment();
    ResetFeatureExpansion();
    ResetInstantThrowables();
    ResetInstantReload();
    ResetAutoDeposit();
    ResetBackpackTools();
    PhoenixSession::Publish({});
    FireIntervalTuning.Restore();
    CloseLootTarget(); Loot = {};
}

void GameplayExtensionsTick(SDK::UWorld* World) {
    FeatureExpansionTick(World);
    TekEquipmentTick(World);
    PlacementExperimentTick(World);
    // Called only by the serialized local camera/viewport callback.
    auto* PC = GetLocalShooterController(World);
    auto* Pawn = IsUsableObject(PC) && IsUsableObject(PC->Pawn) ? PC->Pawn : nullptr;
    auto* Mount = Pawn && Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())
        ? static_cast<SDK::APrimalDinoCharacter*>(Pawn) : nullptr;
    auto* Rider = Mount ? Mount->Rider.Get() : nullptr;
    if (!IsUsableObject(Rider)) Rider = nullptr;
    // Character/Pawn can both point at the possessed dino. The controller's
    // player-character accessor and the mount's rider retain the survivor.
    auto* Human = IsUsableObject(PC) ? PC->GetPlayerCharacter() : nullptr;
    if (!IsUsableObject(Human)) Human = Rider;
    if (!IsUsableObject(Human)) Human = GetLocalHumanCharacter(World);
    DWORD ForegroundPID = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &ForegroundPID);
    const bool InteractionReady = !IsUploadTerminalActive() && !IsAutomaticDepositActive() && IsUsableObject(PC) && IsUsableObject(Human) && Pawn &&
        !Human->IsDead() && !g_MDX12::g_MenuState::g_isOpen && !PC->bShowMouseCursor &&
        !PC->IsViewingInventoryUI() && ForegroundPID == GetCurrentProcessId();
    const bool Ready = InteractionReady && Pawn == Human;
    const bool LootReady = InteractionReady && !IsHusbandryInventoryActive() && (!Mount || !Mount->IsDead()) &&
        AutoLootControlsHuman(Pawn, Human, Mount, Rider);
    auto* Weapon = IsUsableObject(Human) && IsUsableObject(Human->CurrentWeapon) ? Human->CurrentWeapon : nullptr;
    // Share one owner with the general fire-rate override: two patches of the
    // same field would otherwise compound their baselines and restore incorrectly.
    const bool FastTekGrenade = g_Config::bInstantThrowables && Weapon &&
        Weapon->IsA(SDK::APrimalWeaponGrenade::StaticClass()) &&
        ExtensionClassMatches(Weapon, "WeapTekGrenade_C");
    if ((g_Config::bWeaponFireRate || FastTekGrenade) && Weapon &&
        (Weapon->IsA(SDK::AShooterWeapon_Instant::StaticClass()) || Weapon->IsA(SDK::AShooterWeapon_Projectile::StaticClass()))) {
        const bool Authority = Weapon->Role == SDK::ENetRole::ROLE_Authority;
        const bool RemoteTekSniper = !Authority && ExtensionClassMatches(Weapon, "WeapTekSniper_C");
        // The tested server rejects projectile RPCs below 0.67 * its interval.
        // Leave 10 ms at the stock 1 s interval for frame/network timing. This
        // changes client cadence only; reload/animation can still be limiting.
        const float Multiplier = FastTekGrenade ? (1.0f / 0.68f) :
            EffectiveFireRateMultiplier(Authority, g_Config::WeaponFireRateMultiplier);
        const bool TypeAllowed = FastTekGrenade || FireRateKindEnabled(ClassifyFireRateWeapon(Weapon));
        auto* Interval = GetReflectedNestedFloatAddress(Weapon, "WeaponConfig", "TimeBetweenShots");
        if (TypeAllowed && !RemoteTekSniper && Ready && !IsFreeCameraActive() && Interval && std::isfinite(*Interval) && *Interval > 0 && Multiplier != 1.0f)
            FireIntervalTuning.Update(Weapon, Interval, Multiplier, true);
        else FireIntervalTuning.Restore();
        ExtensionStatus(FireRateStatus, !TypeAllowed ? "当前枪械类型未在射速筛选中启用" :
            RemoteTekSniper ? "泰克狙击枪射速由远程服务器控制，已保留原生间隔以避免无伤害空枪" :
            !Interval ? "未匹配武器开火间隔" : !Ready || IsFreeCameraActive() ? "射速暂时恢复，返回游戏后应用" :
            Authority ? "本地主机：原生开火间隔已调整" : "本地开火间隔已调整；联机效果以实际扣弹和伤害为准");
    }
    else {
        FireIntervalTuning.Restore();
        ExtensionStatus(FireRateStatus, !g_Config::bWeaponFireRate ? "射速修改未启用" :
            !Ready ? "人物或界面未就绪：射速已恢复" : "当前武器没有受支持的枪械开火流程");
    }
    // 秒换弹与弓秒蓄力（Hack/InstantReload.inl）。两者按开关自行恢复，
    // 未启用时也会被调用以清掉上一次遗留的补丁。
    InstantReloadTick(World);
    InstantChargeTick(World);
    if (Loot.World.Get() != World || Loot.Player.Get() != Human || Loot.Controller.Get() != PC || Loot.Pawn.Get() != Pawn) {
        CloseLootTarget(); Loot = {};
        Loot.World = ExtensionObject(World); Loot.Player = ExtensionObject(Human); Loot.Controller = ExtensionObject(PC);
        Loot.Pawn = ExtensionObject(Pawn);
    }
    if (!g_Config::bAutoLootContainers || !LootReady || !IsUsableObject(Human->MyCharacterStatusComponent)) {
        CloseLootTarget(); Loot.Retry.clear(); Loot.PendingEgg = {};
        ExtensionStatus(LootStatus, g_Config::bAutoLootContainers ? "自动吸包已暂停，等待人物或骑乘就绪、关闭界面并返回游戏" : "自动吸包未启用");
        return;
    }
    const auto Now = std::chrono::steady_clock::now();
    const float Range = ExtensionLimit(g_Config::AutoLootRangeMeters, 1, 30, 10);
    float Weight = Human->MyCharacterStatusComponent->BPGetPercentStatusValue(SDK::EPrimalCharacterStatusValue::Weight);
    if (Weight > 1.5f) Weight *= 0.01f;
    if (!std::isfinite(Weight) || Weight < 0 || Weight >= ExtensionLimit(g_Config::AutoLootMaxWeightPercent, 10, 100, 85) * 0.01f) {
        CloseLootTarget(); ExtensionStatus(LootStatus, "达到负重上限或负重无效，已暂停"); return;
    }
    auto* Target = static_cast<SDK::UPrimalInventoryComponent*>(Loot.Target.Get());
    auto* Viewing = PC->GetViewingInventory();
    if ((Viewing && Viewing != Target) || g_AutoGachaPullRuntime.TargetInventory) {
        CloseLootTarget(); ExtensionStatus(LootStatus, "手动或其他自动功能正在访问容器，已暂停"); return;
    }
    if (Now < Loot.Next) return;
    Loot.Next = Now + std::chrono::milliseconds(static_cast<int>(ExtensionLimit(g_Config::AutoLootIntervalMs, 250, 3000, 750)));
    for (auto It = Loot.Retry.begin(); It != Loot.Retry.end();) {
        if (Now >= It->second) It = Loot.Retry.erase(It); else ++It;
    }
    if (Target) {
        auto* Owner = Target->GetOwner();
        if (!IsUsableObject(Owner) || !Owner->IsA(SDK::APrimalStructureItemContainer::StaticClass()) ||
            !LootContainerEnabled(static_cast<SDK::APrimalStructureItemContainer*>(Owner)) ||
            !AutoLootWithinRange(Human->GetDistanceTo(Owner), Range) || !Target->RemoteInventoryAllowViewing(PC, 0)) {
            CloseLootTarget(); ExtensionStatus(LootStatus, "目标失效、超距或无法访问，已取消"); return;
        }
        if (!Loot.TransferSent && Target->InventoryItems.Num() > 0 && Target->InventoryItems.Num() <= 10000) {
            Loot.BeforeCount = Target->InventoryItems.Num();
            const SDK::FString Empty(L"");
            PC->ServerTransferAllFromRemoteInventory(Target, Empty, Empty, Empty, true);
            Loot.TransferSent = true; Loot.Opened = Now;
            ExtensionStatus(LootStatus, "已请求取出目标全部物品，等待服务器同步"); return;
        }
        if (Loot.TransferSent && Target->InventoryItems.Num() < Loot.BeforeCount) {
            CloseLootTarget(); ExtensionStatus(LootStatus, "已观察到容器物品减少，继续寻找附近空投／包"); return;
        }
        if (Now - Loot.Opened >= std::chrono::seconds(3)) {
            CloseLootTarget(); ExtensionStatus(LootStatus, "容器为空或转移未确认，稍后重试");
        }
        return;
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
    std::unordered_set<SDK::ULevel*> Levels;
    if (World->PersistentLevel) Levels.insert(World->PersistentLevel);
    if (World->Levels.Num() >= 0 && World->Levels.Num() <= 4096)
        for (auto* Level : World->Levels) if (Level) Levels.insert(Level);
    for (auto* Level : Levels) {
        if (Level->Actors.Num() < 0 || Level->Actors.Num() > 200000) continue;
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
                if (!AutoLootWithinRange(Distance, Range) || Distance > BestDistance ||
                    !Ref.Get() || Loot.Retry.contains(Ref.Key())) continue;
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
            const double Distance = Human->GetDistanceTo(Container);
            if (!AutoLootWithinRange(Distance, Range) || Distance > BestDistance) continue;
            auto* Inventory = Container->MyInventoryComponent;
            const ExtensionObject Ref(Inventory);
            if (!Ref.Get() || Loot.Retry.contains(Ref.Key()) || !Inventory->RemoteInventoryAllowViewing(PC, 0)) continue;
            Best = Container; BestEgg = nullptr; BestDistance = Distance;
        }
    }
    if (BestEgg) {
        // Snapshot before RPC: a listen server can destroy the egg synchronously.
        const ExtensionObject Ref(BestEgg);
        Loot.Retry[Ref.Key()] = Now + std::chrono::seconds(10);
        Loot.PendingEgg = Ref;
        Loot.EggBeforeQuantity = BestEgg->MyItem->ItemQuantity;
        Loot.EggRequested = Now;
        PC->ServerMultiUse(BestEggUseObject, 100);
        ExtensionStatus(LootStatus, "已请求拾取野生龙蛋，等待服务器同步"); return;
    }
    if (!Best) {
        ExtensionStatus(LootStatus, EggUnconfirmed ?
            "龙蛋拾取请求未确认；请靠近巢穴，服务器可能限制骑乘拾取" :
            "范围内没有可拾取的空投、掉落包或野生龙蛋"); return;
    }
    Loot.Target = ExtensionObject(Best->MyInventoryComponent);
    Loot.Retry[Loot.Target.Key()] = Now + std::chrono::seconds(10);
    Loot.Opened = Now; Loot.TransferSent = false;
    PC->ServerActorViewRemoteInventory(Best->MyInventoryComponent);
    PC->ServerRequestActorItems(Best->MyInventoryComponent, true, false);
    ExtensionStatus(LootStatus, "正在同步附近空投／掉落包");
}
