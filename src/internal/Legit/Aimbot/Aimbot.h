#pragma once
#include <string>
#include <vector>
#include "AimTargetRules.h"

namespace g_Aimbot {
	struct TargetInfo {
		SDK::APrimalCharacter* Character = nullptr;
		float CurrentSpread = 0.0f;
		float HitChance = 0.0f;
		SDK::FVector BestComponentLocation{};
		AimBodyPart Part = AimBodyPart::None;
		float ScreenDistance = 999999.0f;
		float Health = 0.f;
		float Distance = 0.f;
		int BestBoneIndex = -1;
		bool bIsValid = false;
		bool bIsLocked = false;
		bool bIsTriggering = false;
	};
    bool InitializeProjectileEvents();
    bool IsProjectileShotEvent(SDK::UFunction* Function);
    void OverrideProjectileEvent(SDK::UObject* Object, SDK::UFunction* Function, void* Parameters);
    bool IsSelectedTarget(SDK::AActor* Actor);
	void Tick();
	void Reset();
	void DrawOverlay(SDK::UCanvas* Canvas);
    // Shared visual head/body points, independent of weapon and aim toggles.
    std::vector<SDK::FVector> GetDefenseLockPoints(SDK::APrimalCharacter* Target, const SDK::FVector& Camera);
	void OverrideWeaponAim(SDK::AShooterWeapon* Weapon, SDK::FVector& Direction);
	void OverrideShotgunSpread(SDK::AShooterWeapon_Instant* Weapon, float& Spread);
	void OverrideTekBowInstantView(SDK::UObject* ScriptObject, SDK::UFunction* ScriptNode,
		uint8_t* Locals, const uint8_t* Arguments, SDK::APrimalCharacter* Character);
	std::string GetAimbotStatus();
	std::string GetMagicBulletStatus();
	TargetInfo GetBestTarget();
}
