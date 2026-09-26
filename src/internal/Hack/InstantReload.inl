// 秒换弹（泵动式霰弹枪／泰克榴弹发射器／制式步枪／制式弩）与弓秒蓄力。
// Included at the end of Hack.cpp after GameplayExtensions/FeatureExpansion.
// 全部改动按「记录原值 → 应用 → 关闭/换枪/断线/卸载恢复」处理；
// 服务器权威数值（扣弹、冷却、伤害）一律保留，只改本地时序与字段。
namespace {
    using namespace PhoenixReload;

    // 字段补丁统一用规则层的 PhoenixReload::NestedFieldPatch（离线有覆盖）。
    // 它支持：顶层浮点、结构体内浮点、结构体内位段 bool；并带对象身份校验、
    // 「外部改写则以其为新基线」以及针对位段/浮点各自的恢复逻辑。
    // 这里注入项目的「指针 + 索引 + 序列号」校验，避免 GC 槽位复用后写错对象。
    bool InstantReloadOwnerAlive(const SDK::UObject* Object, int ObjectIndex, int* Serial) {
        return TryGetLiveObjectSerial(Object, ObjectIndex, Serial) &&
            IsUsableObject(Object) && Object->Index == ObjectIndex;
    }

    struct InstantReloadRuntime {
        ExtensionObject Weapon;
        WeaponFamily Family = WeaponFamily::None;
        int ClipCapacity = 1;
        uint64_t LastDiagnosticScan = 0;
        std::string Identity;
    };
    InstantReloadRuntime InstantReload;
    NestedFieldPatch ReloadDurationPerAmmoCountPatch;
    NestedFieldPatch NoAnimReloadDurationPatch;
    NestedFieldPatch DontTakeAmmoOnReloadPatch;
    // 弓秒蓄力：满蓄力判定阈值补丁（当前手持弓的顶层浮点字段）。
    NestedFieldPatch BowMaxPullTimePatch;
    // 四个补丁共用同一套生命周期校验。
    void BindInstantReloadPatchLifetime() {
        for (NestedFieldPatch* Patch : {&ReloadDurationPerAmmoCountPatch, &NoAnimReloadDurationPatch,
                &DontTakeAmmoOnReloadPatch, &BowMaxPullTimePatch})
            Patch->Alive = &InstantReloadOwnerAlive;
    }
    // 弓种分类要遍历类链并反射关联物品，按武器对象缓存，换枪时才重算。
    SDK::AShooterWeapon* CachedBowWeapon = nullptr;
    BowFamily CachedBowFamily = BowFamily::None;
    // 渲染线程只读这些游戏线程写好的英文状态快照（§15.5：绘制侧不碰游戏对象）。
    std::string InstantReloadStatusSnapshot = "Instant reload is disabled";
    std::string InstantChargeStatusSnapshot = "Instant bow charge is disabled";
    // 泰克榴弹发射器弹药槽只读诊断（不写游戏内存，供实机核对武器机制）。
    std::string TekLauncherSlotDiagnostic = "尚未匹配泰克榴弹发射器";

    // 武器身份串：类链 + 关联物品类链。只靠单一类名在模组武器上容易失配。
    std::string InstantReloadIdentityOf(const SDK::AShooterWeapon* Weapon) {
        std::string Identity;
        if (!IsUsableObject(Weapon)) return Identity;
        Identity += NormalizedClassChain(Weapon);
        if (SDK::UObject* Item = ReadReflectedObjectValue(
            const_cast<SDK::AShooterWeapon*>(Weapon), "AssociatedPrimalItem")) {
            Identity += ' ';
            Identity += NormalizedClassChain(Item);
        }
        return Identity;
    }

    void RestoreInstantReloadPatches() {
        ReloadDurationPerAmmoCountPatch.Restore();
        NoAnimReloadDurationPatch.Restore();
        DontTakeAmmoOnReloadPatch.Restore();
    }
    // 秒换弹状态快照文案（游戏线程构造，渲染线程只读快照）。
    void BuildInstantReloadStatus(bool Authority) {
        std::string Text;
        if (InstantReload.Family == WeaponFamily::None)
            Text = "当前武器不属于秒换弹类型（霰弹枪／榴弹发射器／步枪／弩）";
        else if (!ReloadFamilyEnabled(InstantReload.Family))
            Text = std::format("已识别 {}，但该类型未在秒换弹筛选中启用",
                ReloadFamilyLabel(InstantReload.Family));
        else if (Authority && ReloadDurationPerAmmoCountPatch.Active && NoAnimReloadDurationPatch.Active)
            Text = "本地主机换弹时长字段已归零；实际装填效果待验证，保留原生扣弹";
        else
            Text = "换弹时长补丁未生效，保持原生流程";
        ExtensionStatus(InstantReloadStatusSnapshot, std::move(Text));
    }

    void ResetInstantReloadTracking() {
        RestoreInstantReloadPatches();
        InstantReload.Weapon = {};
        InstantReload.Family = WeaponFamily::None;
        InstantReload.ClipCapacity = 1;
        InstantReload.Identity.clear();
        InstantReload.LastDiagnosticScan = 0;
        ExtensionStatus(TekLauncherSlotDiagnostic, "尚未匹配泰克榴弹发射器");
        ExtensionStatus(InstantReloadStatusSnapshot, "等待手持武器就绪");
    }

    // 状态快照由游戏线程写入（ExtensionStatus 持锁）；这里在锁内复制一份，
    // 交给 TranslateCopy 翻译后返回：渲染线程因此不会碰游戏对象（§15.5）。
    std::string ReadStatusSnapshot(const std::string& Snapshot) {
        std::scoped_lock Lock(ExtensionStatusMutex);
        return Snapshot;
    }

    // 定义与 Hack.cpp 顶部的匿名命名空间前向声明配对（同一内部链接实体）。
    void InstantReloadTick(SDK::UWorld* World);
    void InstantChargeTick(SDK::UWorld* World);

    // 秒换弹字段补丁落到 FWeaponData（WeaponConfig）内的指定字段。
    // 三个目标字段类型不同（逐发时长/整体时长是 float、免耗弹是 bool 位段），
    // 统一由 NestedFieldPatch 按类型定位与恢复。
    void ApplyReloadConfigPatch(NestedFieldPatch& Patch, SDK::AShooterWeapon* Weapon,
        const char* FieldName, float Value) {
        Patch.ApplyNumber(Weapon, "WeaponConfig", FieldName, Value);
    }

    // 弹夹容量优先级：WeaponConfig.AmmoPerClip → 关联物品 BPGetMaxAmmo → 当前值。
    int ResolveClipCapacity(SDK::AShooterWeapon* Weapon) {
        int Capacity = 0;
        SDK::FProperty* Property = Weapon->Class
            ? FindReflectedProperty(Weapon->Class, "WeaponConfig") : nullptr;
        const bool ConfigOk = Property && Property->ClassPrivate && Property->Offset >= 0 &&
            Property->ArrayDim == 1 && Property->ElementSize >= int(sizeof(float)) &&
            Property->Offset <= Weapon->Class->Size - Property->ElementSize &&
            EqualsNameIgnoreCase(Property->ClassPrivate->Name.ToString(), "StructProperty");
        if (ConfigOk) {
            auto* StructProperty = static_cast<SDK::FStructProperty*>(Property);
            if (StructProperty->Struct) {
                if (SDK::FProperty* AmmoPerClip = FindReflectedProperty(StructProperty->Struct, "AmmoPerClip")) {
                    if (AmmoPerClip->ClassPrivate && AmmoPerClip->Offset >= 0 &&
                        AmmoPerClip->ElementSize >= int(sizeof(int32_t)) &&
                        AmmoPerClip->Offset <= Property->ElementSize - int(sizeof(int32_t)) &&
                        EqualsNameIgnoreCase(AmmoPerClip->ClassPrivate->Name.ToString(), "IntProperty")) {
                        const int32_t Raw = *reinterpret_cast<const int32_t*>(
                            reinterpret_cast<const uint8_t*>(Weapon) + Property->Offset + AmmoPerClip->Offset);
                        if (Raw >= 1 && Raw <= MaxSaneClipCapacity) Capacity = Raw;
                    }
                }
            }
        }
        if (Capacity <= 0) {
            if (SDK::UObject* ItemObject = ReadReflectedObjectValue(Weapon, "AssociatedPrimalItem")) {
                SDK::UClass* ItemClass = SDK::UPrimalItem::StaticClass();
                if (ItemClass && ItemObject->IsA(ItemClass))
                    Capacity = static_cast<SDK::UPrimalItem*>(ItemObject)->BPGetMaxAmmo();
            }
        }
        return SanitizeClipCapacity(Capacity, Weapon->GetCurrentAmmoInClip());
    }

    // 当帧手持弓的弓种（按武器对象缓存；换枪后重算一次）。
    BowFamily InstantBowFamilyOf(SDK::AShooterWeapon* Weapon) {
        if (CachedBowWeapon != Weapon) {
            CachedBowWeapon = Weapon;
            CachedBowFamily = Weapon ? ClassifyBow(InstantReloadIdentityOf(Weapon)) : BowFamily::None;
        }
        return CachedBowFamily;
    }

    // 泰克榴弹发射器弹药槽侦察：弹药类型数组 / 当前槽位 / 槽位结构字段。
    // 全部只读；用于实机核对「多弹种 + 逐槽装填 + 引爆模式切换」的实际字段布局。
    void ScanTekLauncherAmmoSlots(SDK::AShooterWeapon* Weapon) {
        std::string Text = Weapon->Class->GetName();
        for (const char* Name : {"NumSlots", "NextSlotToFire"}) {
            int32_t Value = 0;
            if (ReadReflectedInt32Value(Weapon, Name, &Value))
                Text += std::format("\n{} = {}", Name, Value);
        }
        for (const char* Name : {"SlotRechargeTime", "TimeToAdvanceSlot", "ReloadAnimPlayRate"}) {
            double Value = 0;
            if (ReadReflectedNumberValue(Weapon, Name, &Value))
                Text += std::format("\n{} = {:.3f}", Name, Value);
        }
        for (const char* Name : {"bIsInReloadSequence", "bIsInFullReloadSequence"}) {
            bool Value = false;
            if (ReadReflectedBoolValue(Weapon, Name, &Value))
                Text += std::format("\n{} = {}", Name, Value);
        }
        auto* Field = FindReflectedProperty(Weapon->Class, "Slots");
        if (Field && Field->ClassPrivate && Field->ArrayDim == 1 &&
            Field->ClassPrivate->Name.ToString() == "ArrayProperty" &&
            Field->ElementSize == 16 && Field->Offset >= 0 &&
            Field->Offset <= Weapon->Class->Size - 16) {
            struct ArrayView { const uint8_t* Data; int32_t Num, Max; };
            ArrayView Slots{};
            std::memcpy(&Slots, reinterpret_cast<const uint8_t*>(Weapon) + Field->Offset, sizeof(Slots));
            auto* Inner = static_cast<SDK::FArrayProperty*>(Field)->InnerProperty;
            if (Slots.Data && Slots.Num >= 0 && Slots.Num <= Slots.Max && Slots.Max <= 64 &&
                Inner && Inner->ClassPrivate && Inner->ClassPrivate->Name.ToString() == "StructProperty" &&
                Inner->ElementSize > 0 && Inner->ElementSize <= 4096) {
                auto* SlotType = static_cast<SDK::FStructProperty*>(Inner)->Struct;
                auto* LoadedField = SlotType ? FindReflectedProperty(SlotType, "bIsLoaded") : nullptr;
                if (LoadedField && LoadedField->ClassPrivate &&
                    LoadedField->ClassPrivate->Name.ToString() == "BoolProperty" &&
                    LoadedField->ArrayDim == 1 && LoadedField->ElementSize == 1 && LoadedField->Offset >= 0) {
                    auto* Loaded = static_cast<SDK::FBoolProperty*>(LoadedField);
                    const size_t Offset = size_t(Loaded->Offset) + Loaded->ByteOffset;
                    const uint8_t Mask = Loaded->FieldMask ? Loaded->FieldMask : Loaded->ByteMask;
                    if (Mask && Offset < size_t(Inner->ElementSize)) {
                        Text += std::format("\nSlots = {}", Slots.Num);
                        for (int I = 0; I < Slots.Num && I < 12; ++I)
                            Text += std::format("\n[{}] bIsLoaded = {}", I,
                                (Slots.Data[size_t(I) * Inner->ElementSize + Offset] & Mask) != 0);
                    }
                }
            }
        }
        ExtensionStatus(TekLauncherSlotDiagnostic, std::move(Text));
    }

    void InstantReloadTick(SDK::UWorld* World) {
        BindInstantReloadPatchLifetime();
        if (!g_Config::bInstantReload) {
            ResetInstantReloadTracking();
            ExtensionStatus(InstantReloadStatusSnapshot, "秒换弹未启用");
            return;
        }
        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = PC ? GetLocalHumanCharacter(World) : nullptr;
        SDK::AShooterWeapon* Weapon = IsUsableObject(Human) && IsUsableObject(Human->CurrentWeapon)
            ? Human->CurrentWeapon : nullptr;

        if (!IsUsableObject(Weapon)) {
            // 空手／换枪过渡／死亡：恢复到上一个武器对象上的补丁。
            ResetInstantReloadTracking();
            return;
        }
        if (InstantReload.Weapon.Get() != Weapon) {
            ResetInstantReloadTracking();
            InstantReload.Weapon = ExtensionObject(Weapon);
            InstantReload.Identity = InstantReloadIdentityOf(Weapon);
            InstantReload.Family = ClassifyReloadWeapon(InstantReload.Identity);
        }
        // 泵动霰弹枪若已启用旧的「霰弹枪自动换弹」，这里让位，避免两套逻辑同时改同一把枪。
        if (InstantReload.Family == WeaponFamily::PumpShotgun && g_Config::bPumpShotgunNoReload) {
            RestoreInstantReloadPatches();
            ExtensionStatus(InstantReloadStatusSnapshot,
                "泵动霰弹枪已由「霰弹枪自动换弹」接管，秒换弹让位");
            return;
        }

        if (InstantReload.Family == WeaponFamily::Launcher && g_Config::bInstantReloadLauncher) {
            const uint64_t Now = GetTickCount64();
            if (InstantReload.LastDiagnosticScan == 0 || Now - InstantReload.LastDiagnosticScan >= 2000) {
                InstantReload.LastDiagnosticScan = Now;
                ScanTekLauncherAmmoSlots(Weapon);
            }
        }
        if (!CanPatchReload(Weapon->Role == SDK::ENetRole::ROLE_Authority, InstantReload.Family)) {
            RestoreInstantReloadPatches();
            ExtensionStatus(InstantReloadStatusSnapshot,
                InstantReload.Family == WeaponFamily::Launcher
                    ? "泰克榴弹发射器仅提供槽位只读诊断；秒换弹未实现，保持原生流程"
                    : "远程秒换弹不可用：服务器控制装填；不修改弹夹、不自动请求换弹");
            return;
        }
        const bool Supported = Weapon->IsA(SDK::AShooterWeapon_Instant::StaticClass()) ||
            Weapon->IsA(SDK::AShooterWeapon_Projectile::StaticClass());
        if (!Supported || !ReloadFamilyEnabled(InstantReload.Family)) {
            RestoreInstantReloadPatches();
            if (InstantReload.Family != WeaponFamily::Launcher) {
                InstantReload.LastDiagnosticScan = 0;
                ExtensionStatus(TekLauncherSlotDiagnostic, "尚未匹配泰克榴弹发射器");
            }
            BuildInstantReloadStatus(false);
            return;
        }

        InstantReload.ClipCapacity = ResolveClipCapacity(Weapon);
        const bool Authority = Weapon->Role == SDK::ENetRole::ROLE_Authority;

        // 仅 authority 的普通枪械应用时长字段；实际动画/装填效果另行实测。
        ApplyReloadConfigPatch(ReloadDurationPerAmmoCountPatch, Weapon, "ReloadDurationPerAmmoCount", 0.0f);
        ApplyReloadConfigPatch(NoAnimReloadDurationPatch, Weapon, "NoAnimReloadDuration", 0.0f);
        if (Authority && g_Config::bInstantReloadProtectAmmo)
            DontTakeAmmoOnReloadPatch.ApplyBool(Weapon, "WeaponConfig", "bDontTakeAmmoOnReload", true);
        else
            DontTakeAmmoOnReloadPatch.Restore();

        BuildInstantReloadStatus(Authority);
    }

    // 弓秒蓄力：把当前手持弓的「满蓄力所需拉弓时间」压到近零。
    // 拥有 authority 时服务器使用同一对象，服务器端拉弓时间同样变短。
    // 每帧守卫状态：记录上次应用结果，避免每帧重跑类链反射。
    struct InstantChargeState {
        SDK::AShooterWeapon* Weapon = nullptr;
        BowFamily Family = BowFamily::None;
        bool Enabled = false;
        bool Authority = false;
        bool Ready = false;
        uint64_t NextCheck = 0;
    };
    InstantChargeState InstantChargeTickState;

    // 弓秒蓄力的每帧守卫：只有「手持弓、弓种或弓种开关、补丁/Owner 身份」发生变化，
    // 或 TTL 到期（兜底清理被游戏改回的字段）时才返回 true。
    // 这样每帧只做读取与按需重算，不再每帧跑类链反射与属性查找。
    bool InstantChargeTickGuard() {
        if (!InstantChargeTickState.Ready || GetTickCount64() >= InstantChargeTickState.NextCheck) return true;
        SDK::AShooterPlayerController* PC = GetLocalShooterController(SDK::UWorld::GetWorld());
        SDK::AShooterCharacter* Human = PC ? GetLocalHumanCharacter(SDK::UWorld::GetWorld()) : nullptr;
        SDK::AShooterWeapon* Weapon = IsUsableObject(Human) && IsUsableObject(Human->CurrentWeapon)
            ? Human->CurrentWeapon : nullptr;
        if (Weapon != InstantChargeTickState.Weapon) return true;
        if (Weapon) {
            const BowFamily Family = InstantBowFamilyOf(Weapon);
            if (Family != InstantChargeTickState.Family ||
                BowFamilyEnabled(Family) != InstantChargeTickState.Enabled) return true;
            // authority 会随主机/联机切换变化，状态文案依赖它。
            if ((Weapon->Role == SDK::ENetRole::ROLE_Authority) != InstantChargeTickState.Authority)
                return true;
            if (!BowMaxPullTimePatch.SameOwner(Weapon)) return true;
        }
        return false;
    }

    void InstantChargeTick(SDK::UWorld* World) {
        BindInstantReloadPatchLifetime();
        // 每帧重复做类链反射没有收益：状态与字段已应用过就跳过。
        if (!InstantChargeTickGuard()) return;
        auto& State = InstantChargeTickState;
        State = InstantChargeState{};
        State.Ready = true;
        State.NextCheck = GetTickCount64() + 250;
        if (!g_Config::bFastBow) {
            BowMaxPullTimePatch.Restore();
            ExtensionStatus(InstantChargeStatusSnapshot, "弓秒蓄力未启用");
            return;
        }
        SDK::AShooterPlayerController* PC = GetLocalShooterController(World);
        SDK::AShooterCharacter* Human = PC ? GetLocalHumanCharacter(World) : nullptr;
        SDK::AShooterWeapon* Weapon = IsUsableObject(Human) && IsUsableObject(Human->CurrentWeapon)
            ? Human->CurrentWeapon : nullptr;
        if (!IsUsableObject(Weapon) || !Weapon->IsA(SDK::APrimalWeaponBow::StaticClass())) {
            BowMaxPullTimePatch.Restore();
            ExtensionStatus(InstantChargeStatusSnapshot, "等待手持弓就绪（木弓／复合弓／泰克复合弓）");
            return;
        }
        auto* Bow = static_cast<SDK::APrimalWeaponBow*>(Weapon);
        // 泰克弓即时命中模式由蓝图直接结算，不经过拉弓蓄力。
        if (Bow->bIsAccessoryActive) {
            BowMaxPullTimePatch.Restore();
            ExtensionStatus(InstantChargeStatusSnapshot, "泰克弓即时命中模式不使用拉弓蓄力");
            return;
        }
        const BowFamily Family = InstantBowFamilyOf(Weapon);
        if (!BowFamilyEnabled(Family)) {
            BowMaxPullTimePatch.Restore();
            ExtensionStatus(InstantChargeStatusSnapshot, "已识别该弓，但对应弓种未在秒蓄力筛选中启用");
            return;
        }
        // 顶层浮点字段：偏移、类型与边界由 NestedFieldPatch 校验。
        BowMaxPullTimePatch.ApplyNumber(Weapon, nullptr, "PullingTimeForMaximumSpeed",
            InstantChargePullSeconds);
        if (!BowMaxPullTimePatch.Active) {
            ExtensionStatus(InstantChargeStatusSnapshot, "当前客户端未匹配弓的拉弓时间字段");
            return;
        }
        State.Weapon = Weapon;
        State.Family = Family;
        State.Enabled = true;
        const bool Authority = Weapon->Role == SDK::ENetRole::ROLE_Authority;
        State.Authority = Authority;
        ExtensionStatus(InstantChargeStatusSnapshot,
            // 满蓄力直接出箭需要在本地持有 authority 才能通知服务器，远程服不提供该路径。
            !g_Config::bFastBowForceShot ? "已就绪：拉弓时间已压到最低（仍按原生流程松键出箭）" :
            Authority ? "已就绪：拉弓时间已压到最低，并按满蓄力直接出箭" :
            "已就绪：拉弓时间已压到最低；远程服务器不提供强制出箭路径，请用原生松键出箭");
    }
}

void ResetInstantReload() {
    RestoreInstantReloadPatches();
    BowMaxPullTimePatch.Restore();
    ResetInstantReloadTracking();
    CachedBowWeapon = nullptr;
    CachedBowFamily = BowFamily::None;
    InstantChargeTickState = InstantChargeState{};
    ExtensionStatus(InstantReloadStatusSnapshot, "秒换弹未启用");
    ExtensionStatus(InstantChargeStatusSnapshot, "弓秒蓄力未启用");
    ExtensionStatus(TekLauncherSlotDiagnostic, "尚未匹配泰克榴弹发射器");
}

// 供 hooks.cpp 的弓开火拦截使用：当前手持弓是否应按「已满蓄力」直接出箭。
// 仅在「强制满蓄力出箭」开启、属于已启用弓种且拉弓时间字段读得到时才成立。
// 返回 true 时顺带把拉弓累计时间写成满蓄力阈值：出箭弹速由原生公式按
// PullingTime 计算，只压缩阈值而不抬 PullingTime 会打出最低弹速。
bool ShouldForceFullChargeShot(SDK::AShooterWeapon* Weapon) {
    if (!g_Config::bFastBow || !g_Config::bFastBowForceShot) return false;
    if (!IsUsableObject(Weapon) || !Weapon->IsA(SDK::APrimalWeaponBow::StaticClass())) return false;
    if (Weapon->Role != SDK::ENetRole::ROLE_Authority) return false;
    auto* Bow = static_cast<SDK::APrimalWeaponBow*>(Weapon);
    if (Bow->bIsAccessoryActive) return false;
    if (!BowFamilyEnabled(InstantBowFamilyOf(Weapon))) return false;
    // 需要字段补丁已把该弓的满蓄力阈值压到近零，避免误判原生满蓄力状态。
    if (!BowMaxPullTimePatch.Active || !BowMaxPullTimePatch.SameOwner(Weapon)) return false;
    Bow->PullingTime = InstantChargePullSeconds;
    return true;
}

std::string GetInstantReloadStatus() {
    return LanguageManager::Get().TranslateCopy(ReadStatusSnapshot(InstantReloadStatusSnapshot));
}

std::string GetInstantChargeStatus() {
    return LanguageManager::Get().TranslateCopy(ReadStatusSnapshot(InstantChargeStatusSnapshot));
}

std::string GetTekLauncherSlotStatus() {
    return LanguageManager::Get().TranslateCopy(ReadStatusSnapshot(TekLauncherSlotDiagnostic));
}
