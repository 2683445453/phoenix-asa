#if defined(__cpp_char8_t)
#define U8(str) reinterpret_cast<const char*>(u8##str)
#else
#define U8(str) u8##str
#endif

#include "../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "../../external/CppSDK/SDK.hpp"
// #include "../ESP/ESP.h"
#include "../Config/Configs.h"
// #include "../ESP/DrawESP.h"
#include "ConfigImGui.h"
#include "DrawImGui.h"
#include "Aimbot_Menu.h"
#include "Visuals_Menu.h"
#include "EntityList_Menu.h"
#include "StructureList_Menu.h"
#include "ItemList_Menu.h"
#include "Misc_Menu.h"
#include "Language_Menu.h"
#include "Configs_Menu.h"
#include "Lua_Menu.h"
#include "../Log/LogManager.h"
#include "../Language/LanguageManager.h"
#include "../Config/ConfigManager.h"
#include "../Lua/LuaManager.h"
#include "../Util/Util.h"

#include <cstdio>
#include <string>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>

namespace g_DrawImGui {
	float g_MenuAlpha = 0.0f;
	bool g_UseSidebarNavigation = true;
	MenuPage g_ActiveMenuPage = MenuPage::Automation;

	namespace {
		enum class MenuSection {
			Combat,
			Visuals,
			ResourceHarvest,
			Automation,
			Tools,
			Settings
		};

		MenuSection GetActiveMenuSection() {
			switch (g_ActiveMenuPage) {
			case MenuPage::Combat:
				return MenuSection::Combat;
			case MenuPage::Visuals:
			case MenuPage::TeamVisuals:
				return MenuSection::Visuals;
			case MenuPage::ResourceHarvest:
				return MenuSection::ResourceHarvest;
			case MenuPage::Inventory:
			case MenuPage::CreatureList:
			case MenuPage::StructureList:
			case MenuPage::Lua:
				return MenuSection::Tools;
			case MenuPage::Configs:
			case MenuPage::Language:
			case MenuPage::InterfaceSettings:
				return MenuSection::Settings;
			default:
				return MenuSection::Automation;
			}
		}

		const char* GetPageTitle() {
			switch (g_ActiveMenuPage) {
			case MenuPage::Combat: return U8("战斗与瞄准");
			case MenuPage::Visuals: return U8("视觉与资源");
			case MenuPage::TeamVisuals: return U8("队友显示");
			case MenuPage::ResourceHarvest: return U8("自动采集资源");
			case MenuPage::Inventory: return U8("背包整理");
			case MenuPage::CreatureList: return U8("生物列表");
			case MenuPage::StructureList: return U8("建筑列表");
			case MenuPage::Lua: return U8("脚本扩展");
			case MenuPage::InterfaceSettings: return U8("界面与系统");
			case MenuPage::Configs: return U8("配置管理");
			case MenuPage::Language: return U8("语言设置");
			default: return U8("自动化与实用");
			}
		}

		const char* GetPageDescription() {
			switch (g_ActiveMenuPage) {
			case MenuPage::Combat: return U8("集中管理瞄准、平滑与触发设置");
			case MenuPage::Visuals: return U8("玩家、生物、建筑与世界显示设置");
			case MenuPage::TeamVisuals: return U8("单独配置队友、友方生物与建筑显示");
			case MenuPage::ResourceHarvest: return U8("资源选择、高亮、自动靠近与水陆采集控制");
			case MenuPage::Inventory: return U8("搜索、使用或丢弃人物背包内物品");
			case MenuPage::CreatureList: return U8("按名称筛选附近玩家与生物");
			case MenuPage::StructureList: return U8("按名称和归属筛选附近建筑");
			case MenuPage::Lua: return U8("加载与管理可选 Lua 脚本");
			case MenuPage::InterfaceSettings: return U8("菜单外观、快捷键、系统操作与卸载");
			case MenuPage::Configs: return U8("创建、保存和载入功能配置");
			case MenuPage::Language: return U8("管理与切换菜单语言文件");
			default: return U8("生存、生产、养成、移动及快捷功能");
			}
		}

		bool DrawSidebarEntry(const char* Label, bool Selected, float Height,
			bool Secondary = false) {
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
				ImVec2(Secondary ? 12.0f : 14.0f, 8.0f));
			ImGui::PushStyleColor(ImGuiCol_Header, Selected ? ThemeColors::GetAccent() : ThemeColors::BG);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ThemeColors::YELLOW);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ThemeColors::ACCENT2);
			ImGui::PushStyleColor(ImGuiCol_Text, ThemeColors::TEXT);
			if (Secondary) ImGui::Indent(10.0f);
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			const ImVec2 ExpectedMin = ImGui::GetCursorScreenPos();
			const ImVec2 ExpectedMax(ExpectedMin.x + ImGui::GetContentRegionAvail().x,
				ExpectedMin.y + Height);
			DrawList->AddRectFilled(ImVec2(ExpectedMin.x + 4.0f, ExpectedMin.y + 4.0f),
				ImVec2(ExpectedMax.x + 4.0f, ExpectedMax.y + 4.0f), IM_COL32_BLACK);
			// An unselected ImGui::Selectable does not paint ImGuiCol_Header.
			// Draw the menu face explicitly so the offset shadow cannot cover it.
			DrawList->AddRectFilled(ExpectedMin, ExpectedMax,
				ImGui::GetColorU32(ThemeColors::BG));
			const bool Pressed = ImGui::Selectable(Label, Selected,
				ImGuiSelectableFlags_None, ImVec2(0.0f, Height));
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();
			DrawList->AddRect(Min, Max, IM_COL32_BLACK, 0.0f, 0, 2.0f);
			if (Selected)
				DrawList->AddRectFilled(Min, ImVec2(Min.x + 6.0f, Max.y),
					ImGui::GetColorU32(ThemeColors::ACCENT2));
			if (Secondary) ImGui::Unindent(10.0f);
			ImGui::PopStyleColor(4);
			ImGui::PopStyleVar(2);
			return Pressed;
		}
	}

	void MyImGuiDraw(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
	{
		if (!g_Config::bInitWelcome) return;

		if (!style_initialized) { SetupCustomImGuiStyle(); style_initialized = true; }
		static bool g_PrevMenuState = false;
		bool isNowOpen = g_MDX12::g_MenuState::g_isOpen;

		if (isNowOpen && !g_PrevMenuState) {
			LanguageManager::Get().RefreshFileList();
			LuaManager::Get().RefreshFileList();
			ConfigManager::Get().RefreshFileList();
			LuaManager::Get().Lua_OnMenuOpen();
		}
		else if (!isNowOpen && g_PrevMenuState) {
			ConfigManager::Get().SaveConfig("38s_autosave.ini");
		}
		g_PrevMenuState = isNowOpen;

		ImGuiIO& io = ImGui::GetIO();
		ImVec2 screenSize = io.DisplaySize;
		g_MenuAlpha = isNowOpen ? 1.0f : 0.0f;

		// 定时写入自动预设，异常退出时也能尽量保留最近参数。
		static double nextAutoSaveAt = 0.0;
		const double now = ImGui::GetTime();
		if (!ConfigManager::Get().GetConfigDir().empty() && now >= nextAutoSaveAt) {
			ConfigManager::Get().SaveConfig("38s_autosave.ini");
			nextAutoSaveAt = now + 10.0;
		}

		if (g_MenuAlpha > 0.001f) {
			LuaManager::Get().Lua_OnPaintMenu(g_MenuAlpha);

			static int lastScaleIdx = g_Config::MenuScaleIdx;
			const float menu_size_w = 1160.f;
			const float menu_size_h = 760.f;

			const float fontSizes[] = { 14.0f, 18.0f, 22.0f, 26.0f, 34.0f };
			float menuScale = fontSizes[g_Config::MenuScaleIdx] / fontSizes[1];

			ImGui::GetStyle().WindowMinSize = ImVec2(menu_size_w * menuScale, menu_size_h * menuScale);
			// 处理重置逻辑
			if (g_Config::bMenuNeedReset) {
				g_Config::MenuScaleIdx = 1; // 重置为默认缩放
				ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.382, io.DisplaySize.y * 0.414), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
				ImGui::SetNextWindowSize(ImVec2(menu_size_w * menuScale, menu_size_h * menuScale), ImGuiCond_Always);
				g_Config::bMenuNeedReset = false;
			}
			else {
				// 正常状态：限制最大尺寸不超过屏幕
				ImGui::SetNextWindowSizeConstraints(ImVec2(menu_size_w * menuScale, menu_size_h * menuScale), screenSize);

				// 如果你希望第一次打开是 menu_size_w x menu_size_h，可以用 FirstUseEver
				// 但不要放在 if 块外面，否则会干扰上面的 Always 逻辑
				ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.382, io.DisplaySize.y * 0.414), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
				ImGui::SetNextWindowSize(ImVec2(menu_size_w * menuScale, menu_size_h * menuScale), ImGuiCond_FirstUseEver);
			}

			if (g_Config::MenuScaleIdx != lastScaleIdx) {
				ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.382, io.DisplaySize.y * 0.414), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
				ImGui::SetNextWindowSize(ImVec2(menu_size_w * menuScale, menu_size_h * menuScale), ImGuiCond_Always);
				lastScaleIdx = g_Config::MenuScaleIdx;
			}

			// ImGui::SetNextWindowSize(ImVec2(menu_size_w, menu_size_h), ImGuiCond_FirstUseEver);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_MenuAlpha);

			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 18.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 4.0f);

			ImVec4 accentColor = ThemeColors::GetAccent();

			ImGui::PushStyleColor(ImGuiCol_WindowBg, ThemeColors::BG);
			ImGui::PushStyleColor(ImGuiCol_Border, ThemeColors::TEXT);

			ImGuiWindowFlags wFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
			io.ConfigWindowsResizeFromEdges = false;
			if (g_Config::bMenuLockResize) {
				wFlags |= ImGuiWindowFlags_NoResize; // 如果勾选，禁止缩放
			}

			if (ImGui::Begin(U8("38s\u81EA\u7528###38sPrivate"), nullptr, wFlags)) {

				ImDrawList* draw_list = ImGui::GetWindowDrawList();
				ImVec2 title_pos = ImGui::GetWindowPos();
				float title_width = ImGui::GetWindowWidth();
				ImVec2 title_size = ImGui::GetWindowSize();
				ImGui::GetBackgroundDrawList()->AddRectFilled(
					ImVec2(title_pos.x + 10.0f, title_pos.y + 10.0f),
					ImVec2(title_pos.x + title_size.x + 10.0f,
						title_pos.y + title_size.y + 10.0f), IM_COL32_BLACK);
				draw_list->AddRectFilled(ImVec2(title_pos.x, title_pos.y),
					ImVec2(title_pos.x + title_width, title_pos.y + 9.0f),
					ImGui::GetColorU32(ThemeColors::ACCENT2));
				draw_list->AddLine(ImVec2(title_pos.x, title_pos.y + 9.0f),
					ImVec2(title_pos.x + title_width, title_pos.y + 9.0f), IM_COL32_BLACK, 3.0f);

				const float SidebarWidth = 190.0f * menuScale;
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 3.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 14.0f));
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeColors::BG);
				ImGui::PushStyleColor(ImGuiCol_Border, ThemeColors::TEXT);
				ImGui::BeginChild("MainSidebar", ImVec2(SidebarWidth, 0.0f), true,
					ImGuiWindowFlags_NoScrollbar);
				ImGui::TextColored(accentColor, U8("38s\u81EA\u7528"));
				ImGui::Dummy(ImVec2(0.0f, 8.0f));
				DrawAnimatedSeparator();

				const MenuSection ActiveSection = GetActiveMenuSection();
				if (DrawSidebarEntry(U8("战斗与瞄准"), ActiveSection == MenuSection::Combat, 42.0f))
					g_ActiveMenuPage = MenuPage::Combat;
				if (DrawSidebarEntry(U8("视觉与资源"), ActiveSection == MenuSection::Visuals, 42.0f))
					g_ActiveMenuPage = MenuPage::Visuals;
				if (GetActiveMenuSection() == MenuSection::Visuals) {
					if (DrawSidebarEntry(U8("视觉设置"), g_ActiveMenuPage == MenuPage::Visuals, 32.0f, true))
						g_ActiveMenuPage = MenuPage::Visuals;
					if (DrawSidebarEntry(U8("队友显示"), g_ActiveMenuPage == MenuPage::TeamVisuals, 32.0f, true))
						g_ActiveMenuPage = MenuPage::TeamVisuals;
				}
				if (DrawSidebarEntry(U8("自动采集资源"), GetActiveMenuSection() == MenuSection::ResourceHarvest, 42.0f))
					g_ActiveMenuPage = MenuPage::ResourceHarvest;
				if (DrawSidebarEntry(U8("自动化与实用"), GetActiveMenuSection() == MenuSection::Automation, 42.0f))
					g_ActiveMenuPage = MenuPage::Automation;
				if (DrawSidebarEntry(U8("工具"), GetActiveMenuSection() == MenuSection::Tools, 42.0f))
					g_ActiveMenuPage = MenuPage::Inventory;
				if (GetActiveMenuSection() == MenuSection::Tools) {
					if (DrawSidebarEntry(U8("背包整理"), g_ActiveMenuPage == MenuPage::Inventory, 32.0f, true))
						g_ActiveMenuPage = MenuPage::Inventory;
					if (DrawSidebarEntry(U8("生物列表"), g_ActiveMenuPage == MenuPage::CreatureList, 32.0f, true))
						g_ActiveMenuPage = MenuPage::CreatureList;
					if (DrawSidebarEntry(U8("建筑列表"), g_ActiveMenuPage == MenuPage::StructureList, 32.0f, true))
						g_ActiveMenuPage = MenuPage::StructureList;
					if (DrawSidebarEntry(U8("脚本扩展"), g_ActiveMenuPage == MenuPage::Lua, 32.0f, true))
						g_ActiveMenuPage = MenuPage::Lua;
				}
				if (DrawSidebarEntry(U8("设置"), GetActiveMenuSection() == MenuSection::Settings, 42.0f))
					g_ActiveMenuPage = MenuPage::InterfaceSettings;
				if (GetActiveMenuSection() == MenuSection::Settings) {
					if (DrawSidebarEntry(U8("界面与系统"), g_ActiveMenuPage == MenuPage::InterfaceSettings, 32.0f, true))
						g_ActiveMenuPage = MenuPage::InterfaceSettings;
					if (DrawSidebarEntry(U8("配置管理"), g_ActiveMenuPage == MenuPage::Configs, 32.0f, true))
						g_ActiveMenuPage = MenuPage::Configs;
					if (DrawSidebarEntry(U8("语言设置"), g_ActiveMenuPage == MenuPage::Language, 32.0f, true))
						g_ActiveMenuPage = MenuPage::Language;
				}
				ImGui::EndChild();
				ImGui::SameLine(0.0f, 12.0f);

				ImGui::BeginChild("MainContent", ImVec2(0.0f, 0.0f), true,
					ImGuiWindowFlags_NoScrollbar);
				ImGui::TextColored(accentColor, "%s", GetPageTitle());
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("%s", GetPageDescription());
				ImGui::PopStyleColor();
				ImGui::Dummy(ImVec2(0.0f, 5.0f));
				DrawAnimatedSeparator();
				ImGui::Dummy(ImVec2(0.0f, 7.0f));

				Aimbot_Menu();
				Visuals_Menu();
				ResourceHarvest_Menu();
				EntityList_Menu();
				StructureList_Menu();
				ItemList_Menu();
				Misc_Menu();
				Language_Menu();
				Configs_Menu();
				Lua_Menu();

				ImGui::EndChild();
				ImGui::PopStyleColor(2);
				ImGui::PopStyleVar(3);
			}

			ImGui::End();
			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar(4);
		}

		// g_Aimbot::Tick();
		// g_DrawESP::DrawESP();
		g_LogManager::Run();
		LuaManager::Get().Lua_OnPaint();
	}
}
