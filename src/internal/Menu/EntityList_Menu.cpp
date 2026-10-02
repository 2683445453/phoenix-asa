#pragma once
#include "../Config/Configs.h"
#include "ConfigImGui.h"
#include "EntityList_Menu.h"
#include "../Util/Util.h"
#include "../../external/CppSDK/SDK.hpp"
#include "../Language/LanguageManager.h"
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace g_DrawImGui {
	namespace {
		void DrawSpeciesCatalog() {
			if (!ImGui::CollapsingHeader(Tr(U8("游戏物种图鉴（含未加载物种）")))) return;
			auto* Data = SDK::UPrimalGameData::BPGetGameData();
			if (!Data || !Data->DinoEntriesObjects.IsValid() ||
				Data->DinoEntriesObjects.Num() > 5000) {
				ImGui::TextDisabled("%s", Tr(U8("物种图鉴尚未加载")));
				return;
			}
			static SDK::UPrimalGameData* CachedData = nullptr;
			static int CachedCount = -1;
			static std::vector<std::string> Names;
			if (CachedData != Data || CachedCount != Data->DinoEntriesObjects.Num()) {
				CachedData = Data;
				CachedCount = Data->DinoEntriesObjects.Num();
				Names.clear();
				std::unordered_set<std::string> Seen;
				for (auto* Entry : Data->DinoEntriesObjects) {
					if (!Entry || !Entry->IsA(SDK::UPrimalDinoEntry::StaticClass())) continue;
					std::string Name = Entry->DinoDescriptiveName.ToString();
					if (Name.empty()) Name = Entry->DinoNameTag.ToString();
					if (!Name.empty() && Seen.insert(Name).second) Names.push_back(std::move(Name));
				}
				std::sort(Names.begin(), Names.end());
			}
			ImGui::TextDisabled(Tr(U8("已注册物种：%d（等级、性别和特性请看下方已加载个体）")),
				static_cast<int>(Names.size()));
			ImGui::BeginChild("##species_catalog", ImVec2(0.0f, 180.0f), true);
			for (const auto& Name : Names)
				if (g_Util::IsEntityMatchMulti(Name, g_Config::entitySearchBuf))
					ImGui::TextUnformatted(Name.c_str());
			ImGui::EndChild();
		}
	}

	void EntityList_Menu() {
		const char* tabLabel = LanguageManager::EntityList_Menu::TabLabel;
		const char* secTitle = LanguageManager::EntityList_Menu::SectionTitle;
		const char* enableFilter = LanguageManager::EntityList_Menu::EnableFilter;
		const char* searchHint = LanguageManager::EntityList_Menu::SearchHint;
		const char* filterActive = LanguageManager::EntityList_Menu::FilterActive;

		if (BeginMenuPage(MenuPage::CreatureList, tabLabel)) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 14.0f));
			BeginTabRegion("EntityListRegion");

			ImGui::TextColored(ThemeColors::GetAccent(), secTitle);
			DrawAnimatedSeparator();

			DrawCustomCheckbox(enableFilter, &g_Config::bEnableFilter);
			ImGui::SameLine();

			ImGui::PushItemWidth(-1.0f);
			ImGui::InputTextWithHint("##EntitySearch", searchHint, g_Config::entitySearchBuf, IM_ARRAYSIZE(g_Config::entitySearchBuf));
			ImGui::PopItemWidth();

			// 显示当前激活的筛选词列表
			{
				std::string filter = g_Config::entitySearchBuf;
				if (!filter.empty()) {
					std::vector<std::string> tokens = g_Util::SplitFilterTokens(filter);
					std::vector<std::string> validTokens;
					for (const auto& t : tokens) {
						if (!t.empty()) validTokens.push_back(t);
					}
					if (!validTokens.empty()) {
						ImGui::Spacing();
						ImGui::TextDisabled(filterActive, validTokens.size());
						ImGui::SameLine();
						for (size_t i = 0; i < validTokens.size(); i++) {
							ImGui::TextColored(ThemeColors::GetAccent(), "%s", validTokens[i].c_str());
							if (i + 1 < validTokens.size()) ImGui::SameLine();
						}
					}
				}
			}

			ImGui::Spacing();
			DrawSpeciesCatalog();
			SDK::UWorld* World = SDK::UWorld::GetWorld();
			SDK::APlayerController* LocalPC = g_Util::GetLocalPC();

			if (World && LocalPC && LocalPC->Pawn) {
				SDK::AActor* LocalPawn = LocalPC->Pawn;
				int creatureCount = 0;
				std::unordered_set<SDK::AActor*> seen;
				auto ShowActor = [&](SDK::AActor* TargetActor) {

					if (!TargetActor || !seen.insert(TargetActor).second ||
						TargetActor == LocalPawn || TargetActor->bHidden) return;
					if (!TargetActor->IsA(SDK::APrimalCharacter::StaticClass())) return;

					SDK::APrimalCharacter* TargetChar = (SDK::APrimalCharacter*)TargetActor;
					if (TargetChar->IsDead()) return;
					const bool IsDino = TargetActor->IsA(SDK::APrimalDinoCharacter::StaticClass());
					if (IsDino) ++creatureCount;

					std::string gender = "?";
					if (LocalPC->Pawn && TargetActor && TargetChar) {
						gender = TargetActor->IsFemale() ? "F" : "M";
					}

					std::string displayName;
					if (gender != "?") {
						displayName = TargetChar->PlayerState ? TargetChar->PlayerState->GetPlayerName().ToString() + "-" + gender : TargetChar->GetDescriptiveName().ToString() + "-" + gender;
					}
					else {
						displayName = TargetChar->PlayerState ? TargetChar->PlayerState->GetPlayerName().ToString() : TargetChar->GetDescriptiveName().ToString();
					}

					if (displayName.empty() || displayName == "None") return;

					// 使用多关键词 OR 匹配替换原有的单关键词匹配
					if (g_Util::IsEntityMatchMulti(displayName, g_Config::entitySearchBuf)) {
						float dist = (LocalPC && LocalPC->Pawn && TargetActor) ? LocalPC->Pawn->GetDistanceTo(TargetActor) * 0.01f : 0.0f;
						float curHP = TargetChar->GetHealth();
						float maxHP = TargetChar->GetMaxHealth();
						int hpPercent = (maxHP > 0) ? (int)((curHP / maxHP) * 100.0f) : 0;
						int Level = 0;
						if (IsDino && TargetChar->MyCharacterStatusComponent)
							Level = TargetChar->MyCharacterStatusComponent->GetCharacterLevel();
						if (IsDino)
							ImGui::Text("[%dm] Lv%d %s (%d%%)", (int)dist, Level,
								displayName.c_str(), hpPercent);
						else
							ImGui::Text("[%dm] %s (%d%%)", (int)dist,
								displayName.c_str(), hpPercent);
						if (IsDino) {
							auto* Dino = static_cast<SDK::APrimalDinoCharacter*>(TargetActor);
							if (Dino->GeneTraits.Num() > 0 && Dino->GeneTraits.Num() <= 32) {
								std::string Traits;
								for (int TraitIndex = 0; TraitIndex < Dino->GeneTraits.Num(); ++TraitIndex) {
									if (!Traits.empty()) Traits += ", ";
									Traits += Dino->GeneTraits[TraitIndex].ToString();
								}
								ImGui::TextWrapped("%s %s", Tr(U8("特性:")), Traits.c_str());
							}
						}
					}
				};
				auto ShowLevel = [&](SDK::ULevel* Level) {
					if (!Level || !Level->Actors.IsValid() || Level->Actors.Num() > 200000) return;
					for (SDK::AActor* Actor : Level->Actors) ShowActor(Actor);
				};
				if (World->Levels.IsValid() && World->Levels.Num() <= 4096)
					for (SDK::ULevel* Level : World->Levels) ShowLevel(Level);
				else ShowLevel(World->PersistentLevel);
				ImGui::TextDisabled(Tr(U8("当前已加载生物：%d（远处未加载的生物不在此列表）")), creatureCount);
			}

			EndTabRegion();
			ImGui::PopStyleVar();
			EndMenuPage();
		}
	}
}
