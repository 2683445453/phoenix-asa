#pragma once
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../Config/Configs.h"
#include "ConfigImGui.h"
#include "Misc_Menu.h"
#include "../Hack/Hack.h"
#include "../Config/ConfigManager.h"
#include "../../external/CppSDK/SDK.hpp"
#include "../Language/LanguageManager.h"
#include <algorithm>
#include <cctype>

namespace g_DrawImGui {
	std::vector<const char*> Menu_Scale = {
	"75%",
	"100%",
	"125%",
	"150%",
	"200%"
	};

	void Misc_Menu() {
		const char* tabLabel = LanguageManager::Misc_Menu::TabLabel;
		const char* secMenu = LanguageManager::Misc_Menu::SectionMenu;
		const char* lockLayout = LanguageManager::Misc_Menu::LockLayout;
		const char* resetLayout = LanguageManager::Misc_Menu::ResetLayout;
		const char* secMisc = LanguageManager::Misc_Menu::SectionMisc;
		const char* btnSuicide = LanguageManager::Misc_Menu::Suicide;
		const char* btnUnload = LanguageManager::Misc_Menu::Unload;
		const char* btnDump = LanguageManager::Misc_Menu::DumpServerInfo;
		const char* chkNotes = LanguageManager::Misc_Menu::UnlockExplorerNotes;
		const char* chkFeed = LanguageManager::Misc_Menu::AutoFeed;
		const char* autoFeedRange = LanguageManager::Misc_Menu::AutoFeedRange;
		const char* autoFeedInterval = LanguageManager::Misc_Menu::AutoFeedInterval;
		const char* autoFeedFilter = LanguageManager::Misc_Menu::AutoFeedFilter;
		const char* autoFeedFilterHint = LanguageManager::Misc_Menu::AutoFeedFilterHint;
		const char* autoFeedStatus = LanguageManager::Misc_Menu::AutoFeedStatus;
		const char* enableNearbyDaeodonHealing = LanguageManager::Misc_Menu::EnableNearbyDaeodonHealing;
		const char* daeodonHealingHint = LanguageManager::Misc_Menu::DaeodonHealingHint;
		const char* daeodonHealingStatus = LanguageManager::Misc_Menu::DaeodonHealingStatus;
		const char* chkGacha = LanguageManager::Misc_Menu::AutoOpenGachaCrystals;
		const char* gachaOpenInterval = LanguageManager::Misc_Menu::AutoOpenGachaInterval;
		const char* chkPullGacha = LanguageManager::Misc_Menu::AutoPullGachaCrystals;
		const char* pullGachaRange = LanguageManager::Misc_Menu::AutoPullGachaRange;
		const char* pullGachaStatus = LanguageManager::Misc_Menu::AutoPullGachaStatus;
		const char* chkFishing = LanguageManager::Misc_Menu::AutoFishingQTE;
		const char* chkFishingRecast = LanguageManager::Misc_Menu::AutoFishingRecast;
		const char* fishingQTEStatus = LanguageManager::Misc_Menu::FishingQTEStatus;
		const char* chkAutoEatDrink = LanguageManager::Misc_Menu::AutoEatDrink;
		const char* autoEatDrinkThreshold = LanguageManager::Misc_Menu::AutoEatDrinkThreshold;
		const char* autoEatDrinkStatus = LanguageManager::Misc_Menu::AutoEatDrinkStatus;
		const char* chkAutoMedicalBrew = LanguageManager::Misc_Menu::AutoMedicalBrew;
		const char* medicalBrewThreshold = LanguageManager::Misc_Menu::MedicalBrewThreshold;
		const char* chkAutoEnergyBrew = LanguageManager::Misc_Menu::AutoEnergyBrew;
		const char* energyBrewThreshold = LanguageManager::Misc_Menu::EnergyBrewThreshold;
		const char* autoBrewStatus = LanguageManager::Misc_Menu::AutoBrewStatus;
		const char* chkAutoSwitchFishingRod = LanguageManager::Misc_Menu::AutoSwitchFishingRod;
		const char* chkAutoRecoverFishingBait = LanguageManager::Misc_Menu::AutoRecoverFishingBait;
		const char* fishingRodMaintenanceStatus = LanguageManager::Misc_Menu::FishingRodMaintenanceStatus;
		const char* chkPumpShotgun = LanguageManager::Misc_Menu::PumpShotgunNoReload;
		const char* chkAutoCraft = LanguageManager::Misc_Menu::AutoCraft;
		const char* autoCraftRange = LanguageManager::Misc_Menu::AutoCraftRange;
		const char* autoCraftInterval = LanguageManager::Misc_Menu::AutoCraftInterval;
		const char* autoCraftSearch = LanguageManager::Misc_Menu::AutoCraftSearch;
		const char* autoCraftSearchHint = LanguageManager::Misc_Menu::AutoCraftSearchHint;
		const char* autoCraftStation = LanguageManager::Misc_Menu::AutoCraftStation;
		const char* autoCraftRecipe = LanguageManager::Misc_Menu::AutoCraftRecipe;
		const char* autoCraftNoStation = LanguageManager::Misc_Menu::AutoCraftNoStation;
		const char* autoCraftNoRecipe = LanguageManager::Misc_Menu::AutoCraftNoRecipe;
		const char* autoCraftStatus = LanguageManager::Misc_Menu::AutoCraftStatus;
		const char* chkAutoImprint = LanguageManager::Misc_Menu::AutoImprint;
		const char* autoImprintRange = LanguageManager::Misc_Menu::AutoImprintRange;
		const char* autoImprintStatus = LanguageManager::Misc_Menu::AutoImprintStatus;
		const char* chkAutoGrindHelicoprion = LanguageManager::Misc_Menu::AutoGrindHelicoprion;
		const char* autoGrindHelicoprionStatus = LanguageManager::Misc_Menu::AutoGrindHelicoprionStatus;
		const char* chkPlayerInventory = LanguageManager::Misc_Menu::ExpandPlayerInventory;
		const char* playerInventorySlotLimit = LanguageManager::Misc_Menu::PlayerInventorySlotLimit;
		const char* showLostColonyCooldowns = LanguageManager::Misc_Menu::ShowLostColonyCooldowns;
		const char* lostColonyCooldownHeader = LanguageManager::Misc_Menu::LostColonyCooldownHeader;
		const char* lostColonyCooldownEmpty = LanguageManager::Misc_Menu::LostColonyCooldownEmpty;
		const char* chkFlyer = LanguageManager::Misc_Menu::SuperFlyer;
		const char* chkTurn = LanguageManager::Misc_Menu::ForceTurn;
		const char* chkLogDamage = LanguageManager::Misc_Menu::LogDamage;
		const char* armorRange = LanguageManager::Misc_Menu::ArmorRange;
		const char* automaticChecked = LanguageManager::Misc_Menu::AutomaticChecked;
		const char* outBodyChecked = LanguageManager::Misc_Menu::OutBodyChecked;
		const char* FastReconnectedChecked = LanguageManager::Misc_Menu::FastReconnected;
		const char* FastRespawnedChecked = LanguageManager::Misc_Menu::FastRespawned;
		const char* btnPotatoGraphics = LanguageManager::Misc_Menu::PotatoGraphics;
		const char* cbShowInfo = LanguageManager::Misc_Menu::ShowInfo;


		const bool IsAutomationPage = g_ActiveMenuPage == MenuPage::Automation;
		const bool IsInterfaceSettingsPage =
			g_ActiveMenuPage == MenuPage::InterfaceSettings;
		const bool PageVisible = g_UseSidebarNavigation
			? (IsAutomationPage || IsInterfaceSettingsPage)
			: BeginMenuPage(MenuPage::Automation, tabLabel);
		if (PageVisible) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 14.0f));
			BeginTabRegion("MiscRegion");
			static int AutomationCategory = 0;
			const ImVec4 Accent = ThemeColors::GetAccent();
			auto DrawCategory = [&](const char* Label, int Category) {
				const bool Selected = AutomationCategory == Category;
				ImGui::PushStyleColor(ImGuiCol_Header,
					Selected ? Accent : ThemeColors::BG);
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ThemeColors::YELLOW);
				if (ImGui::Selectable(Label, Selected, ImGuiSelectableFlags_None,
					ImVec2(150.0f, 36.0f))) {
					AutomationCategory = Category;
				}
				ImGui::PopStyleColor(2);
			};
			if (IsAutomationPage) {
				DrawCategory(U8("生存与消耗"), 0);
				ImGui::SameLine();
				DrawCategory(U8("生产与养成"), 1);
				ImGui::SameLine();
				DrawCategory(U8("生物与移动"), 2);
				DrawAnimatedSeparator();
			}

			if (IsInterfaceSettingsPage) {
			ImGui::TextColored(ThemeColors::GetAccent(), secMenu);
			DrawAnimatedSeparator();

			DrawCustomCheckbox(lockLayout, &g_Config::bMenuLockResize);
			ImGui::SameLine();

			DrawKeyBindButton("Menu Toggle Key", g_MDX12::g_MenuState::g_openKey);
			ImGui::SameLine();

			DrawCustomCombo(U8("##Menu_Scale"), &g_Config::MenuScaleIdx, Menu_Scale);
			ImGui::SameLine();

			if (DrawCustomButton(resetLayout)) {
				g_Config::bMenuNeedReset = true;
			}
			ImGui::SameLine();

			if (DrawCustomButton(btnUnload)) {
				ConfigManager::Get().SaveConfig("38s_autosave.ini");
				// Restore runtime changes on the game/render thread before the
				// asynchronous unloader disables callbacks and releases the DLL.
				g_Config::bSuperFlyer = false;
				g_Config::bForceTurn = false;
				g_Config::bOutBodyChecked = false;
				g_Config::bPumpShotgunNoReload = false;
				g_Config::bAutoOpenGachaCrystals = false;
				g_Config::bAutoPullGachaCrystals = false;
				g_Config::bAutoFishingQTE = false;
				g_Config::bAutoFishingRecast = false;
				g_Config::bAutoEatDrink = false;
				g_Config::bAutoMedicalBrew = false;
				g_Config::bAutoEnergyBrew = false;
				g_Config::bAutoSwitchFishingRod = false;
				g_Config::bAutoRecoverFishingBait = false;
				g_Config::bAutoCraft = false;
				g_Config::bAutoImprint = false;
				g_Config::bAutoGrindHelicoprionInventory = false;
				g_Config::bAutoHarvestResources = false;
				g_MDX12::g_MenuState::g_isOutBodyActive = false;
				SDK::UWorld* World = SDK::UWorld::GetWorld();
				g_Hack::OutBody();
				g_Hack::SuperFlyer(World);
				g_Hack::PumpShotgunNoReload(World);
				g_Hack::AutoOpenGachaCrystals(World);
				g_Hack::AutoFishingQTE(World);
				g_Hack::AutoEatDrink(World);
				g_Hack::AutoUseBrews(World);
				g_Hack::MaintainFishingRod(World);
				g_Hack::AutoHarvestResources(World);
				g_Hack::ForceTurn(nullptr, 0.0f);
				g_Hook::RequestUnload();
			}
			DrawAnimatedSeparator();

			ImGui::TextColored(ThemeColors::GetAccent(), secMisc);
			DrawAnimatedSeparator();

			if (DrawCustomButton(btnPotatoGraphics)) {
				g_Config::bPotatoGraphics = true;
			}
			ImGui::SameLine();

			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			{
				if (DrawCustomButton(btnSuicide)) {
					g_Config::bSuicide = true;
				}
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (DrawCustomButton(btnDump)) {
				g_Hack::DumpServerInfo();
			}

			DrawAnimatedSeparator();

			ImGui::BeginDisabled(!g_Hook::PostRenderOK);
			{
				DrawCustomCheckbox(cbShowInfo, &g_Config::bShowInfo);
			}
			ImGui::EndDisabled();
			}

			if (IsAutomationPage && AutomationCategory == 0) {
			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			{
				DrawCustomCheckbox(chkNotes, &g_Config::bUnlockExplorerNotes);
				DrawCustomCheckbox(chkFeed, &g_Config::bAutoFeed);
				if (g_Config::bAutoFeed) {
					ImGui::Indent();
					DrawCustomSliderFloat(autoFeedRange, &g_Config::AutoFeedRangeMeters,
						2.0f, 100.0f, "%.0f", 1.0f, "m");
					DrawCustomSliderFloat(autoFeedInterval, &g_Config::AutoFeedIntervalSeconds,
						0.25f, 30.0f, "%.2f", 0.25f, "s");
					ImGui::TextDisabled("%s", autoFeedFilter);
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::InputTextWithHint("##AutoFeedDinoFilter", autoFeedFilterHint,
						g_Config::AutoFeedDinoFilter, IM_ARRAYSIZE(g_Config::AutoFeedDinoFilter));
					const std::string FeedStatus = g_Hack::GetAutoFeedStatus();
					ImGui::TextWrapped("%s: %s", autoFeedStatus, FeedStatus.c_str());
					ImGui::Unindent();
				}
				if (DrawCustomButton(enableNearbyDaeodonHealing)) {
					g_Hack::RequestEnableNearbyDaeodonHealing();
				}
				ImGui::TextDisabled("%s", daeodonHealingHint);
				const std::string DaeodonStatus = g_Hack::GetDaeodonHealingStatus();
				ImGui::TextWrapped("%s: %s", daeodonHealingStatus, DaeodonStatus.c_str());
				DrawCustomCheckbox(chkAutoEatDrink, &g_Config::bAutoEatDrink);
				if (g_Config::bAutoEatDrink) {
					ImGui::Indent();
					DrawCustomSliderFloat(autoEatDrinkThreshold,
						&g_Config::AutoEatDrinkThresholdPercent,
						10.0f, 90.0f, "%.0f", 5.0f, "%");
					const std::string Status = g_Hack::GetAutoEatDrinkStatus();
					ImGui::TextDisabled("%s: %s", autoEatDrinkStatus, Status.c_str());
					ImGui::Unindent();
				}
				DrawCustomCheckbox(chkAutoMedicalBrew, &g_Config::bAutoMedicalBrew);
				if (g_Config::bAutoMedicalBrew) {
					ImGui::Indent();
					DrawCustomSliderFloat(medicalBrewThreshold,
						&g_Config::MedicalBrewThresholdPercent,
						5.0f, 95.0f, "%.0f", 5.0f, "%");
					ImGui::Unindent();
				}
				DrawCustomCheckbox(chkAutoEnergyBrew, &g_Config::bAutoEnergyBrew);
				if (g_Config::bAutoEnergyBrew) {
					ImGui::Indent();
					DrawCustomSliderFloat(energyBrewThreshold,
						&g_Config::EnergyBrewThresholdPercent,
						5.0f, 95.0f, "%.0f", 5.0f, "%");
					ImGui::Unindent();
				}
				if (g_Config::bAutoMedicalBrew || g_Config::bAutoEnergyBrew) {
					const std::string Status = g_Hack::GetAutoBrewStatus();
					ImGui::Indent();
					ImGui::TextDisabled("%s: %s", autoBrewStatus, Status.c_str());
					ImGui::Unindent();
				}
				DrawCustomCheckbox(chkGacha, &g_Config::bAutoOpenGachaCrystals);
				if (g_Config::bAutoOpenGachaCrystals) {
					ImGui::Indent();
					DrawCustomSliderFloat(gachaOpenInterval, &g_Config::AutoOpenGachaIntervalMs,
						10.0f, 1000.0f, "%.0f", 10.0f, "ms");
					DrawCustomCheckbox(chkPullGacha, &g_Config::bAutoPullGachaCrystals);
					if (g_Config::bAutoPullGachaCrystals) {
						DrawCustomSliderFloat(pullGachaRange, &g_Config::AutoPullGachaRangeMeters,
							2.0f, 30.0f, "%.0f", 1.0f, "m");
						const std::string Status = g_Hack::GetAutoPullGachaStatus();
						ImGui::TextDisabled("%s: %s", pullGachaStatus, Status.c_str());
					}
					ImGui::Unindent();
				}
				DrawCustomCheckbox(chkFishing, &g_Config::bAutoFishingQTE);
				if (g_Config::bAutoFishingQTE) {
					const std::string Status = g_Hack::GetFishingQTEStatus();
					ImGui::Indent();
					DrawCustomCheckbox(chkFishingRecast, &g_Config::bAutoFishingRecast);
					ImGui::TextDisabled("%s: %s", fishingQTEStatus, Status.c_str());
					ImGui::Unindent();
				}
				DrawCustomCheckbox(chkAutoSwitchFishingRod, &g_Config::bAutoSwitchFishingRod);
				DrawCustomCheckbox(chkAutoRecoverFishingBait, &g_Config::bAutoRecoverFishingBait);
				if (g_Config::bAutoSwitchFishingRod || g_Config::bAutoRecoverFishingBait) {
					const std::string Status = g_Hack::GetFishingRodMaintenanceStatus();
					ImGui::Indent();
					ImGui::TextDisabled("%s: %s", fishingRodMaintenanceStatus, Status.c_str());
					ImGui::Unindent();
				}
			}
			ImGui::EndDisabled();
			}

			if (IsAutomationPage && AutomationCategory == 1) {
			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			{
				DrawCustomCheckbox(chkPumpShotgun, &g_Config::bPumpShotgunNoReload);

				DrawCustomSliderFloat(autoCraftRange, &g_Config::AutoCraftRangeMeters,
					2.0f, 100.0f, "%.0f", 1.0f, "m");
				DrawCustomSliderFloat(autoCraftInterval, &g_Config::AutoCraftIntervalSeconds,
					0.25f, 10.0f, "%.2f", 0.25f, "s");

				const std::vector<g_Hack::AutoCraftStationView> CraftStations =
					g_Hack::GetAutoCraftCatalog(SDK::UWorld::GetWorld());
				static char AutoCraftSearchBuffer[128]{};
				auto NormalizeSearchText = [](std::string Value) {
					std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Character) {
						return static_cast<char>(std::tolower(Character));
						});
					return Value;
					};
				const std::string CraftSearchQuery = NormalizeSearchText(AutoCraftSearchBuffer);
				auto RecipeMatchesSearch = [&](const g_Hack::AutoCraftRecipeView& Recipe) {
					return CraftSearchQuery.empty() ||
						NormalizeSearchText(Recipe.Name).find(CraftSearchQuery) != std::string::npos;
					};
				auto StationMatchesSearch = [&](const g_Hack::AutoCraftStationView& Station) {
					return CraftSearchQuery.empty() ||
						NormalizeSearchText(Station.Name).find(CraftSearchQuery) != std::string::npos ||
						std::any_of(Station.Recipes.begin(), Station.Recipes.end(), RecipeMatchesSearch);
					};
				const int SelectedStationIndex = g_Hack::GetSelectedAutoCraftStation();
				const int SelectedRecipeIndex = g_Hack::GetSelectedAutoCraftRecipe();
				const g_Hack::AutoCraftStationView* SelectedStation = nullptr;
				for (const g_Hack::AutoCraftStationView& Station : CraftStations) {
					if (Station.InventoryObjectIndex == SelectedStationIndex) {
						SelectedStation = &Station;
						break;
					}
				}

				ImGui::TextDisabled("%s", autoCraftSearch);
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::InputTextWithHint("##AutoCraftSearch", autoCraftSearchHint,
					AutoCraftSearchBuffer, sizeof(AutoCraftSearchBuffer));

				ImGui::TextDisabled("%s", autoCraftStation);
				const char* StationPreview = SelectedStation ? SelectedStation->Name.c_str() : autoCraftNoStation;
				if (ImGui::BeginCombo("##AutoCraftStation", StationPreview)) {
					bool HasMatchingStation = false;
					for (const g_Hack::AutoCraftStationView& Station : CraftStations) {
						if (!StationMatchesSearch(Station)) continue;
						HasMatchingStation = true;
						const bool Selected = Station.InventoryObjectIndex == SelectedStationIndex;
						const std::string Label = Station.Name + " (" +
							std::to_string(static_cast<int>(Station.DistanceMeters)) + "m)##station" +
							std::to_string(Station.InventoryObjectIndex);
						if (ImGui::Selectable(Label.c_str(), Selected)) {
							g_Hack::SelectAutoCraftStation(Station.InventoryObjectIndex);
						}
						if (Selected) ImGui::SetItemDefaultFocus();
					}
					if (!HasMatchingStation) ImGui::TextDisabled("%s", autoCraftNoStation);
					ImGui::EndCombo();
				}

				ImGui::TextDisabled("%s", autoCraftRecipe);
				const g_Hack::AutoCraftRecipeView* SelectedRecipe = nullptr;
				if (SelectedStation) {
					for (const g_Hack::AutoCraftRecipeView& Recipe : SelectedStation->Recipes) {
						if (Recipe.ItemObjectIndex == SelectedRecipeIndex) {
							SelectedRecipe = &Recipe;
							break;
						}
					}
				}
				const char* RecipePreview = SelectedRecipe ? SelectedRecipe->Name.c_str() : autoCraftNoRecipe;
				ImGui::BeginDisabled(!SelectedStation || SelectedStation->Recipes.empty());
				if (ImGui::BeginCombo("##AutoCraftRecipe", RecipePreview)) {
					bool HasMatchingRecipe = false;
					for (const g_Hack::AutoCraftRecipeView& Recipe : SelectedStation->Recipes) {
						if (!RecipeMatchesSearch(Recipe)) continue;
						HasMatchingRecipe = true;
						const bool Selected = Recipe.ItemObjectIndex == SelectedRecipeIndex;
						const std::string Label = Recipe.Name + "##recipe" +
							std::to_string(Recipe.ItemObjectIndex);
						if (ImGui::Selectable(Label.c_str(), Selected)) {
							g_Hack::SelectAutoCraftRecipe(Recipe.ItemObjectIndex);
						}
						if (Selected) ImGui::SetItemDefaultFocus();
					}
					if (!HasMatchingRecipe) ImGui::TextDisabled("%s", autoCraftNoRecipe);
					ImGui::EndCombo();
				}
				ImGui::EndDisabled();

				DrawCustomCheckbox(chkAutoCraft, &g_Config::bAutoCraft);
				const std::string CraftStatus = g_Hack::GetAutoCraftStatus();
				ImGui::TextDisabled("%s: %s", autoCraftStatus, CraftStatus.c_str());

				DrawCustomCheckbox(chkAutoImprint, &g_Config::bAutoImprint);
				DrawCustomSliderFloat(autoImprintRange, &g_Config::AutoImprintRangeMeters,
					2.0f, 30.0f, "%.0f", 1.0f, "m");
				if (g_Config::bAutoImprint) {
					const std::string ImprintStatus = g_Hack::GetAutoImprintStatus();
					ImGui::TextDisabled("%s: %s", autoImprintStatus, ImprintStatus.c_str());
				}
				DrawCustomCheckbox(chkAutoGrindHelicoprion,
					&g_Config::bAutoGrindHelicoprionInventory);
				if (g_Config::bAutoGrindHelicoprionInventory) {
					const std::string GrindStatus = g_Hack::GetAutoGrindHelicoprionStatus();
					ImGui::TextWrapped("%s: %s", autoGrindHelicoprionStatus, GrindStatus.c_str());
				}
				DrawCustomCheckbox(chkPlayerInventory, &g_Config::bExpandPlayerInventory);
				if (g_Config::bExpandPlayerInventory) {
					ImGui::Indent();
					DrawCustomSliderFloat(playerInventorySlotLimit, &g_Config::PlayerInventorySlotLimit,
						350.0f, 5000.0f, "%.0f", 50.0f);
					ImGui::Unindent();
				}
				DrawCustomCheckbox(showLostColonyCooldowns, &g_Config::bShowLostColonyCooldowns);
				if (g_Config::bShowLostColonyCooldowns) {
					ImGui::Indent();
					ImGui::TextColored(ThemeColors::GetAccent(), "%s", lostColonyCooldownHeader);

					const std::vector<g_Hack::SkillCooldownView> Cooldowns =
						g_Hack::GetLostColonyCooldowns(SDK::UWorld::GetWorld());
					if (Cooldowns.empty()) {
						ImGui::TextDisabled("%s", lostColonyCooldownEmpty);
					}
					else {
						for (const g_Hack::SkillCooldownView& Cooldown : Cooldowns) {
							ImGui::Text("%s: %.1fs / %.1fs", Cooldown.SkillName.c_str(),
								Cooldown.RemainingSeconds, Cooldown.DurationSeconds);
						}
					}
					ImGui::Unindent();
				}

			}
			ImGui::EndDisabled();
			}

			if (IsAutomationPage && AutomationCategory == 2) {
			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			{
				DrawCustomCheckbox(chkFlyer, &g_Config::bSuperFlyer);
			}
			ImGui::EndDisabled();

			ImGui::BeginDisabled(!g_Hook::PhysicsRotationOK);
			{
				DrawCustomCheckbox(chkTurn, &g_Config::bForceTurn);
			}
			ImGui::EndDisabled();

			ImGui::BeginDisabled(!g_Hook::TakeDamageOK);
			{
				DrawColorPickerRow(chkLogDamage, &g_Config::bLogDamage, "LogDamageCol", g_Config::LogDamageColor);
			}
			ImGui::EndDisabled();

			// 好像没啥用
			/*
			ImGui::BeginDisabled(!g_Hook::ClientNotifyReconnectedOK);
			{
				DrawCustomCheckbox(FastReconnectedChecked, &g_Config::bFastReconnected);
			}
			ImGui::EndDisabled();
			*/

			ImGui::BeginDisabled(!g_Hook::ClientNotifyRespawnedOK);
			{
				DrawCustomCheckbox(FastRespawnedChecked, &g_Config::bFastRespawned);
			}
			ImGui::EndDisabled();

			/*
			ImGui::BeginDisabled(!g_Hook::ClientChatMessageOK);
			{
				DrawCustomCheckbox("bBlockedChat", &g_Config::bBlockedChat);
			}
			ImGui::EndDisabled();
			 */

			DrawAnimatedSeparator();
			ImGui::TextColored(ThemeColors::GetAccent(), U8("跨步者配件编辑器"));
			ImGui::TextWrapped(U8("瞄准一只跨步者或骑乘跨步者。修改会写入隐藏装备栏；仅支持单机/主机。"));
			static int TekStriderHeadRig = 0;
			static int TekStriderBodyRig = 1;
			static const std::vector<const char*> TekStriderHeadRigs = {
				U8("采集钻头"), U8("双联机枪"), U8("雷达"), U8("沉默炮")
			};
			static const std::vector<const char*> TekStriderBodyRigs = {
				U8("重型加农炮"), U8("货舱鞍袋"), U8("全向护盾"), U8("单向护盾")
			};
			DrawCustomCombo(U8("头部配件##TekStriderHeadRig"),
				&TekStriderHeadRig, TekStriderHeadRigs);
			DrawCustomCombo(U8("躯干配件##TekStriderBodyRig"),
				&TekStriderBodyRig, TekStriderBodyRigs);
			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			if (DrawCustomButton(U8("应用到目标跨步者"))) {
				g_Hack::RequestTekStriderRigChange(TekStriderHeadRig, TekStriderBodyRig);
			}
			ImGui::EndDisabled();
			const std::string TekStriderRigStatus = g_Hack::GetTekStriderRigStatus();
			ImGui::TextWrapped(U8("状态：%s"), TekStriderRigStatus.c_str());

			DrawAnimatedSeparator();
			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			{
				DrawCustomCheckbox(automaticChecked, &g_Config::bAutomatic);
				DrawCustomSliderFloat(armorRange, &g_Config::ArmorRange, 0.1f, 100.0f, "%.1f", 0.1f, U8(""));
			}
			ImGui::EndDisabled();
			DrawAnimatedSeparator();

			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			{
				DrawCustomCheckbox(outBodyChecked, &g_Config::bOutBodyChecked);
				ImGui::SameLine();
				DrawKeyBindButton("OutBody Key", g_MDX12::g_MenuState::g_OutBodyKey);
			}
			ImGui::EndDisabled();
			DrawAnimatedSeparator();
			}

			EndTabRegion();
			ImGui::PopStyleVar();
			EndMenuPage();
		}
	}
}
