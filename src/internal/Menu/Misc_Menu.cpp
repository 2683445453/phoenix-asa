#include "StartupPresentation.h"
#pragma once
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../Config/Configs.h"
#include "ConfigImGui.h"
#include "Misc_Menu.h"
#include "../Hack/Hack.h"
#include "../Config/ConfigManager.h"
#include "../AActor/Tick.h"
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
		const char* chkNotes = LanguageManager::Misc_Menu::UnlockNoteXPBuff;
		const char* chkFeed = LanguageManager::Misc_Menu::AutoFeed;
		const char* autoFeedRange = LanguageManager::Misc_Menu::AutoFeedRange;
		const char* autoFeedInterval = LanguageManager::Misc_Menu::AutoFeedInterval;
		const char* autoFeedFilter = LanguageManager::Misc_Menu::AutoFeedFilter;
		const char* autoFeedFilterHint = LanguageManager::Misc_Menu::AutoFeedFilterHint;
		const char* autoFeedStatus = LanguageManager::Misc_Menu::AutoFeedStatus;
		const char* chkAutoPickup = LanguageManager::Misc_Menu::AutoPickupDroppedItems;
		const char* autoPickupRange = LanguageManager::Misc_Menu::AutoPickupRange;
		const char* autoPickupInterval = LanguageManager::Misc_Menu::AutoPickupInterval;
		const char* autoPickupMaxWeight = LanguageManager::Misc_Menu::AutoPickupMaxWeight;
		const char* autoPickupUseDroppedFilter = LanguageManager::Misc_Menu::AutoPickupUseDroppedFilter;
		const char* autoPickupHint = LanguageManager::Misc_Menu::AutoPickupHint;
		const char* autoPickupStatus = LanguageManager::Misc_Menu::AutoPickupStatus;
		const char* enableNearbyDaeodonHealing = LanguageManager::Misc_Menu::EnableNearbyDaeodonHealing;
		const char* daeodonHealingHint = LanguageManager::Misc_Menu::DaeodonHealingHint;
		const char* daeodonHealingStatus = LanguageManager::Misc_Menu::DaeodonHealingStatus;
		const char* chkGacha = LanguageManager::Misc_Menu::AutoOpenGachaCrystals;
		const char* gachaOpenInterval = LanguageManager::Misc_Menu::AutoOpenGachaInterval;
		const char* chkPullGacha = LanguageManager::Misc_Menu::AutoPullGachaCrystals;
		const char* pullGachaRange = LanguageManager::Misc_Menu::AutoPullGachaRange;
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
		const char* chkAutoRepair = LanguageManager::Misc_Menu::AutoRepairItems;
		const char* autoRepairThreshold = LanguageManager::Misc_Menu::AutoRepairThreshold;
		const char* autoRepairInterval = LanguageManager::Misc_Menu::AutoRepairInterval;
		const char* autoRepairIncludeBackpack = LanguageManager::Misc_Menu::AutoRepairIncludeBackpack;
		const char* autoRepairHint = LanguageManager::Misc_Menu::AutoRepairHint;
		const char* autoRepairStatus = LanguageManager::Misc_Menu::AutoRepairStatus;
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
		const char* chkFlyer = LanguageManager::Misc_Menu::SuperFlyer;
		const char* chkTurn = LanguageManager::Misc_Menu::ForceTurn;
		const char* chkLogDamage = LanguageManager::Misc_Menu::LogDamage;
		const char* armorRange = LanguageManager::Misc_Menu::ArmorRange;
		const char* automaticChecked = LanguageManager::Misc_Menu::AutomaticChecked;
		const char* freeCameraEnabled = LanguageManager::Misc_Menu::FreeCameraEnabled;
		const char* FastReconnectedChecked = LanguageManager::Misc_Menu::FastReconnected;
		const char* FastRespawnedChecked = LanguageManager::Misc_Menu::FastRespawned;
		const char* btnPotatoGraphics = LanguageManager::Misc_Menu::PotatoGraphics;
		const char* cbShowInfo = LanguageManager::Misc_Menu::ShowInfo;


        if (g_ActiveMenuPage != MenuPage::Automation && g_ActiveMenuPage != MenuPage::InterfaceSettings) return;
        BeginTabRegion("AutomationDetail");
        if (IsTopic(Topic::Care)) {
            ImGui::BeginDisabled(!g_Hook::UpdateCameraOK && !g_Hook::PostRenderOK);
            DrawCustomCheckbox(Tr(U8("自动认养幼崽")), &g_Config::bAutoClaimBabies);
            DrawCustomCheckbox(Tr(U8("自动拥抱留痕")), &g_Config::bAutoImprint);
            DrawCustomCheckbox(Tr(U8("自动给幼崽补充生肉")), &g_Config::bAutoFeedBabies);
            DrawCustomCheckbox(Tr(U8("自动给嘎查放 Y 种子")), &g_Config::bAutoFillGacha);
            DrawCustomCheckbox(Tr(U8("自动给耕地放化肥")), &g_Config::bAutoFertilize);
            DrawCustomCheckbox(Tr(U8("自动补充炮塔子弹")), &g_Config::bAutoFillTurrets);
            DrawCustomCheckbox(Tr(U8("自动激活炮塔")), &g_Config::bAutoActivateTurrets);
            DrawCustomCheckbox(Tr(U8("自动抽取胚胎（原生交互）")), &g_Config::bAutoExtractEmbryos);
            DrawCustomCheckbox(Tr(U8("自动解开附近恐龙流星锤")), &g_Config::bAutoRemoveBola);
            DrawCustomCheckbox(Tr(U8("自动骑乘附近本部落恐龙")), &g_Config::bAutoMount);
            if (auto Fade = ParameterFade("##CareParameters", g_Config::bAutoClaimBabies || g_Config::bAutoImprint ||
                g_Config::bAutoFeedBabies || g_Config::bAutoFillGacha || g_Config::bAutoFertilize || g_Config::bAutoFillTurrets ||
                g_Config::bAutoActivateTurrets || g_Config::bAutoExtractEmbryos || g_Config::bAutoRemoveBola || g_Config::bAutoMount); Fade) {
                DrawCustomSliderFloat(Tr(U8("照料搜索范围")), &g_Config::HusbandryRangeMeters, 1, 30, "%.0f", 1, "m");
                DrawCustomSliderFloat(Tr(U8("照料请求间隔")), &g_Config::HusbandryIntervalSeconds, .5f, 10, "%.1f", .5f, "s");
                ImGui::TextWrapped("%s", g_Hack::GetHusbandryStatus().c_str());
            }
            HelpText(Tr(U8("交互最远尝试 6 米；物资补给仍需容器可访问。留痕仅支持拥抱；幼崽补肉只适合肉食生物。没有对应径向菜单选项时不发送请求。")));
            ImGui::EndDisabled();
        }
        if (IsTopic(Topic::Utilities)) {
            static int LevelStat = 0;
            DrawCustomCombo(Tr(U8("升级属性")), &LevelStat, std::vector<const char*>{Tr(U8("生命")), Tr(U8("耐力")), Tr(U8("负重")), Tr(U8("近战伤害"))});
            ImGui::BeginDisabled(!g_Hack::CanAllocateLevel());
            if (DrawCustomButton(Tr(U8("分配一个可用升级点")))) {
                const int Stats[] = {0,1,7,8};
                g_Hack::RequestAllocateLevel(Stats[std::clamp(LevelStat,0,3)]);
            }
            ImGui::EndDisabled();
            HelpText(Tr(U8("关闭菜单后给当前坐骑或人物分配一个已有升级点；不增加经验。")));
 ImGui::BeginDisabled(!g_Hook::ActorTickOK);
				if (DrawCustomButton(chkNotes)) g_Hack::RequestNoteXPBuff();
                ImGui::SameLine();
                if (DrawCustomButton(Tr(U8("停止笔记解锁")))) g_Hack::CancelNoteXPBuff();
                ImGui::TextWrapped("%s", g_Hack::GetNoteXPBuffStatus().c_str());
                HelpText(Tr(U8("每次请求一轮，启用笔记经验 Buff；服务器决定实际解锁与奖励。")));
                DrawAnimatedSeparator();
                if (DrawCustomButton(Tr(U8("实验：原地打开上传面板")))) g_Hack::RequestOpenUploadTerminal();
                ImGui::TextWrapped("%s", g_Hack::GetUploadTerminalStatus().c_str());
                HelpText(Tr(U8("以人物背包打开原生贡品界面，不读取任何建筑。客户端仅提供界面；物资、恐龙和人物上传能否成功由服务器决定，当前尚未实机验证。")));
                DrawAnimatedSeparator();

 ImGui::EndDisabled();
}
        if (IsTopic(Topic::Taming)) {
 ImGui::BeginDisabled(!g_Hook::ActorTickOK);
				DrawCustomCheckbox(chkFeed, &g_Config::bAutoFeed);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_1", g_Config::bAutoFeed); Fade) {
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
				HelpText("%s", daeodonHealingHint);
				const std::string DaeodonStatus = g_Hack::GetDaeodonHealingStatus();
				ImGui::TextWrapped("%s: %s", daeodonHealingStatus, DaeodonStatus.c_str());

 ImGui::EndDisabled();
}
        if (IsTopic(Topic::Loot)) {
 ImGui::BeginDisabled(!g_Hook::ActorTickOK);
                DrawCustomCheckbox(Tr(U8("自动吸取附近空投／掉落包")), &g_Config::bAutoLootContainers);
                if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_2", g_Config::bAutoLootContainers); Fade) {
                    ImGui::Indent();
                    DrawCustomCheckbox(Tr(U8("吸取空投")), &g_Config::bAutoLootSupplyCrates);
                    DrawCustomCheckbox(Tr(U8("吸取掉落包／死亡包")), &g_Config::bAutoLootBags);
                    DrawCustomCheckbox(Tr(U8("吸取河狸窝")), &g_Config::bAutoLootBeaverDams);
                    DrawCustomCheckbox(Tr(U8("拾取野生飞龙／恐爪龙蛋")), &g_Config::bAutoLootWildEggs);
                    ImGui::TextWrapped(U8("%s"), Tr(U8("龙蛋共用吸包范围、间隔和负重限制；需满足原生拾取距离与视线条件。")));
                    DrawCustomSliderFloat(Tr(U8("吸包搜索范围")), &g_Config::AutoLootRangeMeters, 1.0f, 30.0f, "%.1f", 1.0f, "m");
                    DrawCustomSliderFloat(Tr(U8("吸包请求间隔")), &g_Config::AutoLootIntervalMs, 250.0f, 3000.0f, "%.0f", 50.0f, "ms");
                    DrawCustomSliderFloat(Tr(U8("吸包停止负重")), &g_Config::AutoLootMaxWeightPercent, 10.0f, 100.0f, "%.0f", 5.0f, "%");
                    ImGui::TextWrapped(U8("%s"), g_Hack::GetAutoLootStatus().c_str());
                    HelpText(Tr(U8("支持步行和骑乘，按人物位置搜索，每次吸取一个容器。保留距离、等级和权限检查；失焦或手动打开界面时暂停，上下坐骑后重新搜索。负重阈值按人物计算，单次批量转移可能超过阈值。")));
                    ImGui::Unindent();
                }
				DrawCustomCheckbox(chkAutoPickup, &g_Config::bAutoPickupDroppedItems);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_3", g_Config::bAutoPickupDroppedItems); Fade) {
					ImGui::Indent();
					DrawCustomSliderFloat(autoPickupRange, &g_Config::AutoPickupRangeMeters,
						1.0f, 5.0f, "%.1f", 0.1f, "m");
					DrawCustomSliderFloat(autoPickupInterval, &g_Config::AutoPickupIntervalMs,
						150.0f, 2000.0f, "%.0f", 50.0f, "ms");
					DrawCustomSliderFloat(autoPickupMaxWeight,
						&g_Config::AutoPickupMaxWeightPercent,
						10.0f, 100.0f, "%.0f", 5.0f, "%");
					DrawCustomCheckbox(autoPickupUseDroppedFilter,
						&g_Config::bAutoPickupUseDroppedItemFilter);
					HelpText("%s", autoPickupHint);
					const std::string PickupStatus = g_Hack::GetAutoPickupStatus();
					ImGui::TextWrapped("%s: %s", autoPickupStatus, PickupStatus.c_str());
					ImGui::Unindent();
				}

 ImGui::EndDisabled();
}
        if (IsTopic(Topic::Survival)) {
 ImGui::BeginDisabled(!g_Hook::ActorTickOK);
				DrawCustomCheckbox(chkAutoEatDrink, &g_Config::bAutoEatDrink);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_4", g_Config::bAutoEatDrink); Fade) {
					ImGui::Indent();
					DrawCustomSliderFloat(autoEatDrinkThreshold,
						&g_Config::AutoEatDrinkThresholdPercent,
						10.0f, 90.0f, "%.0f", 5.0f, "%");
					const std::string Status = g_Hack::GetAutoEatDrinkStatus();
					ImGui::TextDisabled("%s: %s", autoEatDrinkStatus, Status.c_str());
					ImGui::Unindent();
				}
				DrawCustomCheckbox(chkAutoMedicalBrew, &g_Config::bAutoMedicalBrew);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_5", g_Config::bAutoMedicalBrew); Fade) {
					ImGui::Indent();
					DrawCustomSliderFloat(medicalBrewThreshold,
						&g_Config::MedicalBrewThresholdPercent,
						5.0f, 95.0f, "%.0f", 5.0f, "%");
					ImGui::Unindent();
				}
				DrawCustomCheckbox(chkAutoEnergyBrew, &g_Config::bAutoEnergyBrew);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_6", g_Config::bAutoEnergyBrew); Fade) {
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
				DrawCustomCheckbox(Tr(U8("自动汤类增益（buff 到期自动续）")), &g_Config::bAutoBuffSoup);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_16", g_Config::bAutoBuffSoup); Fade) {
					ImGui::Indent();
					ImGui::TextDisabled("%s", Tr(U8("自动续杯类型")));
					DrawCustomCheckbox(Tr(U8("战斗牛排")), &g_Config::bSoupBattleTartare);
					DrawCustomCheckbox(Tr(U8("仙人掌汤")), &g_Config::bSoupCactusBroth);
					DrawCustomCheckbox(Tr(U8("绯红辣椒")), &g_Config::bSoupFocalChili);
					DrawCustomCheckbox(Tr(U8("菲拉咖喱")), &g_Config::bSoupFriaCurry);
					DrawCustomCheckbox(Tr(U8("卡琳汤")), &g_Config::bSoupCalienSoup);
					DrawCustomCheckbox(Tr(U8("拉撒路杂烩")), &g_Config::bSoupLazarusChowder);
					DrawCustomCheckbox(Tr(U8("耐力炖锅")), &g_Config::bSoupEnduroStew);
					DrawCustomCheckbox(Tr(U8("影子牛排")), &g_Config::bSoupShadowSteak);
					DrawCustomCheckbox(Tr(U8("蘑菇汤")), &g_Config::bSoupMushroomBrew);
					HelpText(Tr(U8("只识别原版增益汤类目；对应增益消失后自动吃一个补续，背包没有该汤时提示缺少。每次使用仍由服务器按原生规则结算。")));
					ImGui::TextDisabled("%s: %s", Tr(U8("汤类状态")), g_Hack::GetAutoBuffSoupStatus().c_str());
					ImGui::Unindent();
				}
				DrawCustomCheckbox(Tr(U8("着火自动投掷灭火手雷")), &g_Config::bAutoFireExtinguish);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_17", g_Config::bAutoFireExtinguish); Fade) {
					ImGui::Indent();
					DrawCustomSliderFloat(Tr(U8("两次投掷间隔")), &g_Config::FireExtinguishIntervalSeconds,
						2.0f, 60.0f, "%.0f", 1.0f, "s");
					HelpText(Tr(U8("检测到身体着火时，自动装备背包里的灭火手雷（跳过举起动画），短暂压低视角向脚下投掷，随后恢复原武器与视角。每次仍消耗一枚灭火手雷，灭火效果由服务器结算；装备失败、骑乘限制或界面打开会自动取消并提示。")));
					ImGui::TextDisabled("%s: %s", Tr(U8("灭火状态")), g_Hack::GetFireExtinguishStatus().c_str());
					ImGui::Unindent();
				}

 ImGui::EndDisabled();
}
        if (IsTopic(Topic::Gacha)) {
            DrawCustomCheckbox(Tr(U8("装备与建筑自动转入研磨机")), &g_Config::bAutoDepositGachaGear);
            HelpText(Tr(U8("开启自动转移前，请先存好贵重物品。会转移背包里已有的装备和建筑；仅排除已装备、快捷栏、蓝图、皮肤及禁止研磨的物品，不检查收藏标记。")));
            if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_7", g_Config::bAutoDepositGachaGear); Fade) {
                DrawCustomSliderFloat(Tr(U8("研磨机搜索范围")), &g_Config::AutoDepositGrinderRangeMeters, 2, 30, "%.0f", 1, "m");
                ImGui::TextWrapped("%s", g_Hack::GetGrinderDepositStatus().c_str());
                HelpText(Tr(U8("支持原版工业研磨机与 QoL+ 研磨机。独立于开水晶开关，只转入、不自动研磨；按最近可访问目标逐组转移，服务器校验距离、权限和容量。")));
            }
            DrawAnimatedSeparator();
 ImGui::BeginDisabled(!g_Hook::ActorTickOK);
				DrawCustomCheckbox(chkGacha, &g_Config::bAutoOpenGachaCrystals);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_8", g_Config::bAutoOpenGachaCrystals); Fade) {
					ImGui::Indent();
					DrawCustomSliderFloat(gachaOpenInterval, &g_Config::AutoOpenGachaIntervalMs,
						10.0f, 1000.0f, "%.0f", 10.0f, "ms");
					HelpText(Tr(U8("每批最多开启 32 颗，连续处理完整个背包；间隔按整批计算，等待同步时不重复请求同一颗。")));
					ImGui::TextWrapped("%s", g_Hack::GetAutoPullGachaStatus().c_str());
					DrawCustomCheckbox(chkPullGacha, &g_Config::bAutoPullGachaCrystals);
					if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_9", g_Config::bAutoPullGachaCrystals); Fade) {
						DrawCustomSliderFloat(pullGachaRange, &g_Config::AutoPullGachaRangeMeters,
							2.0f, 30.0f, "%.0f", 1.0f, "m");
					}
					ImGui::Unindent();
				}

 ImGui::EndDisabled();
}
        if (IsTopic(Topic::Fishing)) {
            DrawCustomCheckbox(Tr(U8("背包蓝图自动转入附近保险箱")), &g_Config::bAutoDepositFishingBlueprints);
            if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_10", g_Config::bAutoDepositFishingBlueprints); Fade) {
                DrawCustomSliderFloat(Tr(U8("蓝图保险箱搜索范围")), &g_Config::AutoDepositVaultRangeMeters, 2, 30, "%.0f", 1, "m");
                ImGui::TextWrapped("%s", g_Hack::GetBlueprintDepositStatus().c_str());
                HelpText(Tr(U8("转移背包内真实蓝图，包括已有蓝图；不转移学习的印痕、已装备或快捷栏物品。独立于 QTE 和连竿开关；手动打开容器或上传面板时暂停。")));
            }
            DrawAnimatedSeparator();
 ImGui::BeginDisabled(!g_Hook::ActorTickOK);
				DrawCustomCheckbox(chkFishing, &g_Config::bAutoFishingQTE);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_11", g_Config::bAutoFishingQTE); Fade) {
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

 ImGui::EndDisabled();
}
        if (IsTopic(Topic::Repair)) {
 ImGui::BeginDisabled(!g_Hook::ActorTickOK);
				DrawCustomCheckbox(chkAutoRepair, &g_Config::bAutoRepairItems);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_12", g_Config::bAutoRepairItems); Fade) {
					ImGui::Indent();
					DrawCustomSliderFloat(autoRepairThreshold,
						&g_Config::AutoRepairThresholdPercent,
						1.0f, 95.0f, "%.0f", 1.0f, "%");
					DrawCustomSliderFloat(autoRepairInterval,
						&g_Config::AutoRepairIntervalSeconds,
						0.5f, 10.0f, "%.1f", 0.5f, "s");
					DrawCustomCheckbox(autoRepairIncludeBackpack,
						&g_Config::bAutoRepairIncludeBackpack);
					HelpText("%s", autoRepairHint);
					const std::string RepairStatus = g_Hack::GetAutoRepairStatus();
					ImGui::TextWrapped("%s: %s", autoRepairStatus, RepairStatus.c_str());
					ImGui::Unindent();
				}


 ImGui::EndDisabled();
}
        if (IsTopic(Topic::Craft)) {
 ImGui::BeginDisabled(!g_Hook::ActorTickOK);
DrawCustomCheckbox(chkAutoCraft, &g_Config::bAutoCraft);
if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_13", g_Config::bAutoCraft); Fade) {
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


                HelpText(Tr(U8("持续分批补单，100 为缓冲队列；缺料或离开范围后会等待并自动继续。")));
				const std::string CraftStatus = g_Hack::GetAutoCraftStatus();
				ImGui::TextDisabled("%s: %s", autoCraftStatus, CraftStatus.c_str());

}

 ImGui::EndDisabled();
}
        if (IsTopic(Topic::Travel)) {
            DrawCustomCheckbox(Tr(U8("恐龙移动加速（客户端实验）")), &g_Config::bDinoMoveSpeed);
            HelpText(Tr(U8("专服仅实验修改当前坐骑的本地步行速度，可能回拉；实际加速尚未验证。关闭、下龙或打开菜单时恢复。单机仍支持游泳与飞行倍率。")));
            if (auto Fade = ParameterFade("##DinoMoveSpeed", g_Config::bDinoMoveSpeed); Fade)
                DrawCustomSliderFloat(Tr(U8("移动速度倍率")), &g_Config::DinoMoveSpeedMultiplier, .25f, 5, "%.2f", .25f, "x");
            DrawCustomCheckbox(Tr(U8("穴角兔大跳（实验）")), &g_Config::bJackalopeBigJump);
            if (auto Fade = ParameterFade("##JackalopeJump", g_Config::bJackalopeBigJump); Fade)
                DrawCustomSliderFloat(Tr(U8("穴角兔起跳力度倍率")), &g_Config::JackalopeJumpMultiplier, 1.0f, 20.0f, "%.1f", .1f, "x");
            HelpText(Tr(U8("调整当前穴角兔的基础起跳、前向速度、蓄力速度上限及最小垂直速度；也可能影响原生空中跳跃。保留蓄力、耐力和次数限制。关闭、下龙、打开菜单或失焦时恢复。联机效果待双端验证，客户端变快不代表服务器接受。")));
            DrawCustomCheckbox(Tr(U8("穴角兔蓄力速度上限（实验）")), &g_Config::bJackalopeChargeCap);
            if (auto Fade = ParameterFade("##JackalopeChargeCap", g_Config::bJackalopeChargeCap); Fade)
                DrawCustomSliderFloat(Tr(U8("蓄力速度上限倍率")), &g_Config::JackalopeChargeCapMultiplier, 1, 20, "%.1f", .1f, "x");
            DrawCustomCheckbox(Tr(U8("穴角兔加速蓄力（实验）")), &g_Config::bJackalopeFastCharge);
            if (auto Fade = ParameterFade("##JackalopeFastCharge", g_Config::bJackalopeFastCharge); Fade)
                DrawCustomSliderFloat(Tr(U8("蓄力加速倍率")), &g_Config::JackalopeChargeSpeedMultiplier, 1, 10, "%.1f", .5f, "x");
            HelpText(Tr(U8("可同时开启大跳、蓄力速度上限和加速蓄力。上限开启时独立决定最大速度倍率，不与大跳倍率相乘；加速5倍将蓄力时间缩短到原来的五分之一，松键起跳。保留原生蓄力值、耐力和跳跃次数限制。关闭或暂停时恢复。实际效果请在游戏内确认。")));
            DrawCustomCheckbox(Tr(U8("恐爪龙大跳")), &g_Config::bDeinoFlight);
            if (g_Config::bDeinoFlight) {
                DrawCustomSliderFloat(Tr(U8("地面大跳倍率")), &g_Config::DeinonychusJumpMultiplier,1,5,"%.1f",.1f,"x");
                DrawCustomSliderFloat(Tr(U8("空中水平倍率")), &g_Config::DeinoAirHorizontalMultiplier,1,30,"%.1f",.1f,"x");
                DrawCustomSliderFloat(Tr(U8("空中向上速度")), &g_Config::DeinoAirUpSpeed,0,30,"%.1f",.5f,"m/s");
                DrawCustomSliderFloat(Tr(U8("起跳保护时间")), &g_Config::DeinoTakeoffProtectionSeconds,.1f,5,"%.2f",.05f,"s");
                DrawCustomSliderFloat(Tr(U8("空中跳跃保护时间")), &g_Config::DeinoAirProtectionSeconds,.1f,5,"%.2f",.05f,"s");
                DrawCustomSliderFloat(Tr(U8("悬停请求间隔")), &g_Config::DeinoHoverIntervalMs,10,500,"%.0f",10,"ms");
                HelpText(Tr(U8("地面按空格大跳；空中再按空格向前跳，松开空格与方向键后悬停。水平按视角朝向计算，1倍为30米每秒，上限30倍；向上速度独立设置，不随倍率或抬头增大。")));
                HelpText(Tr(U8("起跳和空中跳跃各自的保护期内暂停悬停；过早再按空格不会排队。保护结束后松键由结束链接管，按住空格或WASD、Ctrl暂停悬停。默认10毫秒间隔，实际受游戏帧率限制，不积压补发。菜单、失焦、下骑或关闭后停止。参数支持保存。")));
                HelpText(Tr(U8("结束链已实测不额外扣飞扑耐力，但仍可能缓降；空中跳跃仍由服务器扣费和衰减。零耐力可继续悬停，不保证空中跳跃。新的水平分量与整合控制待实测，先从低倍率开始。")));
                ImGui::TextWrapped("%s",g_Hack::GetDeinoFlightStatus().c_str());
            }
            DrawCustomCheckbox(Tr(U8("浮空建筑（放置参数实验）")), &g_Config::bFloatingStructures);
            if (g_Config::bFloatingStructures) ImGui::TextWrapped("%s",g_Hack::GetFloatingStructureStatus().c_str());

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
			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			{
				DrawCustomCheckbox(automaticChecked, &g_Config::bAutomatic);
				if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_14", g_Config::bAutomatic); Fade) {
					DrawCustomSliderFloat(armorRange, &g_Config::ArmorRange, 0.1f, 100.0f, "%.1f", 0.1f, U8(""));
					DrawCustomCheckbox(Tr(U8("只替换已装备部位（不自动穿戴空槽位）")), &g_Config::bArmorSwapOnlyEquipped);
					HelpText(Tr(U8("开启后仅当该部位已经穿着护甲（或只剩皮肤）时才找备件替换，不会把空槽位自动穿上背包里的装备。")));
					ImGui::TextDisabled("%s", Tr(U8("参与替换的部位（5 个身体部位 + 副手）")));
					DrawCustomCheckbox(Tr(U8("头盔")), &g_Config::bArmorSwapHead);
					DrawCustomCheckbox(Tr(U8("胸甲")), &g_Config::bArmorSwapShirt);
					DrawCustomCheckbox(Tr(U8("护腿")), &g_Config::bArmorSwapPants);
					DrawCustomCheckbox(Tr(U8("靴子")), &g_Config::bArmorSwapBoots);
					DrawCustomCheckbox(Tr(U8("手套（手臂）")), &g_Config::bArmorSwapGloves);
					DrawCustomCheckbox(Tr(U8("副手盾牌")), &g_Config::bArmorSwapShield);
					HelpText(Tr(U8("每帧只请求替换一个部位，多个部位同时破损会依次排队；同槽位带皮肤的护甲也能正常替换。备件需为背包内可用、未破损的非皮肤装备。")));
					ImGui::TextWrapped("%s: %s", Tr(U8("换甲状态")), g_Hack::GetArmorSwapStatus().c_str());
				}
			}
			ImGui::EndDisabled();
			DrawAnimatedSeparator();

			ImGui::BeginDisabled(!g_Hook::UpdateCameraOK);
            {
                DrawCustomCheckbox(freeCameraEnabled, &g_Config::bFreeCameraEnabled);
                if (auto Fade=ParameterFade("##parameters_Misc_Menu.cpp_15", g_Config::bFreeCameraEnabled); Fade) {
                ImGui::TextWrapped("%s", g_Hack::GetFreeCameraStatus().c_str());
                DrawCustomSliderFloat(Tr(U8("本地相机飞行速度")), &g_Config::FreeCameraSpeedMeters, 1.0f, PhoenixInterface::MaxCameraSpeed, "%.0f", 1.0f, "m/s");
                HelpText(Tr(U8("先下坐骑，在快捷键页面设置开启键（默认 Z）。按快捷键进入／退出；也可关闭菜单后用此开关进入。WASD 移动、空格上升、Ctrl 下降、Shift 临时加速、鼠标转向；Esc 返回人物视角。")));
                HelpText(Tr(U8("仅移动本地相机，无需管理员权限；相机不会改变人物位置，人物仍受重力、碰撞和伤害影响。菜单、聊天、背包、失焦、死亡、骑乘及人物或世界切换时退出并恢复视角与输入。速度不扩大远程建造或交互权限。")));
                }
			}
			ImGui::EndDisabled();
			DrawAnimatedSeparator();

}
        if (IsTopic(Topic::Interface)) {

            DrawCustomCheckbox(lockLayout, &g_Config::bMenuLockResize);
            DrawCustomCombo(Tr(U8("菜单字号")), &g_Config::MenuScaleIdx, Menu_Scale);
            DrawCustomCheckbox(Tr(U8("悬停时显示功能说明与注意事项")), &g_Config::bShowFeatureHelp);
            DrawCustomCheckbox(Tr(U8("显示 phoenix 启动展示")), &g_Config::bShowWelcome);
            if (DrawCustomButton(Tr(U8("重播启动动画")))) PhoenixStartup::Current.Replay();
            DrawCustomCheckbox(Tr(U8("静态威胁提示（减少动态效果）")), &g_Config::bStaticThreatIndicators);
            if (DrawCustomButton(resetLayout)) g_Config::bMenuNeedReset = true;

}
        if (IsTopic(Topic::System)) {
			ImGui::TextColored(ThemeColors::GetAccent(), secMisc);
			DrawAnimatedSeparator();

			if (DrawCustomButton(btnPotatoGraphics)) {
				g_Config::bPotatoGraphics = true;
			}


			ImGui::BeginDisabled(!g_Hook::ActorTickOK);
			{
				if (DrawCustomButton(btnSuicide)) {
					g_Config::bSuicide = true;
				}
			}
			ImGui::EndDisabled();


			if (DrawCustomButton(btnDump)) {
				g_Hack::DumpServerInfo();
			}

			DrawAnimatedSeparator();

			ImGui::BeginDisabled(!g_Hook::PostRenderOK);
			{
				DrawCustomCheckbox(cbShowInfo, &g_Config::bShowInfo);
			}
			ImGui::EndDisabled();

			DrawAnimatedSeparator();
			Title(Tr(U8("客户端运行诊断")));
			const g_AActor::TickDiagnostics TickStats =
				g_AActor::GetTickDiagnostics();
			const double SuppressionPercent = TickStats.ActorCallbacks > 0
				? static_cast<double>(TickStats.SuppressedCallbacks) * 100.0 /
					static_cast<double>(TickStats.ActorCallbacks)
				: 0.0;
			ImGui::TextDisabled(Tr(U8("Actor 回调 %llu | 世界 Tick %llu | 已合并 %.1f%%")),
				static_cast<unsigned long long>(TickStats.ActorCallbacks),
				static_cast<unsigned long long>(TickStats.WorldTicks),
				SuppressionPercent);
			ImGui::TextDisabled(Tr(U8("上一世界帧 Actor %u | 功能耗时 %.3f ms | 峰值 %.3f ms")),
				TickStats.CallbacksInLastWorldFrame,
				TickStats.LastWorkMilliseconds,
				TickStats.MaximumWorkMilliseconds);
			HelpText(Tr(U8("全局自动化与 Lua OnWorldTick 现在每个 UWorld 帧只执行一次；这里可直接确认合并比例和客户端开销。")));
            DrawAnimatedSeparator();
            if (DrawCustomButton(btnUnload)) {
                ConfigManager::Get().SaveConfig("phoenix_autosave.ini");
                g_Hook::RequestUnload();
            }

}
        EndTabRegion();
    }
}
