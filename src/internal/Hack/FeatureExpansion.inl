// Included after GameplayExtensions and inventory helpers; serialized game thread only.
namespace {
#include "FeatureExpansionPatch.inl"
    // 19 owns MaxWalkSpeed for both ground and airborne experiments; never
    // give two patches ownership of the same field. 22 owns jump speed.
    // 31..34 own the Jackalope's base and charged jump speed parameters.
    // 35/36 own the reflected X/Y members of the Jackalope charge time range.
    ExpansionPatch ExpansionPatches[39];
    std::string ExpansionCombatStatus = "等待人物就绪";
    std::string HusbandryStatus = "自动照料未启用";
    auto& RequestedLevelStat = PhoenixShortcuts::PendingLevelStat;
    std::atomic<bool> ExpansionSurvivorAvailable{false};
    std::atomic<unsigned> MountedNativeLastGate{0}; // 1 unavailable, 2 rejected, 3 allowed
    std::atomic<uint64_t> MountedNativeChecks{0};
    std::atomic<unsigned> MountedNativeEquipGate{0}, MountedCharacterEquipGate{0};
    uint64_t NextMountedNativeCheck = 0;
    uint8_t* MountedYawClampTest = nullptr;
    bool SetMountedNativeYawClampBypass(bool Enabled) {
        // ASA CL 832029: the rider-weapon branch tests AllowMountedWeaponry and
        // clamps view yaw to mount yaw +/- 90 degrees when it returns true.
        // Replace only that TEST AL,AL with XOR AL,AL, preserving the existing
        // branch and all weapon equip/fire checks elsewhere in the function.
        if (!Enabled && !MountedYawClampTest) return true;
        if (Enabled && !MountedYawClampTest) {
            auto* Base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
            if (!Base) return false;
            constexpr uintptr_t SiteRva = 0x66CDB95;
            constexpr uint8_t Before[] = {
                0x48,0x8B,0x00,0x45,0x33,0xC0,0x33,0xD2,0x48,0x8B,0xCB,
                0xFF,0x90,0xE0,0x23,0x00,0x00
            };
            constexpr uint8_t After[] = {0xC0,0x0F,0x84,0xC9,0x01,0x00,0x00};
            auto* Site = Base + SiteRva;
            MEMORY_BASIC_INFORMATION Memory{};
            if (VirtualQuery(Site, &Memory, sizeof(Memory)) != sizeof(Memory) ||
                Memory.State != MEM_COMMIT ||
                std::memcmp(Site - sizeof(Before), Before, sizeof(Before)) ||
                Site[0] != 0x84 ||
                std::memcmp(Site + 1, After, sizeof(After))) return false;
            MountedYawClampTest = Site;
        }
        auto* Site = MountedYawClampTest;
        if (!Site) return false;
        if (Site[0] == (Enabled ? 0x30 : 0x84)) {
            if (!Enabled) MountedYawClampTest = nullptr;
            return true;
        }
        if (Site[0] != (Enabled ? 0x84 : 0x30)) return false;
        DWORD PreviousProtect = 0;
        if (!VirtualProtect(Site, 1, PAGE_EXECUTE_READWRITE, &PreviousProtect)) return false;
        *Site = Enabled ? 0x30 : 0x84; // single-byte instruction change
        FlushInstructionCache(GetCurrentProcess(), Site, 1);
        DWORD Ignored = 0;
        VirtualProtect(Site, 1, PreviousProtect, &Ignored);
        if (!Enabled) MountedYawClampTest = nullptr;
        return true;
    }
    ExtensionObject ExpansionCameraHuman, ExpansionCamera;
    bool ExpansionOriginalFirstPerson = false;
    void RestoreExpansionCamera() {
        auto* Camera = static_cast<SDK::AShooterPlayerCameraManager*>(ExpansionCamera.Get());
        if (auto* O = ExpansionCameraHuman.Get(); O && Camera && !Camera->IsFirstPerson())
            static_cast<SDK::AShooterCharacter*>(O)->SetCameraMode(ExpansionOriginalFirstPerson, true, true);
        ExpansionCameraHuman = {}; ExpansionCamera = {};
    }
    bool ExpansionHasClass(SDK::UObject* O, const char* Name) {
        if (!IsUsableObject(O)) return false;
        for (auto* C = O->Class; C; C = static_cast<SDK::UClass*>(C->SuperStruct))
            if (PhoenixExpansion::Normalize(C->GetName()).find(Name) != std::string::npos) return true;
        return false;
    }
#include "NativeMount.inl"
#include "GargantarClient.inl"
#include "DeinoActionTrace.inl"
#include "DeinoAirLatch.inl"
#include "DeinoGlideProbe.inl"
#include "DeinoFlightInput.inl"
#include "DeinoFlight.inl"
    // 投掷物瞬间扔出：给背包里的手雷类物品打上“跳过装备动画”（纯表现层位段），
    // 关闭、物品消失或卸载时恢复原值。
    struct InstantThrowPatchedItem { SDK::FItemNetID ID{}; };
    std::vector<InstantThrowPatchedItem> InstantThrowPatched;
    uint64_t NextInstantThrowScan = 0;
    uint64_t NextHeldGrenadeAttempt = 0;
    bool HeldGrenadeArmed = false;
    ExtensionObject HeldGrenadeWeapon;
    void MaintainHeldGrenade(SDK::AShooterWeapon* Weapon, bool Ready) {
        const bool Down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!Ready || !g_Config::bInstantThrowables || !g_Hook::InstantThrowOK ||
            !IsUsableObject(Weapon) || !Weapon->IsA(SDK::APrimalWeaponGrenade::StaticClass())) {
            HeldGrenadeArmed = false;
            HeldGrenadeWeapon = {};
            NextHeldGrenadeAttempt = 0;
            return;
        }
        if (HeldGrenadeWeapon.Get() != Weapon) {
            HeldGrenadeWeapon = ExtensionObject(Weapon);
            HeldGrenadeArmed = !Down;
            NextHeldGrenadeAttempt = 0;
        }
        if (!Down) {
            HeldGrenadeArmed = true;
            NextHeldGrenadeAttempt = 0;
            return;
        }
        const auto Now = GetTickCount64();
        if (!HeldGrenadeArmed || Now < NextHeldGrenadeAttempt) return;
        auto* Grenade = static_cast<SDK::APrimalWeaponGrenade*>(Weapon);
        if (Grenade->bIsCookingGrenade || Weapon->CurrentAmmoInClip <= 0 ||
            Weapon->BPGetCurrentState() != SDK::EWeaponState::Idle) return;
        // Re-enter native StartFire only when ready. The grenade hook releases it;
        // native fire/reload timers and server ammo checks remain authoritative.
        NextHeldGrenadeAttempt = Now + 100;
        Weapon->StartFire(false);
    }
    void MaintainInstantThrowableItems(SDK::AShooterCharacter* Human) {
        auto* Inventory = IsUsableObject(Human) ? Human->MyInventoryComponent : nullptr;
        if (!g_Config::bInstantThrowables || !IsUsableObject(Inventory)) {
            if (IsUsableObject(Inventory)) {
                for (const auto& P : InstantThrowPatched) {
                    auto* Item = Inventory->BPFindItemWithID(
                        static_cast<int32_t>(P.ID.ItemID1), static_cast<int32_t>(P.ID.ItemID2));
                    if (IsUsableObject(Item)) Item->bSkipEquipAnimation = false;
                }
            }
            InstantThrowPatched.clear();
            NextInstantThrowScan = 0;
            return;
        }
        const uint64_t Now = GetTickCount64();
        if (Now < NextInstantThrowScan) return;
        NextInstantThrowScan = Now + 250;
        if (Inventory->InventoryItems.Num() <= 0 || Inventory->InventoryItems.Num() > 100000) return;
        for (int32_t Index = 0; Index < Inventory->InventoryItems.Num(); ++Index) {
            auto* Item = Inventory->InventoryItems[Index];
            if (!IsUsableObject(Item) || Item->bIsBlueprint || Item->bIsEngram || Item->bIsItemSkin) continue;
            if (!IsThrowableGrenadeChain(NormalizedClassChain(Item))) continue;
            if (!Item->bSkipEquipAnimation) {
                Item->bSkipEquipAnimation = true;
                InstantThrowPatched.push_back({Item->ItemID});
            }
        }
        std::erase_if(InstantThrowPatched, [&](const InstantThrowPatchedItem& P) {
            auto* Item = Inventory->BPFindItemWithID(
                static_cast<int32_t>(P.ID.ItemID1), static_cast<int32_t>(P.ID.ItemID2));
            return !IsUsableObject(Item);
        });
    }
    bool HusbandryEnabled() {
        return g_Config::bAutoClaimBabies || g_Config::bAutoImprint || g_Config::bAutoMount ||
            g_Config::bAutoActivateTurrets || g_Config::bAutoExtractEmbryos || g_Config::bAutoRemoveBola ||
            g_Config::bAutoFillGacha || g_Config::bAutoFeedBabies || g_Config::bAutoFertilize || g_Config::bAutoFillTurrets;
    }
    struct HusbandryRuntime {
        ExtensionObject World, Human, PC, Target, Item;
        PhoenixExpansion::Supply Kind = PhoenixExpansion::Supply::None;
        uint64_t Next = 0, Opened = 0;
        int Before = 0;
        bool Sent = false;
        std::unordered_map<uint64_t, uint64_t> Retry;
    } Husbandry;
    void CloseHusbandry() {
        auto* PC = static_cast<SDK::AShooterPlayerController*>(Husbandry.PC.Get());
        auto* I = static_cast<SDK::UPrimalInventoryComponent*>(Husbandry.Target.Get());
        if (PC && I && !PC->IsViewingInventoryUI() &&
            (!PC->GetViewingInventory() || PC->GetViewingInventory() == I)) PC->ServerActorCloseRemoteInventory(I);
        Husbandry.Target = {}; Husbandry.Item = {}; Husbandry.Sent = false;
    }
    bool SupplyEnabled(PhoenixExpansion::Supply K) {
        using enum PhoenixExpansion::Supply;
        return K == Gacha ? g_Config::bAutoFillGacha : K == Baby ? g_Config::bAutoFeedBabies :
            K == Crop ? g_Config::bAutoFertilize : K == Turret ? g_Config::bAutoFillTurrets : false;
    }
    bool SupplyItem(SDK::UPrimalItem* I, SDK::UPrimalInventoryComponent* Bag, PhoenixExpansion::Supply Kind) {
        return IsUsableObject(I) && I->ItemQuantity > 0 && I->bAllowRemovalFromInventory &&
            !I->bIsBlueprint && !I->bIsEngram && !I->bIsItemSkin && !I->bEquippedItem && I->SlotIndex < 0 &&
            DepositContains(Bag->InventoryItems, I) && !DepositContains(Bag->EquippedItems, I) && !DepositContains(Bag->ItemSlots, I) &&
            PhoenixExpansion::MatchesSupply(Kind, I->Class->GetName());
    }
    void HusbandryTick(SDK::UWorld* World, SDK::AShooterPlayerController* PC, SDK::AShooterCharacter* Human, bool Ready) {
        using namespace PhoenixExpansion;
        if (Husbandry.World.Get() != World || Husbandry.Human.Get() != Human || Husbandry.PC.Get() != PC) {
            CloseHusbandry(); Husbandry = {};
            Husbandry.World = ExtensionObject(World); Husbandry.Human = ExtensionObject(Human); Husbandry.PC = ExtensionObject(PC);
        }
        if (!HusbandryEnabled() || !Ready) {
            CloseHusbandry(); Husbandry.Retry.clear();
            ExtensionStatus(HusbandryStatus, HusbandryEnabled() ? "照料已暂停，等待人物与界面就绪" : "自动照料未启用");
            return;
        }
        auto* Bag = Human->MyInventoryComponent;
        if (!IsUsableObject(Bag) || !ValidDepositArray(Bag->InventoryItems)) { CloseHusbandry(); return; }
        auto* Target = static_cast<SDK::UPrimalInventoryComponent*>(Husbandry.Target.Get());
        if (IsUploadTerminalActive() || IsAutomaticDepositActive() || Loot.Target.Get() || g_AutoGachaPullRuntime.TargetInventory ||
            g_AutoCraftRuntime.OwnsCraftRemoteView || g_AutoCraftRuntime.DiscoveryInventory ||
            (PC->GetViewingInventory() && PC->GetViewingInventory() != Target)) {
            CloseHusbandry(); ExtensionStatus(HusbandryStatus, "其他容器会话进行中，照料等待"); return;
        }
        const auto Now = GetTickCount64();
        const float Range = ExtensionLimit(g_Config::HusbandryRangeMeters, 1, 30, 6);
        if (Now < Husbandry.Next) return;
        Husbandry.Next = Now + uint64_t(1000 * ExtensionLimit(g_Config::HusbandryIntervalSeconds, .5f, 10, 1));
        if (Target) {
            auto* Owner = Target->GetOwner();
            auto* Item = static_cast<SDK::UPrimalItem*>(Husbandry.Item.Get());
            const float Distance = IsUsableObject(Owner) ? Human->GetDistanceTo(Owner) * .01f : -1;
            if (!SupplyEnabled(Husbandry.Kind) || !std::isfinite(Distance) || Distance < 0 || Distance > Range ||
                !Target->IsAllowedInventoryAccess(PC) || !Target->RemoteInventoryAllowViewing(PC, 0)) { CloseHusbandry(); return; }
            if (Husbandry.Sent) {
                if (!Item || Item->ItemQuantity < Husbandry.Before) {
                    CloseHusbandry(); ExtensionStatus(HusbandryStatus, "已观察到背包物品减少，继续照料");
                }
                else if (Now - Husbandry.Opened >= 4000) {
                    CloseHusbandry(); ExtensionStatus(HusbandryStatus, "转移尚未确认，稍后重试");
                }
                return;
            }
            if (!SupplyItem(Item, Bag, Husbandry.Kind)) { CloseHusbandry(); return; }
            if (Now - Husbandry.Opened < 500) return;
            if (!ValidDepositArray(Target->InventoryItems)) { CloseHusbandry(); return; }
            int Existing = 0;
            for (auto* I : Target->InventoryItems)
                if (IsUsableObject(I) && MatchesSupply(Husbandry.Kind, I->Class->GetName()))
                    Existing = std::min(100000, Existing + std::clamp(I->ItemQuantity, 0, 10000));
            const int Desired = Husbandry.Kind == Supply::Crop ? 5 : Husbandry.Kind == Supply::Turret ? 500 : 100;
            if (Existing >= Desired) { CloseHusbandry(); return; }
            Husbandry.Before = Item->ItemQuantity; Husbandry.Sent = true; Husbandry.Opened = Now;
            PC->ServerTransferToRemoteInventory(Target, Item->ItemID, false, std::min(Item->ItemQuantity, Desired - Existing));
            ExtensionStatus(HusbandryStatus, "已请求补充物资，等待服务器同步"); return;
        }
        CloseHusbandry();
        for (auto I = Husbandry.Retry.begin(); I != Husbandry.Retry.end();)
            if (I->second <= Now) I = Husbandry.Retry.erase(I); else ++I;
        if (Husbandry.Retry.size() > 512) Husbandry.Retry.clear();
        struct Candidate { ExtensionObject Actor; float Distance; };
        std::vector<Candidate> Candidates;
        std::unordered_set<SDK::ULevel*> Levels;
        if (World->PersistentLevel) Levels.insert(World->PersistentLevel);
        if (World->Levels.Num() >= 0 && World->Levels.Num() <= 4096)
            for (auto* L : World->Levels) if (L) Levels.insert(L);
        for (auto* L : Levels) {
            if (L->Actors.Num() < 0 || L->Actors.Num() > 200000) continue;
            for (auto* A : L->Actors) {
                if (!IsUsableObject(A) || A->bHidden || A == Human ||
                    (!A->IsA(SDK::APrimalDinoCharacter::StaticClass()) && !A->IsA(SDK::APrimalStructure::StaticClass()))) continue;
                const float D = Human->GetDistanceTo(A) * .01f;
                const ExtensionObject Ref(A);
                if (!Ref.Get() || !std::isfinite(D) || D < 0 || D > Range || Husbandry.Retry.contains(Ref.Key())) continue;
                Candidates.push_back({Ref, D});
            }
        }
        std::sort(Candidates.begin(), Candidates.end(), [](const Candidate& A, const Candidate& B) { return A.Distance < B.Distance; });
        if (Candidates.size() > 32) Candidates.resize(32);
        for (const auto& C : Candidates) {
            auto* A = static_cast<SDK::AActor*>(C.Actor.Get());
            if (!A) continue;
            Husbandry.Retry[C.Actor.Key()] = Now + 10000;
            auto* Dino = A->IsA(SDK::APrimalDinoCharacter::StaticClass()) ? static_cast<SDK::APrimalDinoCharacter*>(A) : nullptr;
            auto* Structure = !Dino ? static_cast<SDK::APrimalStructure*>(A) : nullptr;
            if ((Dino && Dino->IsDead()) || (Structure && (Structure->bIsPreviewStructure || Structure->Health <= 0))) continue;
            const bool Friendly = Human->TargetingTeam != 0 && Human->TargetingTeam == (Dino ? Dino->TargetingTeam : Structure->TargetingTeam);
            const bool Baby = Dino && Dino->bIsBaby;
            const bool Turret = Structure && A->IsA(SDK::APrimalStructureTurret::StaticClass()) &&
                !A->IsA(SDK::APrimalStructureTurretPlant::StaticClass());
            // Only requests exposed by the actual radial wheel; no hard-coded use indices.
            if (C.Distance <= 6 && (Friendly || (Baby && g_Config::bAutoClaimBabies))) {
                SDK::TArray<SDK::FMultiUseEntry> Entries;
                A->GetMultiUseEntries(PC, &Entries, -1);
                if (Entries.Num() >= 0 && Entries.Num() <= 512) for (const auto& E : Entries) {
                    if (E.UseIndex < 0 || E.bClientSideOnly || E.bDisableUse || E.bHideFromUI) continue;
                    const auto Action = ClassifyUse(E.UseString.ToString());
                    const bool Allowed = (Action == Use::Claim && Baby && !Friendly && g_Config::bAutoClaimBabies) ||
                        (Friendly && ((Action == Use::Imprint && Baby && Dino->IsImprintPlayer(Human) && g_Config::bAutoImprint) ||
                        (Action == Use::Ride && Dino && PC->Pawn == Human && g_Config::bAutoMount) ||
                        (Action == Use::Activate && Turret && g_Config::bAutoActivateTurrets) ||
                        (Action == Use::Extract && Dino && g_Config::bAutoExtractEmbryos) ||
                        (Action == Use::RemoveBola && Dino && g_Config::bAutoRemoveBola)));
                    if (!Allowed) continue;
                    auto* UseObject = IsUsableObject(E.ForComponent) ? static_cast<SDK::UObject*>(E.ForComponent) : A;
                    PC->ServerMultiUse(UseObject, E.UseIndex);
                    ExtensionStatus(HusbandryStatus, "已提交原生交互请求，等待游戏反馈"); return;
                }
            }
            if (!Friendly) continue;
            Supply Kind = Supply::None;
            if (Dino && g_Config::bAutoFillGacha && ExpansionHasClass(Dino, "gacha")) Kind = Supply::Gacha;
            else if (Baby && g_Config::bAutoFeedBabies) Kind = Supply::Baby;
            else if (Structure && g_Config::bAutoFertilize && ExpansionHasClass(A, "cropplot")) Kind = Supply::Crop;
            else if (Turret && g_Config::bAutoFillTurrets && !ExpansionHasClass(A, "tek")) Kind = Supply::Turret;
            if (Kind == Supply::None) continue;
            auto* I = Dino ? Dino->MyInventoryComponent : (A->IsA(SDK::APrimalStructureItemContainer::StaticClass()) ?
                static_cast<SDK::APrimalStructureItemContainer*>(A)->MyInventoryComponent : nullptr);
            if (!IsUsableObject(I) || !I->IsAllowedInventoryAccess(PC) || !I->RemoteInventoryAllowViewing(PC, 0)) continue;
            SDK::UPrimalItem* Item = nullptr;
            for (auto* V : Bag->InventoryItems) if (SupplyItem(V, Bag, Kind)) { Item = V; break; }
            if (!Item) continue;
            Husbandry.Target = ExtensionObject(I); Husbandry.Item = ExtensionObject(Item); Husbandry.Kind = Kind;
            Husbandry.Opened = Now;
            PC->ServerActorViewRemoteInventory(I); PC->ServerRequestActorItems(I, true, false);
            ExtensionStatus(HusbandryStatus, "正在同步照料目标背包"); return;
        }
        ExtensionStatus(HusbandryStatus, "等待可交互目标或背包补给物资");
    }
}

std::string GetNativeMountStatus() {
    std::scoped_lock L(ExtensionStatusMutex);
    auto Text=LanguageManager::Get().TranslateCopy(NativeMountStatus);
    if (!NativeMountLastResult.empty() && NativeMountLastResult!=NativeMountStatus)
        Text+="\n"+LanguageManager::Get().TranslateCopy(NativeMountLastResult);
    return Text;
}
void TraceDeinoActionEvent(SDK::UObject* O,SDK::UFunction* F,void* P,bool After) { ObserveDeinoAction(O,F,P,After); }
bool SuppressDeinoFlightInput(SDK::UObject* O,SDK::UFunction* F) { return SuppressFlightInput(O,F); }
std::string GetDeinoFlightStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(DeinoFlightStatus); }
std::string GetDeinoHoverFacingStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(DeinoHoverFacingStatus); }
std::string GetDeinoAirLatchStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(DeinoAirLatchStatus); }
std::string GetDeinoGlideProbeStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(DeinoGlideProbeStatus); }
std::string GetDeinoGlideProbePath() { std::scoped_lock L(ExtensionStatusMutex); return DeinoGlideProbePath; }
void NativeMountPhysicsHook(SDK::UMovementComponent* Movement) {
    // The rotation hook can also visit unrelated actors. Only consume a fresh,
    // weak-owned request on the gameplay thread after the original callback.
    if (GetCurrentThreadId()!=NativeMountThread.load() || !NativeMount.QueuedAt) return;
    if (NativeMount.QueuedMove.Get()!=Movement) return;
    const auto At=NativeMount.QueuedAt;
    NativeMount.QueuedAt=0; NativeMount.QueuedMove={};
    NativeMountResult("运动回调已到达，但对象、输入或运动状态检查未通过");
    auto* PC=static_cast<SDK::AShooterPlayerController*>(NativeMount.QueuedController.Get());
    NativeMount.QueuedController={};
    auto* D=static_cast<SDK::APrimalDinoCharacter*>(NativeMount.Mount.Get());
    auto* M=static_cast<SDK::UCharacterMovementComponent*>(Movement);
    DWORD PID=0; GetWindowThreadProcessId(GetForegroundWindow(),&PID);
    if (!g_Config::bNativeMountJump || !g_Config::bNativeMountPhysicsHook || g_Config::bDeinoAirLatch || g_Config::bDeinoAutoJumpTest || g_Config::bDeinoLatchTrace || g_Config::bDeinoFlight ||
        GetTickCount64()-At>200 || !D || !PC || PC->Pawn!=D || D->IsDead() || IsFreeCameraActive() ||
        !PhoenixShortcuts::GameplayInputReady(PC,g_MDX12::g_MenuState::g_isOpen,false) ||
        g_MDX12::g_MenuState::g_isOpen || PID!=GetCurrentProcessId() ||
        D->CharacterMovement!=M || M->MovementMode!=NativeMount.QueuedMode ||
        !(D->Role==SDK::ENetRole::ROLE_AutonomousProxy || D->Role==SDK::ENetRole::ROLE_Authority)) return;
    const auto V=M->Velocity;
    const auto Plan=PhoenixNativeMount::Boost({V.X,V.Y,V.Z},NativeMount.QueuedYaw,
        M->MovementMode==SDK::EMovementMode::MOVE_Walking,NativeMount.QueuedGroundSpeed,
        NativeMount.QueuedAirStep,NativeMount.QueuedCap);
    if (!Plan.Valid) return;
    if (ExpansionHasClass(D,"deinonychus_character_bp")) {
        auto* P=FindReflectedProperty(D->Class,"UberpounceState");
        if (!P || !P->ClassPrivate || P->ClassPrivate->Name.ToString()!="ByteProperty" ||
            P->ElementSize!=1 || P->ArrayDim!=1 || P->Offset<0 || P->Offset>=D->Class->Size ||
            *(reinterpret_cast<uint8_t*>(D)+P->Offset)!=0) return;
    }
    // Consume once; never rewrite position, suppress correction, or accumulate
    // the same request on subsequent substeps. Mode changes use the native API.
    if (M->MovementMode==SDK::EMovementMode::MOVE_Walking &&
        !NativeMountMode(M,SDK::EMovementMode::MOVE_Falling,0)) return;
    if (M->MovementMode!=SDK::EMovementMode::MOVE_Falling) return;
    M->Velocity={Plan.Target.X,Plan.Target.Y,Plan.Target.Z};
    NativeMountResult("已在运动Hook写入一次速度；服务器接受情况待确认");
}
std::string GetExpansionCombatStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(ExpansionCombatStatus); }
std::string GetGargantarClientStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(GargantarStatus); }
std::string GetHusbandryStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(HusbandryStatus); }
bool CanAllocateLevel() { return ExpansionSurvivorAvailable.load(); }
void RequestAllocateLevel(int Stat) { if (CanAllocateLevel() && Stat >= 0 && Stat < 12) RequestedLevelStat.store(Stat); }
bool IsHusbandryInventoryActive() { return Husbandry.Target.Pointer != nullptr; }
void ResetFeatureExpansion() {
    ResetGargantarClient();
    SetMountedNativeYawClampBypass(false);
    ResetDeinoFlight();
    FinishDeinoGlideProbe("reset_or_unload");
    ResetDeinoAirLatch();
    ResetNativeMount();
    for (auto& P : ExpansionPatches) P.Restore();
    RestoreExpansionCamera(); CloseHusbandry(); Husbandry = {};
    RequestedLevelStat.store(-1);
    NextMountedNativeCheck = 0;
    ExpansionSurvivorAvailable.store(false);
}
void FeatureExpansionTick(SDK::UWorld* World) {
    auto* PC = GetLocalShooterController(World);
    auto* Human = IsUsableObject(PC) ? PC->GetPlayerCharacter() : nullptr;
    auto* Pawn = IsUsableObject(PC) && IsUsableObject(PC->Pawn) ? PC->Pawn : nullptr;
    auto* Mount = Pawn && Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass()) ? static_cast<SDK::APrimalDinoCharacter*>(Pawn) : nullptr;
    if (!IsUsableObject(Human) && Mount) Human = Mount->Rider.Get();
    const bool SurvivorAvailable = World && IsUsableObject(Human) && Pawn && !Human->IsDead();
    ExpansionSurvivorAvailable.store(SurvivorAvailable);
    if (!SurvivorAvailable) RequestedLevelStat.store(-1);
    DWORD PID = 0; GetWindowThreadProcessId(GetForegroundWindow(), &PID);
    const bool Ready = World && IsUsableObject(PC) && IsUsableObject(Human) && Pawn && !Human->IsDead() &&
        (!Mount || !Mount->IsDead()) && PID == GetCurrentProcessId() &&
        PhoenixShortcuts::GameplayInputReady(PC, g_MDX12::g_MenuState::g_isOpen, false);
    const bool Authority = Ready && Pawn->Role == SDK::ENetRole::ROLE_Authority;
    for (auto& P : ExpansionPatches) P.Seen = false;
    auto Set = [&](int Slot, SDK::UObject* O, const char* Name, float Value, bool Multiply = false) {
        ExpansionPatches[Slot].Set(O, Name, Value, Multiply);
    };
    auto* W = Ready && IsUsableObject(Human->CurrentWeapon) ? Human->CurrentWeapon : nullptr;
    const bool MountedYawNativeReady = SetMountedNativeYawClampBypass(
        Ready && W && Mount && g_Config::bMountedFullRotation);
    if (W && g_Config::bFastAim) { Set(0,W,"TargetingDelayTime",0); Set(1,W,"TargetingFOVInterpSpeed",100); }
    if (W && g_Config::bNoRecoil) {
        Set(2,W,"GlobalFireCameraShakeScale",0); Set(3,W,"GlobalFireCameraShakeScaleTargeting",0);
        Set(4,W,"AimDriftYawAngle",0); Set(5,W,"AimDriftPitchAngle",0);
    }
    if (W && !Mount && g_Config::bSprintAttack) {
        Set(28,W,"bAllowRunning",1);
        Set(29,W,"bAllowRunningWhileFiring",1);
        Set(30,W,"bAllowRunningWhileMeleeAttacking",1);
    }
    // 弓秒蓄力（含木弓／复合弓／泰克弓分项筛选）改由 Hack/InstantReload.inl
    // 的 InstantChargeTick 统一负责，避免两套补丁争用同一字段。
    if (Ready && Mount && g_Config::bMountedWeapons) {
        // CanEquipWeapons is consulted before CurrentWeapon exists.
        Set(23,Human,"bPreventAllWeapons",0);
        // AllowMountedWeaponry(false, false) explicitly rejects a weapon when
        // bAllowUseWhileRidingDino is set (CL 829680 native branch 6c7f5ec).
        // That flag is for tools usable alongside mount input. Guns must take
        // the rider weapon branch so native OnStartFire/OnStopFire own input.
        if (W) { Set(8,W,"bAllowUseWhileRidingDino",0); Set(9,W,"bForceAllowMountedWeaponry",1); }
        Set(10,Mount,"bPreventAllRiderWeapons",0); Set(11,Mount,"bAllowMountedWeaponry",1); Set(12,Mount,"bAllowMountedWeaponryPVE",1);
        if (GetTickCount64() >= NextMountedNativeCheck) {
            NextMountedNativeCheck = GetTickCount64() + 250;
            auto* Query = Mount->Class->GetFunction("PrimalDinoCharacter", "AllowMountedWeaponry");
            unsigned Result = 1;
            if (IsUsableObject(Query) && Query->Size == 3) {
                struct { bool IgnoreWeapon = false, WeaponForces = false, Allowed = false; } Args;
                Mount->ProcessEvent(Query, &Args); Result = Args.Allowed ? 3u : 2u;
                Args={true,false,false}; Mount->ProcessEvent(Query,&Args);
                MountedNativeEquipGate.store(Args.Allowed?3u:2u);
            }
            auto* Equip=Human->Class->GetFunction("ShooterCharacter","CanEquipWeapons");
            if(IsUsableObject(Equip)&&Equip->Size==1){bool Allowed=false;Human->ProcessEvent(Equip,&Allowed);MountedCharacterEquipGate.store(Allowed?3u:2u);}
            MountedNativeLastGate.store(Result); MountedNativeChecks.fetch_add(1);
        }
    }
    if (Ready && W && Mount && g_Config::bMountedFullRotation && IsUsableObject(PC->PlayerCameraManager)) {
        const auto [YawMin,YawMax] = PhoenixExpansion::MountedYawWindow(PC->GetControlRotation().Yaw);
        Set(13,PC->PlayerCameraManager,"ViewYawMin",YawMin); Set(14,PC->PlayerCameraManager,"ViewYawMax",YawMax);
        Set(15,PC->PlayerCameraManager,"ViewPitchMin",-89); Set(16,PC->PlayerCameraManager,"ViewPitchMax",89);
        Set(17,Mount,"bLimitRiderYawOnLatched",0);
        // Passenger seats impose a second yaw clamp specifically while a
        // simple weapon is equipped. The camera manager limits alone cannot
        // override it. Patch the active seat definition and restore on exit.
        ExpansionPatches[37].Set(Human,"CurrentPassengerSeatDefinition",0,false,"bLimitCameraYaw");
        ExpansionPatches[38].Set(Human,"CurrentPassengerSeatDefinition",0,false,"SimpleWeaponEquipped_LimitCameraYaw");
    }
    if (Ready && Mount && g_Config::bMountedThirdPerson) {
        if (ExpansionCameraHuman.Get() != Human) {
            RestoreExpansionCamera();
            auto* Camera = IsUsableObject(PC->PlayerCameraManager) && PC->PlayerCameraManager->IsA(SDK::AShooterPlayerCameraManager::StaticClass()) ?
                static_cast<SDK::AShooterPlayerCameraManager*>(PC->PlayerCameraManager) : nullptr;
            if (Camera) { ExpansionOriginalFirstPerson = Camera->IsFirstPerson(); ExpansionCamera = ExtensionObject(Camera);
                ExpansionCameraHuman = ExtensionObject(Human); Human->SetCameraMode(false, true, true); }
        }
        if (W) {
            Set(18,W,"bForceFirstPersonWhileTargeting",0);
            // GetCameraStyle has a dedicated mounted branch; CameraStyle alone
            // is overwritten each frame unless the equipped weapon permits TPV.
            Set(24,W,"bForceTPV_EquippedWhileRiding",1);
            Set(25,W,"bUseBPForceFirstPerson",0);
        }
        auto* Camera = static_cast<SDK::AShooterPlayerCameraManager*>(ExpansionCamera.Get());
        if (Camera && Camera->IsFirstPerson()) Human->SetCameraMode(false, true, true);
    }
    else RestoreExpansionCamera();
    auto* Movement = Ready && Mount && IsUsableObject(Mount->CharacterMovement) ? Mount->CharacterMovement : nullptr;
    TickGargantarClient(Mount,Ready);
    // Remote-client experiment: change prediction only on the locally possessed
    // mount. Keep server movement/correction untouched and restore via patches.
    const bool ClientSpeedExperiment = Movement && !Authority &&
        Mount->Role == SDK::ENetRole::ROLE_AutonomousProxy && g_Config::bDinoMoveSpeed;
    const bool DeinonychusFalling = Movement && ExpansionHasClass(Mount,"deinonychus") &&
        Movement->MovementMode == SDK::EMovementMode::MOVE_Falling &&
        (Authority || Mount->Role == SDK::ENetRole::ROLE_AutonomousProxy);
    const bool AirSpeedExperiment = DeinonychusFalling && g_Config::bDeinonychusAirSpeed;
    if (Movement && (Authority || ClientSpeedExperiment) && g_Config::bDinoMoveSpeed) {
        const float M = ExtensionLimit(g_Config::DinoMoveSpeedMultiplier,.25f,5,2);
        if (!AirSpeedExperiment) Set(19,Movement,"MaxWalkSpeed",M,true);
        if (Authority) { Set(20,Movement,"MaxSwimSpeed",M,true); Set(21,Movement,"MaxFlySpeed",M,true); }
    }
    // Falling uses MaxWalkSpeed through the native GetMaxSpeed route.
    // This changes a prediction limit, not existing velocity or server policy.
    if (AirSpeedExperiment)
        Set(19,Movement,"MaxWalkSpeed",ExtensionLimit(g_Config::DeinonychusAirSpeedMultiplier,.5f,3,2),true);
    if (Ready && Mount && g_Config::bDeinoFlight && ExpansionHasClass(Mount,"deinonychus")) {
        // ComputeJumpVector uses this Blueprint double before the native
        // ServerRequestJump(Velocity) path; keep stamina and jump input native.
        Set(22,Mount,"UberpounceJumpSpeed",ExtensionLimit(g_Config::DeinonychusJumpMultiplier,1,5,2),true);
    }
    if (Movement && ExpansionHasClass(Mount,"jackalope_character_bp")) {
        const float M = ExtensionLimit(g_Config::JackalopeJumpMultiplier,1,20,2);
        // One owner for the shared maximum: explicit cap overrides the big-jump
        // cap, while the other three jump inputs remain independently active.
        if (g_Config::bJackalopeChargeCap || g_Config::bJackalopeBigJump)
            Set(31,Mount,"CustomJumpMaxVelocity",g_Config::bJackalopeChargeCap ?
                ExtensionLimit(g_Config::JackalopeChargeCapMultiplier,1,20,2) : M,true);
        if (g_Config::bJackalopeBigJump) {
            Set(32,Mount,"CustomJumpMinZVelocity",M,true);
            Set(33,Movement,"JumpZVelocity",M,true);
            Set(34,Mount,"AddForwardVelocityOnJump",M,true);
        }
        if (g_Config::bJackalopeFastCharge) {
            const double Scale = 1.0 / ExtensionLimit(g_Config::JackalopeChargeSpeedMultiplier,1,10,5);
            ExpansionPatches[35].Set(Mount,"CustomJumpHoldTimeMinMax",Scale,true,"X");
            ExpansionPatches[36].Set(Mount,"CustomJumpHoldTimeMinMax",Scale,true,"Y");
            if (ExpansionPatches[35].Offset < 0 || ExpansionPatches[36].Offset < 0) {
                ExpansionPatches[35].Restore(); ExpansionPatches[36].Restore();
            }
        }
    }
    TickNativeMount(PC,Mount,Movement,Ready);
    // Preserve only the already-bound flight when menus/focus block new input.
    // Other feature readiness and movement patches retain their existing gates.
    const bool FlightReady=Ready || (g_Config::bDeinoFlight && SurvivorAvailable &&
        IsUsableObject(Mount) && !Mount->IsDead() && DeinoFlight.Dino.Get()==Mount);
    auto* FlightMovement=FlightReady && Mount && IsUsableObject(Mount->CharacterMovement) ? Mount->CharacterMovement : nullptr;
    TickDeinoGlideProbe(PC,Mount,FlightMovement,FlightReady,Ready);
    TickDeinoFlight(PC,Mount,FlightMovement,FlightReady,Ready);
    TickDeinoAirLatch(PC,Mount,Movement,Ready && !g_Config::bDeinoFlight && !g_Config::bDeinoAutoJumpTest && !g_Config::bDeinoGlideProbe &&
        (!g_Config::bDeinoLatchTrace || (DeinoGlideProbe.Window.Started && DeinoGlideProbe.Mode==3)));
    // 手雷物品“免举起”位段维护（独立于武器扩展补丁，卸载/断开时恢复）。
    MaintainInstantThrowableItems(Human);
    MaintainHeldGrenade(W, Ready && !IsFreeCameraActive());
    for (auto& P : ExpansionPatches) if (!P.Seen) P.Restore();
    const bool Wanted = g_Config::bFastAim || g_Config::bNoRecoil || g_Config::bSprintAttack || g_Config::bFastBow || g_Config::bMountedWeapons ||
        g_Config::bMountedFullRotation || g_Config::bMountedThirdPerson || g_Config::bDinoMoveSpeed || g_Config::bDeinonychusBigJump ||
        g_Config::bDeinonychusAirSpeed || g_Config::bJackalopeBigJump || g_Config::bJackalopeChargeCap || g_Config::bJackalopeFastCharge;
    bool Applied = ExpansionCameraHuman.Get() != nullptr;
    for (const auto& P : ExpansionPatches) Applied |= P.Offset >= 0 && P.Seen;
    ExtensionStatus(ExpansionCombatStatus, !Ready ? "人物或界面未就绪，临时修改已恢复" : !Wanted ? "武器与坐骑扩展未启用" :
        Mount && W && g_Config::bMountedFullRotation && !MountedYawNativeReady ? "当前游戏版本的骑乘视角原生补丁不匹配" :
        !Applied ? "当前对象或主机权限不匹配，未应用修改" : ClientSpeedExperiment ?
        "客户端移速实验：已调整本地步行参数；实际加速与回拉待服务器采样确认" : Authority ?
        "本地主机：已应用匹配的武器与坐骑设置" : "远程联机：仅应用本地武器与视角设置，蓄力、移速和飞行由服务器决定");
    // One explicit level allocation request; never manufacture XP or available points.
    const int Stat = Ready ? RequestedLevelStat.exchange(-1) : -1;
    if (Stat >= 0 && Ready) {
        auto* Character = Mount ? static_cast<SDK::APrimalCharacter*>(Mount) : Human;
        auto* Status = Character->MyCharacterStatusComponent;
        if (IsUsableObject(Status) && Status->CanLevelUp(static_cast<SDK::EPrimalCharacterStatusValue>(Stat), true))
            PC->ServerRequestLevelUp(Status, static_cast<SDK::EPrimalCharacterStatusValue>(Stat));
    }
    HusbandryTick(World, PC, Human, Ready);
}

void MaintainMountedFullRotationCamera(SDK::AShooterPlayerCameraManager* Camera) {
    // The native camera update can replace the rider's yaw limits after
    // FeatureExpansionTick. Preserve the active override for the next input
    // sample without reviving a patch that was restored this frame.
    if (!IsUsableObject(Camera) || !IsUsableObject(Camera->PCOwner) ||
        ExpansionPatches[13].Owner.Get() != Camera ||
        !ExpansionPatches[13].Seen) return;
    const auto [YawMin,YawMax] = PhoenixExpansion::MountedYawWindow(Camera->PCOwner->GetControlRotation().Yaw);
    ExpansionPatches[13].Set(Camera,"ViewYawMin",YawMin);
    ExpansionPatches[14].Set(Camera,"ViewYawMax",YawMax);
    ExpansionPatches[15].Set(Camera,"ViewPitchMin",-89);
    ExpansionPatches[16].Set(Camera,"ViewPitchMax",89);
}

void ResetInstantThrowables() {
    HeldGrenadeArmed = false;
    HeldGrenadeWeapon = {};
    NextHeldGrenadeAttempt = 0;
    // 卸载/断开时没有后续回调可恢复物品位段，这里按当前人物背包尽量恢复。
    auto* World = SDK::UWorld::GetWorld();
    auto* Human = GetLocalHumanCharacter(World);
    auto* Inventory = IsUsableObject(Human) ? Human->MyInventoryComponent : nullptr;
    if (IsUsableObject(Inventory)) {
        for (const auto& P : InstantThrowPatched) {
            auto* Item = Inventory->BPFindItemWithID(
                static_cast<int32_t>(P.ID.ItemID1), static_cast<int32_t>(P.ID.ItemID2));
            if (IsUsableObject(Item)) Item->bSkipEquipAnimation = false;
        }
    }
    InstantThrowPatched.clear();
    NextInstantThrowScan = 0;
}
