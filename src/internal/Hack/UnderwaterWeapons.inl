// Included after WeaponWaterAccess.inl, inside g_Hack. Only the serialized
// game-thread callback owns these changes; no asset loads or engine calls occur.
namespace {
    std::string UnderwaterStatus = "水下枪械未启用";
    struct WeaponWaterTuning {
        SDK::AShooterWeapon* Pointer = nullptr;
        SDK::UClass* Class = nullptr;
        int32_t Index = -1, Serial = -1, ClassIndex = -1, ClassSerial = -1;
        bool Default = false;
        double PatchedAtWorldTime = -1;
        uint64_t SeenInventoryPass = 0, NextApply = 0;
        WaterAccess::Flag FiringField, EquippingField;
        BackpackRules::OwnedFlag Firing, Equipping;

        bool Capture(SDK::AShooterWeapon* Weapon) {
            if (!WaterAccess::GetFlags(Weapon, FiringField, EquippingField)) return false;
            SDK::UObject* Registered = nullptr;
            if (!WaterAccess::Registry(Weapon->Index, Registered, Serial) || Registered != Weapon ||
                !WaterAccess::Registry(Weapon->Class->Index, Registered, ClassSerial) || Registered != Weapon->Class) return false;
            Default = (Weapon->Flags & SDK::EObjectFlags::ClassDefaultObject) != 0;
            // A live UClass owns its CDO. That positive-serial class can anchor a
            // default whose own weak serial has not been allocated yet. Ordinary
            // instances must have their own generation; never invent one.
            if (Default ? (ClassSerial <= 0 || WaterAccess::DefaultWeapon(Weapon->Class) != Weapon) : Serial <= 0) return false;
            Pointer = Weapon; Index = Weapon->Index;
            Class = Weapon->Class; ClassIndex = Class->Index;
            return true;
        }
        SDK::AShooterWeapon* Get() const {
            return WaterAccess::CapturedWeapon(Pointer, Class, Index, Serial,
                ClassIndex, ClassSerial, Default) ? Pointer : nullptr;
        }
        void Apply(SDK::AShooterWeapon* Weapon, uint64_t Now) {
            if (Now < NextApply) return;
            NextApply = Now + 250;
            const auto Set = [&](const WaterAccess::Flag& Field, BackpackRules::OwnedFlag& Owned, bool Desired) {
                if (!Field.AccessValidated(Weapon, true)) return;
                const bool Current = Field.ReadValidated(Weapon);
                const bool Value = Owned.Apply(Current, Desired);
                if (Value != Current) Field.WriteValidated(Weapon, Value);
            };
            Set(FiringField, Firing, true); Set(EquippingField, Equipping, false);
        }
        void Restore() { if (auto* Weapon = Get()) {
            const auto Set = [&](const WaterAccess::Flag& Field, BackpackRules::OwnedFlag& Owned) {
                if (!Field.AccessValidated(Weapon, true)) return;
                const bool Current = Field.ReadValidated(Weapon);
                const bool Value = Owned.Restore(Current);
                if (Value != Current) Field.WriteValidated(Weapon, Value);
            };
            Set(FiringField, Firing); Set(EquippingField, Equipping);
        } }
    };
    std::vector<WeaponWaterTuning> WaterWeapons;
    SDK::AShooterWeapon* WaterCurrent = nullptr;
    SDK::AShooterWeapon* WaterCurrentDefault = nullptr;
    uint64_t NextWaterCurrentCheck = 0, NextWaterInventoryScan = 0;
    uint64_t WaterInventoryPass = 0, WaterCompletedPass = 0;
    int32_t WaterInventoryCursor = 0, WaterInventoryLimit = 0;
    std::vector<SDK::UPrimalItem*> WaterInventorySnapshot;
    size_t WaterMaintenanceCursor = 0;
    bool WaterInventoryScanning = false;
    constexpr unsigned WaterItemsPerFrame = 4, WaterMaintenancePerFrame = 4;
    constexpr size_t WaterMaximumTunings = 256;

    auto FindWaterWeapon(SDK::AShooterWeapon* Weapon) {
        // Pointer comparison is cheap and never dereferences a candidate. Only
        // the matched tuning crosses the complete captured-identity boundary.
        return std::find_if(WaterWeapons.begin(), WaterWeapons.end(),
            [Weapon](const auto& Tuning) { return Tuning.Pointer == Weapon; });
    }
}

void ResetUnderwaterWeapons() {
    WaterAccess::ValidationPass Validation;
    for (auto& Tuning : WaterWeapons) Tuning.Restore();
    WaterWeapons.clear(); WaterCurrent = nullptr; WaterCurrentDefault = nullptr;
    NextWaterCurrentCheck = NextWaterInventoryScan = 0;
    WaterInventoryPass = WaterCompletedPass = 0;
    WaterInventoryCursor = WaterInventoryLimit = 0; WaterMaintenanceCursor = 0;
    WaterInventorySnapshot.clear();
    WaterInventoryScanning = false;
    WaterAccess::ClearCaches();
}

template <class TReadWorldTime>
void UpdateUnderwaterWeaponsWithClock(SDK::AShooterWeapon* CurrentWeapon,
    const SDK::TArray<SDK::UPrimalItem*>& Items, uint64_t Now, TReadWorldTime&& ReadWorldTime) {
    WaterAccess::ValidationPass Validation;
    const auto Started = std::chrono::steady_clock::now();
    const auto HasBudget = [&] {
        return std::chrono::steady_clock::now() - Started < std::chrono::microseconds(750);
    };
    bool ReadClock = false;
    double WorldTime = -1;
    const auto PatchTime = [&] {
        if (!ReadClock) { ReadClock = true; WorldTime = ReadWorldTime(); }
        return WorldTime;
    };
    const auto Track = [&](SDK::AShooterWeapon* Weapon, uint64_t SeenPass) -> SDK::AShooterWeapon* {
        if (!Weapon) return nullptr;
        auto It = FindWaterWeapon(Weapon);
        if (It != WaterWeapons.end()) {
            if (auto* Live = It->Get()) {
                if (SeenPass) It->SeenInventoryPass = SeenPass;
                It->Apply(Live, Now); return Live;
            }
            It->Restore(); WaterWeapons.erase(It);
        }
        WeaponWaterTuning Tuning;
        if (!Tuning.Capture(Weapon)) return nullptr;
        if (WaterWeapons.size() >= WaterMaximumTunings) {
            if (Tuning.Default) return nullptr;
            auto Spare = std::find_if(WaterWeapons.begin(), WaterWeapons.end(),
                [&](const auto& T) { return T.Default && T.Pointer != WaterCurrentDefault && T.Class != Tuning.Class; });
            if (Spare == WaterWeapons.end()) return nullptr;
            Spare->Restore(); WaterWeapons.erase(Spare);
        }
        Tuning.SeenInventoryPass = SeenPass;
        if (Tuning.Default) Tuning.PatchedAtWorldTime = PatchTime();
        else {
            // Only a proven newborn may inherit originals from a previously
            // modified CDO. Old actors first equipped later keep their overrides.
            double CreatedAt = -1;
            if (WaterAccess::CreationTime(Weapon, CreatedAt)) {
                auto Default = std::find_if(WaterWeapons.begin(), WaterWeapons.end(),
                    [&](const auto& T) { return T.Default && T.Class == Tuning.Class; });
                if (Default != WaterWeapons.end() && Default->PatchedAtWorldTime > 0 &&
                    CreatedAt > Default->PatchedAtWorldTime && Default->Get()) {
                    Tuning.Firing = Default->Firing; Tuning.Equipping = Default->Equipping;
                }
            }
        }
        // Capture already verified this exact weapon and its fields.
        Tuning.Apply(Weapon, Now);
        WaterWeapons.push_back(Tuning); return Weapon;
    };
    const auto TrackDefault = [&](SDK::UClass* Class, uint64_t SeenPass) -> SDK::AShooterWeapon* {
        if (!Class) return nullptr;
        auto It = std::find_if(WaterWeapons.begin(), WaterWeapons.end(),
            [Class](const auto& T) { return T.Default && T.Class == Class; });
        if (It != WaterWeapons.end()) {
            if (auto* Live = It->Get()) {
                if (SeenPass) It->SeenInventoryPass = SeenPass;
                It->Apply(Live, Now); return Live;
            }
            It->Restore(); WaterWeapons.erase(It);
        }
        return Track(WaterAccess::DefaultWeapon(Class), SeenPass);
    };

    // The equipped weapon is handled before background inventory work. A swap
    // never waits for a full backpack pass; repeated unchanged frames are cheap.
    if (CurrentWeapon != WaterCurrent || Now >= NextWaterCurrentCheck) {
        if (CurrentWeapon != WaterCurrent) {
            auto Previous = FindWaterWeapon(WaterCurrent);
            if (Previous != WaterWeapons.end() && !Previous->Default) {
                Previous->Restore(); WaterWeapons.erase(Previous);
            }
        }
        WaterCurrent = CurrentWeapon; WaterCurrentDefault = nullptr;
        NextWaterCurrentCheck = Now + 250;
        if (auto* Live = Track(CurrentWeapon, 0)) WaterCurrentDefault = TrackDefault(Live->Class, 0);
        else if (WaterAccess::IsSupported(CurrentWeapon)) WaterCurrentDefault = TrackDefault(CurrentWeapon->Class, 0);
    }

    if (!WaterInventoryScanning && Now >= NextWaterInventoryScan) {
        WaterInventoryScanning = true; ++WaterInventoryPass;
        if (Items.Num() > 0) WaterInventorySnapshot.assign(Items.GetDataPtr(), Items.GetDataPtr() + Items.Num());
        else WaterInventorySnapshot.clear();
        WaterInventoryCursor = 0; WaterInventoryLimit = int32_t(WaterInventorySnapshot.size());
    }
    unsigned Processed = 0;
    while (WaterInventoryScanning && WaterInventoryCursor < WaterInventoryLimit &&
        Processed < WaterItemsPerFrame && HasBudget()) {
        auto* Item = WaterInventorySnapshot[WaterInventoryCursor++]; ++Processed;
        if (Items.Num() <= 0 || std::find(Items.GetDataPtr(), Items.GetDataPtr() + Items.Num(), Item) ==
            Items.GetDataPtr() + Items.Num()) continue;
        if (auto* Class = WaterAccess::ItemWeaponClass(Item)) TrackDefault(Class, WaterInventoryPass);
    }
    if (WaterInventoryScanning && WaterInventoryCursor >= WaterInventoryLimit) {
        // Removing/reordering items while a pass is in flight cannot prove that
        // an unseen class left the backpack. Only prune after a stable pass.
        const bool Stable = Items.Num() == WaterInventoryLimit &&
            (Items.Num() == 0 || std::equal(WaterInventorySnapshot.begin(), WaterInventorySnapshot.end(), Items.GetDataPtr()));
        if (Stable) WaterCompletedPass = WaterInventoryPass;
        WaterInventoryScanning = false;
        NextWaterInventoryScan = Stable ? Now + 500 : Now;
    }

    // Keep previously discovered defaults until a whole inventory pass finishes.
    // Removing them after each slice would flip settings on unvisited weapons.
    // Refresh/sweep only a bounded number of entries, with one identity check per
    // entry. No Get() calls inside nested searches or repeated flag writes.
    for (unsigned Count = 0; Count < WaterMaintenancePerFrame && !WaterWeapons.empty() && HasBudget(); ++Count) {
        WaterMaintenanceCursor %= WaterWeapons.size();
        auto& Tuning = WaterWeapons[WaterMaintenanceCursor];
        const bool Remove = Tuning.Default ?
            (WaterCompletedPass && Tuning.SeenInventoryPass < WaterCompletedPass && Tuning.Pointer != WaterCurrentDefault) :
            Tuning.Pointer != WaterCurrent;
        if (Remove) {
            Tuning.Restore(); WaterWeapons.erase(WaterWeapons.begin() + WaterMaintenanceCursor);
        } else if (Now >= Tuning.NextApply) {
            if (auto* Live = Tuning.Get()) { Tuning.Apply(Live, Now); ++WaterMaintenanceCursor; }
            else WaterWeapons.erase(WaterWeapons.begin() + WaterMaintenanceCursor);
        } else ++WaterMaintenanceCursor;
    }
    const char* Status = WaterWeapons.empty() ? "未找到已加载的适用枪械或泰克相枪" :
        "已启用水下装备与开火标记；扣弹、命中和伤害由服务器确认";
    if (UnderwaterStatus != Status) ExtensionStatus(UnderwaterStatus, Status);
}

// Deterministic clock entry point for offline lifecycle/performance fixtures.
void UpdateUnderwaterWeapons(SDK::AShooterWeapon* CurrentWeapon,
    const SDK::TArray<SDK::UPrimalItem*>& Items, uint64_t Now, double WorldTime = -1) {
    UpdateUnderwaterWeaponsWithClock(CurrentWeapon, Items, Now, [WorldTime] { return WorldTime; });
}
