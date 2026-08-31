#pragma once
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "../Config/Configs.h"
#include "../Util/Util.h"
#include "../Lua/LuaManager.h"
#include "../Hack/Hack.h"
#include "../../external/CppSDK/SDK.hpp"
#include "../Legit/Aimbot/Aimbot.h"

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

		g_Aimbot::Tick();
        g_Hack::OutBody();
        // Menu requests are queued from the render thread and applied here,
        // where Blueprint inventory/equipment events can safely run.
        g_Hack::ProcessTekStriderRigRequest(world);
        g_Hack::ProcessEnableNearbyDaeodonHealingRequest(world);
		if (g_Config::bSuicide) {
			g_Hack::Suicide(world);
			g_Config::bSuicide = false;
		}

		if (g_Config::bUnlockExplorerNotes) {
			g_Hack::UnlockExplorerNotes(world);
		}

		// Stateful range scanning keeps per-dino request cooldowns; run while
		// disabled as well so toggling the option clears that runtime state.
		g_Hack::AutoFeed(world);

        // Always run the state handler so disabling the option can close a
        // remote vault view that was opened by the automation.
        g_Hack::AutoOpenGachaCrystals(world);

        // Stateful so a held synthetic QTE key is always released even when
        // the option is disabled between the press and release ticks.
        g_Hack::AutoFishingQTE(world);

        // Normal inventory/equip RPCs are rate-limited inside these handlers.
        g_Hack::AutoEatDrink(world);
        g_Hack::AutoUseBrews(world);
        g_Hack::MaintainFishingRod(world);
		// These handlers retain small pieces of runtime state, so keep calling
        // them while disabled as well to restore state and update menu status.
		g_Hack::PumpShotgunNoReload(world);
		g_Hack::AutoCraft(world);
		g_Hack::AutoImprint(world);
		// The target inventory is cached while it is open. Helicoprion closes
		// its UI after a grind starts, but subsequent server requests can keep
		// using the same validated inventory component.
		g_Hack::AutoGrindHelicoprionInventory(world);

		// Uses the resource marker cache, normal mounted movement and a
		// standard primary-fire input. Keep calling while disabled so a
		// pending mouse release and temporary ForceTurn state are restored.
		g_Hack::AutoHarvestResources(world);

		// Stateful so disabling the option can restore the inventory's
        // original limits immediately instead of leaving a patched component.
        g_Hack::ExpandPlayerInventory(world);

		// Always run the state handler so landing, dismounting or disabling the
        // option restores the flyer's original movement flags.
        g_Hack::SuperFlyer(world);

        if (g_Config::bAutomatic) {
            g_Hack::AutoSwapBrokenEquipment();
        }

        g_Util::DropOrUseItem();

		LuaManager::Get().Lua_OnWorldTick();
	}
}
