#pragma once
#include "../../external/CppSDK/SDK.hpp"
#include "../Config/Configs.h"
#include "../Hack/Hack.h"
#include "PhysicsRotation.h"

namespace g_UMovementComponent {
	void PhysicsRotation(SDK::UMovementComponent* rcx, float DeltaTime) {
		// Always run the state handler so disabling the feature or dismounting
		// restores the movement component's original rotation flags.
		g_Hack::ForceTurn(rcx, DeltaTime);
	}
}
