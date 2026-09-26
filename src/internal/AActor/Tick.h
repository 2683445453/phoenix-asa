#pragma once

#include <cstdint>

namespace SDK {
	class AActor;
}

namespace g_AActor {
	struct TickDiagnostics {
		std::uint64_t ActorCallbacks = 0;
		std::uint64_t WorldTicks = 0;
		std::uint64_t SuppressedCallbacks = 0;
		std::uint32_t CallbacksInLastWorldFrame = 0;
		double LastWorkMilliseconds = 0.0;
		double MaximumWorkMilliseconds = 0.0;
	};

	void Tick(SDK::AActor* actor);
	TickDiagnostics GetTickDiagnostics();
}
