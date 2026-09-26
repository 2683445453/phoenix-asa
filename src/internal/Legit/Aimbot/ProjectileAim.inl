// Included in the private aim namespace after RefreshShotPoint.
#include "TekBowProfile.inl"
bool UsesProjectileFlight(SDK::AShooterWeapon* Weapon) {
    return Weapon && Weapon->IsA(SDK::AShooterWeapon_Projectile::StaticClass()) &&
        !(IsTekBow(Weapon) && Weapon->bIsAccessoryActive);
}
bool ProjectileProfile(SDK::AShooterWeapon* Weapon, double ExplicitSpeed, double& Speed, double& Gravity) {
    if (!UsesProjectileFlight(Weapon)) return false;
    auto* ProjectileWeapon = static_cast<SDK::AShooterWeapon_Projectile*>(Weapon);
    auto* Class = ProjectileWeapon->ProjectileClass.Get();
    const bool TekBow = IsTekBow(Weapon);
    int AmmoIndex = -1;
    if (TekBow && !ResolveTekBowArrow(Weapon, Class, AmmoIndex, Speed, ExplicitSpeed)) return false;
    if (!Class || !Class->ClassDefaultObject ||
        !Class->ClassDefaultObject->IsA(SDK::AShooterProjectile::StaticClass())) return false;
    auto* Default = static_cast<SDK::AShooterProjectile*>(Class->ClassDefaultObject);
    if (!Default->MovementComp) return false;
    if (!TekBow) {
      Speed = Default->MovementComp->InitialSpeed;
      if (Weapon->IsA(SDK::APrimalWeaponBow::StaticClass())) {
        auto* Bow = static_cast<SDK::APrimalWeaponBow*>(Weapon);
        const double Charge = Bow->PullingTimeForMaximumSpeed > 0 ?
            std::clamp(double(Bow->PullingTime / Bow->PullingTimeForMaximumSpeed), 0.0, 1.0) : 1.0;
        Speed = Bow->MinimumInitialSpeed + (Bow->MaximumInitialSpeed - Bow->MinimumInitialSpeed) * Charge;
        if (!Bow->bIsPullingString && std::isfinite(Bow->ProjectileSpeed) && Bow->ProjectileSpeed > 1)
            Speed = Bow->ProjectileSpeed;
      }
      if (std::isfinite(ExplicitSpeed) && ExplicitSpeed > 1) Speed = ExplicitSpeed;
      else Speed *= ProjectileWeapon->GetProjectileSpeedModifier();
    }
    auto* World = SDK::UWorld::GetWorld();
    auto* Settings = World ? World->K2_GetWorldSettings() : nullptr;
    if (!Settings) return false;
    const double WorldGravity = Settings->bGlobalGravitySet ? Settings->GlobalGravityZ : Settings->WorldGravityZ;
    Gravity = WorldGravity * Default->MovementComp->ProjectileGravityScale * ProjectileWeapon->GetProjectileGravityModifier();
    return std::isfinite(Speed) && Speed > 1 && Speed < 1e8 && std::isfinite(Gravity);
}

bool ProjectilePathClear(const SDK::FVector& Origin, const SDK::FVector& Direction,
    double Speed, double Gravity, double Time) {
    auto* PC = g_Util::GetLocalPC();
    auto* Target = Live(LockedTarget);
    if (!PC || !Target) return false;
    SDK::AActor* Ignored[] = {PC->Pawn, Live(AimOwner), Target, Live(AimWeapon)};
    SDK::TArray<SDK::AActor*> Ignore(Ignored, 4, 4);
    SDK::FVector Start = Origin;
    const int Steps = std::clamp(static_cast<int>(std::ceil(Time / 0.04)), 4, 128);
    for (int i = 1; i <= Steps; ++i) {
        const double T = Time * i / Steps;
        const SDK::FVector End = Origin + Direction * (Speed * T) + SDK::FVector{0, 0, Gravity * T * T * 0.5};
        SDK::FHitResult Hit{};
        if (SDK::UVictoryCore::VTraceSingleBP_IgnoreActorsArray(PC, &Hit, Start, End,
            Ignore, PC->Pawn, SDK::ECollisionChannel::ECC_GameTraceChannel1, 0, {}, false, false, 0) && Hit.bBlockingHit)
            return false;
        Start = End;
    }
    return true;
}

bool PrepareProjectileShot(SDK::AShooterWeapon* Weapon, const SDK::FVector& Origin,
    SDK::FVector& Direction, double ExplicitSpeed) {
    if (!RefreshShotPoint(Weapon, Origin, !g_Config::bProjectilePrediction)) return false;
    double Speed = 0, Gravity = 0;
    if (!ProjectileProfile(Weapon, ExplicitSpeed, Speed, Gravity)) {
        SetStatus("当前弹种的弹速或重力未匹配，保留原生发射");
        return false;
    }
    const auto Velocity = g_Config::bProjectilePrediction ? Live(LockedTarget)->GetVelocity(false) : SDK::FVector{};
    const auto Vector = [](const SDK::FVector& v) { return BallisticVector{v.X, v.Y, v.Z}; };
    const double Horizon = std::isfinite(g_Config::ProjectilePredictionMaxSeconds) ?
        std::clamp(g_Config::ProjectilePredictionMaxSeconds, 0.1f, 10.0f) : 3.0;
    const double Delay = g_Config::bProjectilePrediction && std::isfinite(g_Config::ProjectilePredictionDelayMs) ?
        std::clamp(g_Config::ProjectilePredictionDelayMs, 0.0f, 300.0f) * 0.001 : 0;
    auto Solution = PredictProjectile(Vector(Origin), Vector(ShotLocation), Vector(Velocity), Speed, Gravity, Horizon, Delay);
    if (!Solution) { SetStatus("投射物在预判时限内无法追上目标，保留原生发射"); return false; }
    const SDK::FVector Result{Solution->Direction.X, Solution->Direction.Y, Solution->Direction.Z};
    if (!ProjectilePathClear(Origin, Result, Speed, Gravity, Solution->Time)) {
        SetStatus("预计弹道被遮挡，保留原生发射"); return false;
    }
    Direction = Result;
    SetStatus(IsTekRifle(Weapon) ? std::format("泰克步枪投射物预判 | 预计飞行 {:.2f} 秒", Solution->Time) :
        std::format("投射物预判 | 预计飞行 {:.2f} 秒", Solution->Time));
    return true;
}
