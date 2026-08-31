#pragma once
#include "../../external/CppSDK/SDK.hpp"
#include "../Util/Util.h"

namespace _DD {
	inline SDK::AShooterPlayerController* LocalSPC = nullptr;
	inline SDK::AShooterCharacter* LocalSC = nullptr;
	inline SDK::AShooterPlayerState* LocalSPS = nullptr;

	inline SDK::UWorld* World = nullptr;
	inline SDK::UEngine* Engine = nullptr;
	inline SDK::AShooterGameState* SGS = nullptr;

	__forceinline void Update() {
		_DD::Engine = nullptr;
		_DD::World = nullptr;
		_DD::SGS = nullptr;
		_DD::LocalSPC = nullptr;
		_DD::LocalSC = nullptr; 
		_DD::LocalSPS = nullptr;

		SDK::UEngine* Engine = SDK::UEngine::GetEngine();
		SDK::UWorld* World = SDK::UWorld::GetWorld();
		SDK::APlayerController* LocalPC = g_Util::GetLocalPC();

		if (Engine) {
			_DD::Engine = Engine;
		}

		if (World) {
		    _DD::World = World;
		}

		if (World && World->GameState && World->GameState->IsA(SDK::AShooterGameState::StaticClass())) {
			_DD::SGS = static_cast<SDK::AShooterGameState*>(World->GameState);
		}

		if (LocalPC && LocalPC->IsA(SDK::AShooterPlayerController::StaticClass())) {
			_DD::LocalSPC = static_cast<SDK::AShooterPlayerController*>(LocalPC);
		}

		if (LocalPC && LocalPC->Character && LocalPC->Character->IsA(SDK::AShooterCharacter::StaticClass())) {
			_DD::LocalSC = static_cast<SDK::AShooterCharacter*>(LocalPC->Character);
		}

		// While mounted, Character/Pawn can point at the controlled dino instead of
		// the survivor. Keep the human rider available for tribe/team checks and
		// make sure the local rider is not rendered as another ESP target.
		if (!_DD::LocalSC && LocalPC && LocalPC->Pawn &&
			LocalPC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
			SDK::APrimalDinoCharacter* MountedDino =
				static_cast<SDK::APrimalDinoCharacter*>(LocalPC->Pawn);
			SDK::AShooterCharacter* Rider = MountedDino->Rider.Get();
			if (Rider && Rider->Class && Rider->IsA(SDK::AShooterCharacter::StaticClass())) {
				_DD::LocalSC = Rider;
			}
		}

		if (LocalPC && LocalPC->PlayerState && LocalPC->PlayerState->IsA(SDK::AShooterPlayerState::StaticClass())) {
			_DD::LocalSPS = static_cast<SDK::AShooterPlayerState*>(LocalPC->PlayerState);
		}
	}
}
