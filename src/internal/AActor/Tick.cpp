#pragma once
#include "Tick.h"
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "../Config/Configs.h"
#include "../Util/Util.h"
#include "../Lua/LuaManager.h"
#include "../Hack/Hack.h"
#include "../Hack/GeneScannerMarkers.h"
#include "../../external/CppSDK/SDK.hpp"
#include "../Legit/Aimbot/Aimbot.h"
#include <atomic>
#include <chrono>

namespace {
	std::atomic<std::uint64_t> g_ActorCallbacks{ 0 };
	std::atomic<std::uint64_t> g_WorldTicks{ 0 };
	std::atomic<std::uint64_t> g_SuppressedCallbacks{ 0 };
	std::atomic<std::uint32_t> g_CallbacksInCurrentWorldFrame{ 0 };
	std::atomic<std::uint32_t> g_CallbacksInLastWorldFrame{ 0 };
	std::atomic<std::uint64_t> g_LastWorkNanoseconds{ 0 };
	std::atomic<std::uint64_t> g_MaximumWorkNanoseconds{ 0 };

	// AActor::Tick is invoked once for every ticking actor, but the work below is
	// world-scoped. ASA exposes the owning UPrimalWorld frame counter directly,
	// so paused worlds and time-dilation zero still get one maintenance pass.
	SDK::UWorld* g_LastTickWorld = nullptr;
	std::int32_t g_LastWorldFrameCounter = 0;

	bool BeginWorldFrame(SDK::UWorld* World) {
		g_ActorCallbacks.fetch_add(1, std::memory_order_relaxed);
		if (!World) {
			g_SuppressedCallbacks.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		if (World == g_LastTickWorld && World->FrameCounter == g_LastWorldFrameCounter) {
			g_CallbacksInCurrentWorldFrame.fetch_add(1, std::memory_order_relaxed);
			g_SuppressedCallbacks.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		g_CallbacksInLastWorldFrame.store(
			g_CallbacksInCurrentWorldFrame.exchange(1, std::memory_order_relaxed),
			std::memory_order_relaxed);
		g_LastTickWorld = World;
		g_LastWorldFrameCounter = World->FrameCounter;
		g_WorldTicks.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void RecordWorldTickDuration(const std::chrono::steady_clock::duration Duration) {
		const auto Nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(Duration).count());
		g_LastWorkNanoseconds.store(Nanoseconds, std::memory_order_relaxed);

		std::uint64_t Maximum = g_MaximumWorkNanoseconds.load(std::memory_order_relaxed);
		while (Nanoseconds > Maximum &&
			!g_MaximumWorkNanoseconds.compare_exchange_weak(Maximum, Nanoseconds,
				std::memory_order_relaxed, std::memory_order_relaxed)) {
		}
	}
}

namespace g_AActor {
    /*

    Everyone said coders are closer to god than to man. 
    But they say that because our LLMs. Without them, we're like everyone else.
     
    The idea that we control the LLMs, is an illusion. 
    They are a power developers should never have trifled with. 
    One that brought Segmentation Fault its doom. 
    If we don't mind our own fundamentals, it will do the same to us. 
    A Coder must understand this to be Senior, or Dev."
    
    */

	void Tick(SDK::AActor* actor) {
		SDK::UWorld* world = SDK::UWorld::GetWorld();
		if (!BeginWorldFrame(world)) return;
		const auto WorkStarted = std::chrono::steady_clock::now();

        // Aiming and crafting run on the serialized camera / viewport thread.
        // Menu requests are queued from the render thread and applied here,
        // where Blueprint inventory/equipment events can safely run.
        g_Hack::ProcessEnableNearbyDaeodonHealingRequest(world);
		if (g_Config::bSuicide) {
			g_Hack::Suicide(world);
			g_Config::bSuicide = false;
		}


		// Stateful range scanning keeps per-dino request cooldowns; run while
		// disabled as well so toggling the option clears that runtime state.
		g_Hack::AutoFeed(world);

        // Always run the state handler so disabling the option can close a
        // remote vault view that was opened by the automation.

        // Stateful so a held synthetic QTE key is always released even when
        // the option is disabled between the press and release ticks.
        g_Hack::AutoFishingQTE(world);

        // Normal inventory/equip RPCs are rate-limited inside these handlers.
        g_Hack::AutoEatDrink(world);
        g_Hack::AutoUseBrews(world);
        // 汤类增益自动续杯与着火自动灭火：关闭时也会调用以恢复现场。
        g_Hack::AutoBuffSoup(world);
        g_Hack::AutoFireExtinguish(world);
        // Both helpers use ordinary server-authoritative inventory/interaction
        // RPCs. Keep calling while disabled so their cooldown state and status
        // are reset immediately when a setting is turned off.
        g_Hack::AutoPickupDroppedItems(world);
        g_Hack::AutoRepairItems(world);
        g_Hack::MaintainFishingRod(world);
		// These handlers retain small pieces of runtime state, so keep calling
        // them while disabled as well to restore state and update menu status.
		g_Hack::PumpShotgunNoReload(world);


		// Uses the resource marker cache, normal mounted movement and the
		// selected mount's harvest control. Keep calling while disabled so a
		// pending mouse release and temporary ForceTurn state are restored.


		// Always run the state handler so landing, dismounting or disabling the
        // option restores the flyer's original movement flags.
        g_Hack::SuperFlyer(world);

        // 自动换甲（副手盾牌已并入部位筛选，跟随同一开关）
        if (g_Config::bAutomatic) {
            g_Hack::AutoSwapBrokenEquipment();
        }

		g_Util::DropOrUseItem();
		g_GeneScannerMarkers::Tick(world);

		LuaManager::Get().Lua_OnWorldTick();
		RecordWorldTickDuration(std::chrono::steady_clock::now() - WorkStarted);
	}

	TickDiagnostics GetTickDiagnostics() {
		TickDiagnostics Result{};
		Result.ActorCallbacks = g_ActorCallbacks.load(std::memory_order_relaxed);
		Result.WorldTicks = g_WorldTicks.load(std::memory_order_relaxed);
		Result.SuppressedCallbacks =
			g_SuppressedCallbacks.load(std::memory_order_relaxed);
		Result.CallbacksInLastWorldFrame =
			g_CallbacksInLastWorldFrame.load(std::memory_order_relaxed);
		Result.LastWorkMilliseconds = static_cast<double>(
			g_LastWorkNanoseconds.load(std::memory_order_relaxed)) / 1000000.0;
		Result.MaximumWorkMilliseconds = static_cast<double>(
			g_MaximumWorkNanoseconds.load(std::memory_order_relaxed)) / 1000000.0;
		return Result;
	}
}
