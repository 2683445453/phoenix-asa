// Fields verified from the live CL 829680 Blueprint classes. Never write CDOs.
namespace {
    ExpansionPatch TekPatches[12];
    ExtensionObject TekHuman, TekChest, TekGloves, TekChestClass, TekGloveClass;
    uint64_t NextTekBuffLookup = 0;
    std::atomic<SDK::UObject*> TekHeatEventWeapon{nullptr};
    std::atomic<SDK::UFunction*> TekHeatEventFunction{nullptr};
    std::atomic<uint64_t> TekHeatEventsSkipped{0};
    std::atomic<uint64_t> TekRifleHeatEventsSkipped{0}, TekSniperHeatEventsSkipped{0};
    std::atomic<unsigned> TekHeatMatchedKind{0};
    std::atomic<bool> TekHeatEventIsSniper{false};
    ExtensionObject TekHeatOwner;
    std::string TekEquipmentStatus = "泰克装备设置未启用";
    bool TekClassMatches(SDK::UObject* O, const char* Name) {
        if (!IsUsableObject(O)) return false;
        for (auto* C = O->Class; C; C = static_cast<SDK::UClass*>(C->SuperStruct))
            if (C->GetName() == Name) return true;
        return false;
    }
    SDK::UObject* FindEquippedTekBuff(SDK::AShooterCharacter* Human, const char* Name, ExtensionObject& CachedClass) {
        auto* C = CachedClass.Get();
        if (!C) { C = SDK::UObject::FindClassFast(Name); CachedClass = ExtensionObject(C); }
        auto* F = Human->Class->GetFunction("PrimalCharacter", "GetBuff");
        if (!C || !IsUsableObject(F) || F->Size != 16) return nullptr;
        struct { SDK::UObject* Class; SDK::UObject* Result; } Args{C, nullptr};
        Human->ProcessEvent(F, &Args);
        return IsUsableObject(Args.Result) ? Args.Result : nullptr;
    }
    bool PrepareTekHeatEvent(SDK::UObject* W, bool Sniper) {
        if (TekHeatOwner.Get() == W) return TekHeatEventFunction.load() != nullptr;
        TekHeatEventFunction.store(nullptr); TekHeatEventWeapon.store(nullptr);
        TekHeatOwner = ExtensionObject(W); TekHeatEventsSkipped.store(0);
        const char* Owner = Sniper ? "WeapTekSniper_C" : "WeapTekRifle_C";
        auto* Fire = W->Class->GetFunction(Owner, "BPFireWeapon");
        auto* Heat = W->Class->GetFunction(Owner, "AddHeat");
        if (!IsUsableObject(Fire) || !IsUsableObject(Heat) || Fire->Size != 0 ||
            Heat->Size != (Sniper ? 1 : 0) || !(static_cast<uint32_t>(Heat->FunctionFlags) & 0x200000u)) return false;
        struct ScriptStorage { const uint8_t* Data; int32_t Num, Max; } Script{};
        std::memcpy(&Script, reinterpret_cast<const uint8_t*>(Fire) + 0x60, sizeof(Script));
        if (!Script.Data || Script.Num < 0 || Script.Num > Script.Max || Script.Max > 4096 ||
            !PhoenixTek::IsHeatOnlyFireEvent({Script.Data, size_t(Script.Num)}, Heat->Name.ComparisonIndex, Sniper)) return false;
        TekHeatEventIsSniper.store(Sniper); TekHeatMatchedKind.store(Sniper ? 2u : 1u);
        TekHeatEventWeapon.store(W); TekHeatEventFunction.store(Fire); return true;
    }
}
bool SuppressTekHeatEvent(SDK::UObject* Object, SDK::UFunction* Function) {
    if (!g_Config::bTekNoHeat || !Function || Function != TekHeatEventFunction.load() || Object != TekHeatEventWeapon.load()) return false;
    TekHeatEventsSkipped.fetch_add(1);
    (TekHeatEventIsSniper.load() ? TekSniperHeatEventsSkipped : TekRifleHeatEventsSkipped).fetch_add(1);
    return true;
}
std::string GetTekEquipmentStatus() { std::scoped_lock L(ExtensionStatusMutex); return LanguageManager::Get().TranslateCopy(TekEquipmentStatus); }
void ResetTekEquipment() {
    TekHeatEventFunction.store(nullptr); TekHeatEventWeapon.store(nullptr); TekHeatOwner = {};
    for (auto& P : TekPatches) P.Restore();
    TekHuman = {}; TekChest = {}; TekGloves = {}; NextTekBuffLookup = 0;
}
void TekEquipmentTick(SDK::UWorld* World) {
    const bool Wanted = g_Config::bTekNoHeat || g_Config::bTekChestJetNoCooldown || g_Config::bTekGloveDashNoCooldown;
    auto* PC = GetLocalShooterController(World);
    auto* Human = IsUsableObject(PC) ? PC->GetPlayerCharacter() : nullptr;
    DWORD PID = 0; GetWindowThreadProcessId(GetForegroundWindow(), &PID);
    const bool Ready = Wanted && IsUsableObject(World) && IsUsableObject(Human) && !Human->IsDead() &&
        !IsFreeCameraActive() && PID == GetCurrentProcessId() &&
        PhoenixShortcuts::GameplayInputReady(PC, g_MDX12::g_MenuState::g_isOpen, false);
    if (!Ready) {
        ResetTekEquipment();
        ExtensionStatus(TekEquipmentStatus, Wanted ? "泰克设置已暂停，返回游戏后应用" : "泰克装备设置未启用"); return;
    }
    if (TekHuman.Get() != Human) { ResetTekEquipment(); TekHuman = ExtensionObject(Human); }
    for (auto& P : TekPatches) P.Seen = false;
    auto* W = IsUsableObject(Human->CurrentWeapon) ? Human->CurrentWeapon : nullptr;
    bool HeatReady = false;
    const bool Sniper = TekClassMatches(W, "WeapTekSniper_C");
    const bool TekWeapon = TekClassMatches(W, "WeapTekRifle_C") || Sniper;
    const bool Authority = TekWeapon && W->Role == SDK::ENetRole::ROLE_Authority;
    if (g_Config::bTekNoHeat && TekWeapon) {
        // Rifle heat is accepted from the owning client in the verified build.
        // Sniper heat is authoritative on the server: suppress it only when this
        // process owns authority, otherwise the client would show damaging-looking
        // shots that the server rejected.
        if (!Sniper || Authority) HeatReady = PrepareTekHeatEvent(W, Sniper);
        else { TekHeatEventFunction.store(nullptr); TekHeatEventWeapon.store(nullptr); TekHeatOwner = {}; }
        if (Authority) {
        const char* Fields[] = {"OverheatAmountPerShot", "LocalOverheatingAmount", "OverheatingAmount"};
        for (int I = 0; I < 3; ++I) TekPatches[I].Set(W, Fields[I], 0);
        if (Sniper) {
            TekPatches[3].Set(W, "OverheatDuration", 0);
            TekPatches[4].Set(W, "OverheatCompleted", 1);
        } else {
            TekPatches[3].Set(W, "OverheatedDelayInterval", 0);
            TekPatches[4].Set(W, "TimeAfterOverheatingToAllowFire", 0);
        }
        }
    }
    else { TekHeatEventFunction.store(nullptr); TekHeatEventWeapon.store(nullptr); TekHeatOwner = {}; }
    if (GetTickCount64() >= NextTekBuffLookup) {
        NextTekBuffLookup = GetTickCount64() + 250;
        TekChest = ExtensionObject(g_Config::bTekChestJetNoCooldown ? FindEquippedTekBuff(Human, "Buff_TekArmor_Shirt_Rework_C", TekChestClass) : nullptr);
        TekGloves = ExtensionObject(g_Config::bTekGloveDashNoCooldown ? FindEquippedTekBuff(Human, "Buff_TekArmor_Gloves_C", TekGloveClass) : nullptr);
    }
    if (g_Config::bTekChestJetNoCooldown) if (auto* B = TekChest.Get()) {
        TekPatches[5].Set(B, "boostCooldownTime", 0);
        TekPatches[6].Set(B, "bCanBoost", 1);
    }
    if (g_Config::bTekGloveDashNoCooldown) if (auto* B = TekGloves.Get()) TekPatches[7].Set(B, "PunchCooldown", 0);
    int Matched = 0;
    for (auto& P : TekPatches) { if (!P.Seen) P.Restore(); if (P.Offset >= 0 && P.Owner.Get()) ++Matched; }
    ExtensionStatus(TekEquipmentStatus, Sniper && !Authority ?
        "泰克狙击枪热量由远程服务器维护；已保留服务器同步状态，需服务端组件才能真正连续射击" :
        HeatReady ? FormatLocalized("专用加热回调已匹配，已处理 {} 次；以实际扣弹和伤害为准", TekHeatEventsSkipped.load()) : Matched ?
        "已调整当前泰克装备的专用热量／冷却参数，实际效果待联机验证" :
        "未匹配已装备的泰克步枪、狙击枪或护甲能力");
}
