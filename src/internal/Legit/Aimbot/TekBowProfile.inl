bool IsTekBow(SDK::AShooterWeapon* Weapon) {
    for (auto* Class = Weapon ? Weapon->Class : nullptr; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
        if (Class->GetName() == "WeapTekBow_C") return true;
    return false;
}
bool IsTekRifle(SDK::AShooterWeapon* Weapon) {
    for (auto* Class = Weapon ? Weapon->Class : nullptr; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
        if (Class->GetName() == "WeapTekRifle_C") return true;
    return false;
}
SDK::FProperty* BowField(SDK::UStruct* Type, const char* Name, const char* ExpectedType) {
    for (auto* Current = Type; Current; Current = Current->SuperStruct) {
        for (auto* Field = Current->ChildProperties; Field; Field = Field->Next) {
            if (Field->Name.ToString() != Name || !Field->ClassPrivate || Field->ClassPrivate->Name.ToString() != ExpectedType) continue;
            auto* Property = static_cast<SDK::FProperty*>(Field);
            if (Property->Offset < 0 || Property->ArrayDim != 1 || Property->ElementSize <= 0 ||
                Property->Offset > Type->Size - Property->ElementSize) return nullptr;
            return Property;
        }
    }
    return nullptr;
}
bool BowNumber(SDK::AShooterWeapon* Weapon, const char* Name, double& Value) {
    auto* Property = BowField(Weapon->Class, Name, "DoubleProperty");
    if (Property && Property->ElementSize == sizeof(double))
        std::memcpy(&Value, reinterpret_cast<uint8_t*>(Weapon) + Property->Offset, sizeof(double));
    else {
        Property = BowField(Weapon->Class, Name, "FloatProperty");
        if (!Property || Property->ElementSize != sizeof(float)) return false;
        float Number = 0; std::memcpy(&Number, reinterpret_cast<uint8_t*>(Weapon) + Property->Offset, sizeof(float));
        Value = Number;
    }
    return std::isfinite(Value);
}
bool ResolveTekBowArrow(SDK::AShooterWeapon* Weapon, SDK::UClass*& ProjectileClass,
    int& AmmoIndex, double& Speed, double ExplicitSpeed) {
    if (!IsTekBow(Weapon) || !Weapon->IsA(SDK::APrimalWeaponBow::StaticClass()) || Weapon->bIsAccessoryActive) return false;
    // The verified Tek bow leaves ShooterWeapon_Projectile::ProjectileClass
    // empty and selects from this blueprint table. Never call its selection
    // blueprint here: the instant branch performs traces and sends damage RPCs.
    auto* ArrayProperty = static_cast<SDK::FArrayProperty*>(BowField(Weapon->Class, "AmmoConfig", "ArrayProperty"));
    auto* IndexProperty = BowField(Weapon->Class, "CurrentAmmoTypeIndex", "IntProperty");
    if (!ArrayProperty || ArrayProperty->ElementSize != 16 || !ArrayProperty->InnerProperty ||
        !ArrayProperty->InnerProperty->ClassPrivate || ArrayProperty->InnerProperty->ClassPrivate->Name.ToString() != "StructProperty" ||
        !IndexProperty || IndexProperty->ElementSize != sizeof(int)) return false;
    auto* Inner = static_cast<SDK::FStructProperty*>(ArrayProperty->InnerProperty);
    if (!Inner->Struct || Inner->Struct->GetName() != "TekBowArrowSetup_Struct" ||
        Inner->ElementSize <= 0 || Inner->ElementSize > 4096 || Inner->Struct->Size > Inner->ElementSize) return false;
    struct ArrayView { uint8_t* Data; int32_t Num, Max; } Array{};
    std::memcpy(&Array, reinterpret_cast<uint8_t*>(Weapon) + ArrayProperty->Offset, sizeof(Array));
    std::memcpy(&AmmoIndex, reinterpret_cast<uint8_t*>(Weapon) + IndexProperty->Offset, sizeof(AmmoIndex));
    if (!Array.Data || Array.Num <= 0 || Array.Num > 32 || Array.Max < Array.Num || Array.Max > 64 ||
        AmmoIndex < 0 || AmmoIndex >= Array.Num) return false;
    SDK::FProperty* ProjectileField = nullptr;
    for (auto* Field = Inner->Struct->ChildProperties; Field; Field = Field->Next) {
        const auto Name = Field->Name.ToString();
        if (Name.rfind("Projectile_", 0) != 0 || !Field->ClassPrivate || Field->ClassPrivate->Name.ToString() != "ClassProperty") continue;
        auto* Property = static_cast<SDK::FProperty*>(Field);
        if (Property->Offset < 0 || Property->ElementSize != sizeof(SDK::UClass*) || Property->ArrayDim != 1 ||
            Property->Offset > Inner->ElementSize - sizeof(SDK::UClass*) || ProjectileField) return false;
        ProjectileField = Property;
    }
    if (!ProjectileField) return false;
    std::memcpy(&ProjectileClass, Array.Data + AmmoIndex * Inner->ElementSize + ProjectileField->Offset, sizeof(ProjectileClass));
    if (!ProjectileClass || !ProjectileClass->ClassDefaultObject ||
        !ProjectileClass->ClassDefaultObject->IsA(SDK::AShooterProjectile::StaticClass())) return false;
    auto* Bow = static_cast<SDK::APrimalWeaponBow*>(Weapon);
    double MinimumExplosive = 0, MaximumExplosive = 0;
    if (AmmoIndex == 1 && (!BowNumber(Weapon, "MinSpeedExplosiveArrow", MinimumExplosive) ||
        !BowNumber(Weapon, "MaxSpeedExplosiveArrow", MaximumExplosive))) return false;
    auto Computed = TekBowArrowSpeed(AmmoIndex, Array.Num, Bow->PullingTime, Bow->PullingTimeForMaximumSpeed,
        Bow->MinimumInitialSpeed, Bow->MaximumInitialSpeed, MinimumExplosive, MaximumExplosive);
    if (!Computed) return false;
    Speed = *Computed;
    if (AmmoIndex != 1) {
        if (std::isfinite(ExplicitSpeed) && ExplicitSpeed > 1) Speed = ExplicitSpeed;
        else if (!Bow->bIsPullingString && std::isfinite(Bow->ProjectileSpeed) && Bow->ProjectileSpeed > 1) Speed = Bow->ProjectileSpeed;
        else Speed *= Bow->GetProjectileSpeedModifier();
    }
    return std::isfinite(Speed) && Speed > 1;
}
