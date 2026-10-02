#include "../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "VibrancyBackdrop.h"
#include "ConfigImGui.h"
#include "FeatureCatalog.h"
#include "StartupPresentation.h"
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
#include "../Config/ConfigManager.h"
#ifndef PHOENIX_UI_TEST
#include "../Lua/LuaManager.h"
#endif
#include "../Hack/Hack.h"
#include "../Hack/DeinoFlightUI.h"
#include "../Hack/TekSpear.h"
#include "../Hack/GeneScannerMarkers.h"

namespace g_DrawImGui {
#ifndef PHOENIX_UI_TEST
#include "DeinoFlightOverlay.inl"
#endif
    float g_MenuAlpha=1;
    bool g_UseSidebarNavigation=true;
    MenuPage g_ActiveMenuPage=MenuPage::Home;
    namespace {
        void PhoenixMark(float Size, float Reveal=1.f) {
            const auto P=ImGui::GetCursorScreenPos(); auto* D=ImGui::GetWindowDrawList();
            const ImU32 C=ImGui::GetColorU32(ThemeColors::GetAccent());
            const float Growth=std::clamp(Reveal,0.f,1.f);
            auto Line=[&](float X,float Y,float A,float B){
                const float Lift=(1.f-Growth)*.10f;
                D->AddLine({P.x+X*Size,P.y+(Y+Lift)*Size},
                    {P.x+(X+(A-X)*Growth)*Size,P.y+(Y+(B-Y)*Growth+Lift)*Size},C,1.6f);
            };
            Line(.5f,.15f,.5f,.72f); Line(.5f,.15f,.62f,.22f); Line(.5f,.42f,.12f,.16f); Line(.12f,.16f,.25f,.59f);
            Line(.25f,.59f,.5f,.72f); Line(.5f,.42f,.88f,.16f); Line(.88f,.16f,.75f,.59f); Line(.75f,.59f,.5f,.72f);
            Line(.5f,.58f,.21f,.39f); Line(.5f,.58f,.79f,.39f); Line(.5f,.72f,.36f,.91f); Line(.5f,.72f,.64f,.91f);
            ImGui::Dummy({Size,Size});
        }
        void StopAutomation() {
            PhoenixShortcuts::StopAll(); PhoenixShortcuts::RequestStop();
        }
        void Welcome() {
            auto& IO=ImGui::GetIO();
            static ImGuiContext* Context=nullptr;
            static double LastTime=-1;
            if (Context!=ImGui::GetCurrentContext() || ImGui::GetTime()<LastTime) {
                Context=ImGui::GetCurrentContext(); PhoenixStartup::Current={};
            }
            LastTime=ImGui::GetTime();
            bool Visible=true;
#ifndef PHOENIX_UI_TEST
            DWORD ForegroundPID=0;
            const HWND Foreground=GetForegroundWindow();
            GetWindowThreadProcessId(Foreground,&ForegroundPID);
            Visible=ForegroundPID==GetCurrentProcessId() && !IsIconic(Foreground);
#endif
            auto& Intro=PhoenixStartup::Current;
            Intro.Tick(IO.DisplaySize.x>1 && IO.DisplaySize.y>1,g_Config::bShowWelcome,Visible,IO.DeltaTime);
            if(Intro.Alpha<=0) return;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,Intro.Alpha);
            ImGui::SetNextWindowPos({IO.DisplaySize.x*.5f,22.f+(1.f-Intro.Alpha)*10.f},ImGuiCond_Always,{.5f,0});
            ImGui::SetNextWindowSize({std::min(420.f,IO.DisplaySize.x-24),0});
            if(ImGui::Begin("phoenix##welcome",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
                ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav)) {
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                PhoenixMark(64,std::min(1.f,Intro.Elapsed/.8f)); ImGui::SameLine(); ImGui::BeginGroup();
                Title("phoenix");ImGui::TextDisabled(Tr(U8("界面已就绪")));
                ImGui::Text(Tr(U8("按 %s 打开控制中心")),KeyName(PhoenixShortcuts::MenuKey).c_str());
                ImGui::EndGroup();
                if(DrawCustomButton(Tr(U8("关闭启动展示")))) Intro.Dismiss();
            } ImGui::End(); ImGui::PopStyleVar();
        }
        void ShortcutSettings() {
            BeginTabRegion("Shortcuts");
            DrawKeyBindButton(Tr(U8("菜单开关")),PhoenixShortcuts::MenuKey);
            DrawKeyBindButton(Tr(U8("停止自动化")),PhoenixShortcuts::StopKey);
            ImGui::Separator();
            Title(Tr(U8("视角与移动快捷键")));
            for(auto& A:PhoenixShortcuts::Actions) if(A.Enabled==&g_Config::bFreeCameraEnabled) {
                DrawKeyBindButton(Tr(A.Label),A.Key,U8("设置灵魂出窍的进入／退出快捷键，默认 Z。点击绑定后按新的按键或组合键，Esc 取消；清除后仅可使用功能页开关。WASD、空格、Ctrl、Shift 与 Esc 为固定移动操作。"));
            }
            ImGui::Separator();
            Title(Tr(U8("功能快捷键")));
            for(auto& A:PhoenixShortcuts::Actions) if(A.Enabled!=&g_Config::bFreeCameraEnabled) DrawKeyBindButton(Tr(A.Label),A.Key);
            if(!PhoenixShortcuts::BindingError.empty()) {
                const std::string Prefix=U8("该快捷键已用于："); const auto& Error=PhoenixShortcuts::BindingError;
                std::string Message=Error.starts_with(Prefix)?std::string(Tr(Prefix.c_str()))+Tr(Error.substr(Prefix.size()).c_str()):Tr(Error.c_str());
                ImGui::TextWrapped("%s",Message.c_str());
            }
            EndTabRegion();
        }
        void Home() {
            BeginTabRegion("Home"); Title(Tr(U8("常用功能")));
            struct Quick { const char* Name; const char* Detail; Topic Target; bool* Enabled; bool Ready; };
            const Quick Links[]={
                {Tr(U8("自动锁定")),Tr(U8("目标与弹道")),Topic::Aim,&g_Config::bAimbotEnabled,g_Hook::UpdateCameraOK||g_Hook::PostRenderOK},
                {Tr(U8("泰克长矛")),Tr(U8("瞬间招架与偏转")),Topic::Spear,&g_Config::bTekSpearAutoBlock,g_Hook::UpdateCameraOK||g_Hook::PostRenderOK},
                {Tr(U8("骑乘采集")),Tr(U8("资源与自动靠近")),Topic::Harvest,&g_Config::bAutoHarvestResources,g_Hook::PostRenderOK&&g_Hook::ActorTickOK},
                {Tr(U8("自动吸包")),Tr(U8("空投与掉落包")),Topic::Loot,&g_Config::bAutoLootContainers,g_Hook::ActorTickOK},
                {Tr(U8("自动制作")),Tr(U8("工作台与配方")),Topic::Craft,&g_Config::bAutoCraft,g_Hook::ActorTickOK},
                {Tr(U8("灵魂出窍")),Tr(U8("本地自由飞行")),Topic::Travel,&g_Config::bFreeCameraEnabled,g_Hook::UpdateCameraOK}
            };
            for(const auto& Q:Links) {
                ImGui::PushID(Q.Name);
                if(DrawCustomButton(Q.Name,{160,0})) SelectTopic(Q.Target);
                ImGui::SameLine(); ImGui::BeginDisabled(!Q.Ready); ImGui::Checkbox(Tr(U8("启用")),Q.Enabled); ImGui::EndDisabled();
                ImGui::TextDisabled("%s%s",Q.Detail,Q.Ready?"":Tr(U8(" · 当前接口不可用"))); ImGui::Separator();ImGui::PopID();
            }
            Title(Tr(U8("已开启的快捷功能")));
            bool Any=false;
            for(auto& A:PhoenixShortcuts::Actions) if(*A.Enabled) {
                Any=true; ImGui::PushID(A.Id); ImGui::Checkbox(Tr(A.Label),A.Enabled);
                if(A.Key) { ImGui::SameLine(); ImGui::TextDisabled("%s",KeyName(A.Key).c_str()); } ImGui::PopID();
            }
            if(!Any) ImGui::TextDisabled(Tr(U8("快捷功能均已关闭")));
            HelpText(Tr(U8("这里列出支持快捷键的功能；其余自动化请在各分类中查看。")));
            EndTabRegion();
        }
    }
    void MyImGuiDraw(IDXGISwapChain3*,UINT,UINT) {
        if(!ImGui::GetCurrentContext()) return;
        if(!style_initialized){SetupCustomImGuiStyle();style_initialized=true;}
        const bool Open=g_MDX12::g_MenuState::g_isOpen;
        HoverHelp::BeginFrame();
        AdvanceTopic(ImGui::GetIO().DeltaTime);
        static float MenuFade=0;
        MenuFade=PhoenixMotion::Approach(MenuFade,Open?1.f:0.f,ImGui::GetIO().DeltaTime,.14f);
        g_MenuAlpha=MenuFade;
        static bool WasOpen=false;
#ifndef PHOENIX_UI_TEST
        if(Open&&!WasOpen){LanguageManager::Get().RefreshFileList();LuaManager::Get().RefreshFileList();ConfigManager::Get().RefreshFileList();LuaManager::Get().Lua_OnMenuOpen();}
        if(!Open&&WasOpen){g_MDX12::g_MenuState::g_pCurrentBindingKey=nullptr;ConfigManager::Get().SaveConfig("phoenix_autosave.ini");}
        WasOpen=Open;
        static double NextSave=0;
        if(!ConfigManager::Get().GetConfigDir().empty()&&ImGui::GetTime()>=NextSave){ConfigManager::Get().SaveConfig("phoenix_autosave.ini");NextSave=ImGui::GetTime()+10;}
#endif
        if(MenuFade>0) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,MenuFade);
#ifndef PHOENIX_UI_TEST
            if(Open) LuaManager::Get().Lua_OnPaintMenu(MenuFade);
#endif
            auto& IO=ImGui::GetIO();
            g_Config::MenuScaleIdx=std::clamp(g_Config::MenuScaleIdx,0,4);
            if(g_Config::bMenuNeedReset) g_Config::MenuScaleIdx=1;
            const float Scales[]={.78f,1,1.22f,1.44f,1.89f};const float Scale=Scales[g_Config::MenuScaleIdx];
            ImVec2 Maximum{std::max(240.f,IO.DisplaySize.x-16),std::max(240.f,IO.DisplaySize.y-16)};
            ImVec2 Desired{std::min(1200.f*Scale,Maximum.x),std::min(790.f*Scale,Maximum.y)};
            ImGui::GetStyle().WindowMinSize={std::min(440.f,Maximum.x),std::min(360.f,Maximum.y)};
            ImGui::SetNextWindowSizeConstraints(ImGui::GetStyle().WindowMinSize,Maximum);
            static int LastScale=-1;
            static ImVec2 LastDisplay{};
            const bool Reset=g_Config::bMenuNeedReset||LastScale!=g_Config::MenuScaleIdx||LastDisplay.x!=IO.DisplaySize.x||LastDisplay.y!=IO.DisplaySize.y;
            LastDisplay=IO.DisplaySize;
            if(g_Config::bMenuNeedReset){g_Config::MenuScaleIdx=1;g_Config::bMenuNeedReset=false;}
            if(Reset) {ImGui::SetNextWindowPos({IO.DisplaySize.x*.5f,IO.DisplaySize.y*.5f},ImGuiCond_Always,{.5f,.5f});ImGui::SetNextWindowSize(Desired,ImGuiCond_Always);LastScale=g_Config::MenuScaleIdx;}
            ImGuiWindowFlags Flags=ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar;
            if(g_Config::bMenuLockResize)Flags|=ImGuiWindowFlags_NoResize;
            if(!Open) Flags|=ImGuiWindowFlags_NoInputs;
            if(ImGui::Begin("phoenix###phoenixMain",nullptr,Flags)) {
                // Keep the entire window reachable after display resolution changes.
                ImVec2 Pos=ImGui::GetWindowPos(),Size=ImGui::GetWindowSize();
                ImGui::SetWindowPos({std::clamp(Pos.x,0.f,std::max(0.f,IO.DisplaySize.x-Size.x)),std::clamp(Pos.y,0.f,std::max(0.f,IO.DisplaySize.y-Size.y))});
                const float Width=ImGui::GetContentRegionAvail().x;
                const bool Wide=Width>=1000*Scale, Medium=Width>=740*Scale;
                int Category=CurrentFeature().Category;
                static char Search[128]{};
                if(!Wide) {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if(ImGui::BeginCombo("##category",Tr(Categories[Category]))) {
                        for(int I=0;I<IM_ARRAYSIZE(Categories);++I) if(ImGui::Selectable(Tr(Categories[I]),I==Category)) {
                            for(const auto& F:Features) if(F.Category==I){SelectTopic(F.TopicId);break;}
                        } ImGui::EndCombo();
                    }
                }
                if(Wide) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg,{28/255.f,28/255.f,30/255.f,.8f});
                    ImGui::BeginChild("Categories",{156*Scale,0},false);
                    PhoenixVibrancy::DrawSidebar();
                    PhoenixMark(42*Scale);Title("phoenix");ImGui::Spacing();
                    for(int I=0;I<IM_ARRAYSIZE(Categories);++I) if(ImGui::Selectable(Tr(Categories[I]),I==Category,0,{0,38*Scale})) {
                        Search[0]=0;for(const auto& F:Features)if(F.Category==I){SelectTopic(F.TopicId);break;}
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("%s",KeyName(PhoenixShortcuts::MenuKey).c_str());
                    if(DrawCustomButton(Tr(U8("停止自动化")),{-FLT_MIN,0}))StopAutomation();
                    ImGui::TextWrapped("%s",KeyName(PhoenixShortcuts::StopKey).c_str());
                    ImGui::EndChild();ImGui::PopStyleColor();ImGui::SameLine();
                }
                if(Medium) {
                    ImGui::BeginChild("FeatureList",{215*Scale,0},true);
                    ImGui::SetNextItemWidth(-FLT_MIN);ImGui::InputTextWithHint("##search",Tr(U8("搜索全部功能…")),Search,sizeof(Search));
                    int Matches=0;
                    for(const auto& F:Features) {
                        if(Search[0] ? (std::string(Tr(F.Name)).find(Search)==std::string::npos&&std::string(Tr(F.Summary)).find(Search)==std::string::npos) : F.Category!=CurrentFeature().Category)continue;
                        ++Matches;ImGui::PushID(static_cast<int>(F.TopicId));
                        if(ImGui::Selectable(Tr(F.Name),IsTopic(F.TopicId),0,{0,36*Scale}))SelectTopic(F.TopicId);
                        HoverHelp::Remember(F.Guide);
                        ImGui::PopID();
                    }
                    if(!Matches)ImGui::TextWrapped(Tr(U8("没有匹配功能，请换个关键词。")));
                    ImGui::EndChild();ImGui::SameLine();
                } else {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if(ImGui::BeginCombo("##feature",Tr(CurrentFeature().Name))) {
                        for(const auto& F:Features)if(F.Category==CurrentFeature().Category&&ImGui::Selectable(Tr(F.Name),IsTopic(F.TopicId)))SelectTopic(F.TopicId);
                        ImGui::EndCombo();
                    }
                }
                ImGui::BeginChild("Detail",{0,0},ImGuiChildFlags_AlwaysUseWindowPadding);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha,MenuFade*TopicFade.Alpha);
                ImGui::BeginDisabled(!Open || TopicFade.Current!=TopicFade.Pending);
                const auto& F=CurrentFeature(); HoverHelp::Guide=F.Guide;
                Title(Tr(F.Name)); ImGui::TextWrapped("%s",Tr(F.Summary)); HoverHelp::Remember();
                if(!Wide&&DrawCustomButton(Tr(U8("停止自动化"))))StopAutomation();
                if(IsTopic(Topic::Home))Home();
                else if(IsTopic(Topic::Shortcuts))ShortcutSettings();
                else {Aimbot_Menu();Visuals_Menu();ResourceHarvest_Menu();EntityList_Menu();StructureList_Menu();ItemList_Menu();Misc_Menu();Language_Menu();Configs_Menu();Lua_Menu();}
                ImGui::EndDisabled();ImGui::PopStyleVar();
                ImGui::EndChild();
            } ImGui::End();
            ImGui::PopStyleVar();
        }
        if(Open) DrawNoteExperimentWindow();
        Welcome(); ColorPalette::Draw(); HoverHelp::Draw();
#ifndef PHOENIX_UI_TEST
        DrawDeinoFlightOverlay();
#endif
#ifndef PHOENIX_UI_TEST
        g_LogManager::Run();LuaManager::Get().Lua_OnPaint();
#endif
    }
}
