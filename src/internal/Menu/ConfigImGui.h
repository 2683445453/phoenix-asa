#pragma once
#if defined(__cpp_char8_t)
#define U8(str) reinterpret_cast<const char*>(u8##str)
#else
#define U8(str) u8##str
#endif
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "../Config/Configs.h"
#include "../Config/InterfaceRules.h"
#include "DrawImGui.h"
#include "../Util/Util.h"
#include "../Language/LanguageManager.h"
#include "FeatureShortcuts.h"
#include "InterfaceMotion.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace g_DrawImGui {
    inline const char* Tr(const char* Source) { return LanguageManager::Get().Translate(Source); }
    enum class MenuPage { Home, Combat, Visuals, ResourceHarvest, Automation, Inventory, CreatureList,
        StructureList, Lua, InterfaceSettings, Configs, Language, Shortcuts };
    enum class Topic { Home, Aim, Weapons, Indicators, Spear, Objects, Typography, Structures, World,
        Harvest, Resources, Diagnostics, Loot, Survival, Fishing, Gacha, Craft, Repair, Taming,
        Travel, Utilities, Inventory, Creatures, StructureList, Scripts, Interface, Shortcuts, System, Configs, Language, Care, Scanner, Overlay };
    extern bool g_UseSidebarNavigation;
    extern MenuPage g_ActiveMenuPage;
    inline Topic g_ActiveTopic = Topic::Home;
    inline ImFont* g_TitleFont = nullptr;
    extern float g_MenuAlpha;
    inline bool style_initialized = false;
    inline bool IsTopic(Topic T) { return g_ActiveTopic == T; }
    inline bool BeginMenuPage(MenuPage P, const char* L) { return g_UseSidebarNavigation ? g_ActiveMenuPage == P : ImGui::BeginTabItem(L); }
    inline void EndMenuPage() { if (!g_UseSidebarNavigation) ImGui::EndTabItem(); }
    namespace ThemeColors {
        inline const ImVec4 BG{28/255.f,28/255.f,30/255.f,1}, PANEL{44/255.f,44/255.f,46/255.f,1}, SURFACE{58/255.f,58/255.f,60/255.f,1};
        inline const ImVec4 TEXT{1,1,1,.95f}, MUTED{1,1,1,.70f}, GLASS_BORDER{1,1,1,.10f};
        inline const ImVec4 MENUBAR_BG = BG, ACCENT2 = SURFACE, YELLOW = SURFACE, CYAN = SURFACE;
        inline ImVec4 GetAccent() { return {10/255.f,132/255.f,1,1}; }
    }
    inline void SetupCustomImGuiStyle() {
        ImGuiStyle& S = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        S.WindowRounding=12; S.ChildRounding=8; S.FrameRounding=8; S.PopupRounding=10;
        S.ScrollbarRounding=6; S.GrabRounding=4; S.TabRounding=8;
        S.WindowBorderSize=S.ChildBorderSize=S.PopupBorderSize=S.FrameBorderSize=S.TabBorderSize=1;
        S.WindowPadding={16,16}; S.FramePadding={10,7}; S.ItemSpacing={12,10}; S.ItemInnerSpacing={8,6}; S.CellPadding={10,8};
        S.TabBarBorderSize=1; S.TabBarOverlineSize=1; S.SeparatorTextBorderSize=1;
        S.ScrollbarSize=10; S.GrabMinSize=12; S.WindowTitleAlign={0, .5f}; S.WindowMenuButtonPosition=ImGuiDir_None;
        S.Colors[ImGuiCol_Text]=ThemeColors::TEXT; S.Colors[ImGuiCol_TextDisabled]=ThemeColors::MUTED;
        for (auto C : {ImGuiCol_WindowBg,ImGuiCol_PopupBg,ImGuiCol_FrameBg,ImGuiCol_TitleBg,ImGuiCol_TitleBgActive,ImGuiCol_TitleBgCollapsed,ImGuiCol_MenuBarBg,ImGuiCol_ScrollbarBg}) S.Colors[C]=ThemeColors::BG;
        S.Colors[ImGuiCol_ChildBg]=ThemeColors::PANEL;
        for (auto C : {ImGuiCol_Button,ImGuiCol_ButtonHovered,ImGuiCol_ButtonActive,ImGuiCol_Header,ImGuiCol_HeaderHovered,ImGuiCol_HeaderActive,ImGuiCol_FrameBgHovered,ImGuiCol_FrameBgActive,ImGuiCol_Tab,ImGuiCol_TabHovered,ImGuiCol_TabActive,ImGuiCol_TabUnfocused,ImGuiCol_TabUnfocusedActive,ImGuiCol_TableHeaderBg,ImGuiCol_ScrollbarGrab,ImGuiCol_ScrollbarGrabHovered,ImGuiCol_ScrollbarGrabActive,ImGuiCol_SliderGrab,ImGuiCol_SliderGrabActive}) S.Colors[C]=ThemeColors::SURFACE;
        for (auto C : {ImGuiCol_Border,ImGuiCol_Separator,ImGuiCol_TableBorderStrong,ImGuiCol_TableBorderLight}) S.Colors[C]=ThemeColors::GLASS_BORDER;
        S.Colors[ImGuiCol_BorderShadow]={0,0,0,0};
        S.Colors[ImGuiCol_CheckboxSelectedBg]=ThemeColors::SURFACE;
        S.Colors[ImGuiCol_ButtonHovered]={.29f,.29f,.30f,1}; // white / 8 hover over the surface
        S.Colors[ImGuiCol_ButtonActive]=ThemeColors::PANEL;
        S.Colors[ImGuiCol_TabSelectedOverline]=S.Colors[ImGuiCol_TabDimmedSelectedOverline]=ThemeColors::GLASS_BORDER;
        S.Colors[ImGuiCol_DragDropTarget]=ThemeColors::GetAccent();
        S.Colors[ImGuiCol_DragDropTargetBg]={1,1,1,.08f};
        S.Colors[ImGuiCol_TreeLines]=ThemeColors::GLASS_BORDER;
        S.Colors[ImGuiCol_TextLink]=ThemeColors::GetAccent();
        S.Colors[ImGuiCol_CheckMark]=ThemeColors::GetAccent(); S.Colors[ImGuiCol_NavHighlight]=ThemeColors::GetAccent();
        S.Colors[ImGuiCol_TextSelectedBg]={1,1,1,.12f}; S.Colors[ImGuiCol_TableRowBg]=ThemeColors::PANEL;
        S.Colors[ImGuiCol_TableRowBgAlt]=ThemeColors::PANEL;
        S.Colors[ImGuiCol_ModalWindowDimBg]={28/255.f,28/255.f,30/255.f,.8f};
        S.Colors[ImGuiCol_ResizeGrip]=S.Colors[ImGuiCol_ResizeGripHovered]=S.Colors[ImGuiCol_ResizeGripActive]=ThemeColors::SURFACE;
        S.Colors[ImGuiCol_SeparatorHovered]=S.Colors[ImGuiCol_SeparatorActive]=ThemeColors::GLASS_BORDER;
        S.Colors[ImGuiCol_NavWindowingHighlight]=ThemeColors::GLASS_BORDER;
        S.Colors[ImGuiCol_NavWindowingDimBg]=S.Colors[ImGuiCol_ModalWindowDimBg];
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
#include "HoverHelp.h"
    inline void Title(const char* Text) { if(g_TitleFont) ImGui::PushFont(g_TitleFont); ImGui::TextUnformatted(Tr(Text)); if(g_TitleFont) ImGui::PopFont(); HoverHelp::Remember(); }
    inline int g_ParameterDepth = 0;
    struct ParameterFade {
        float Alpha=0;
        ImVec2 Start{};
        float Indent=0;
        explicit ParameterFade(const char* Id,bool Enabled) {
            auto* Store=ImGui::GetStateStorage();const auto Key=ImGui::GetID(Id);
            Alpha=PhoenixMotion::Approach(Store->GetFloat(Key,0),Enabled?1.f:0.f,ImGui::GetIO().DeltaTime,.12f);
            Store->SetFloat(Key,Alpha);
            if(Alpha>0) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*Alpha);
                ImGui::BeginDisabled(!Enabled);
                Start=ImGui::GetCursorScreenPos();
                Indent=std::min(18.f, std::max(0.f, ImGui::GetContentRegionAvail().x-100.f));
                ImGui::Indent(Indent);
                if(g_ParameterDepth++ == 0) ImGui::TextDisabled("%s",Tr(U8("参数设置")));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(8,4));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8,7));
            }
        }
        ParameterFade(const ParameterFade&)=delete;
        explicit operator bool() const {return Alpha>0;}
        ~ParameterFade(){if(Alpha>0){
            ImGui::GetWindowDrawList()->AddLine(ImVec2(Start.x+3,Start.y),
                ImVec2(Start.x+3,ImGui::GetCursorScreenPos().y-4),
                ImGui::GetColorU32(ThemeColors::GLASS_BORDER),2);
            --g_ParameterDepth; ImGui::PopStyleVar(2); ImGui::Unindent(Indent);
            ImGui::EndDisabled();ImGui::PopStyleVar();
        }}
    };
    template<typename... T> inline void HelpText(const char* Format, T... Args) { HoverHelp::Add(Format,Args...); }
    struct CustomComboItem { const char* label=nullptr; bool selected=false; float* color_ptr=nullptr; bool* checkbox_ptr=nullptr; };
    struct ColorPickerData { const char* label_id; float* col_ptr; };
    inline float g_saved_color[4]={1,1,1,1};
    inline bool g_has_saved_color=false;
#include "ColorPalette.h"
    inline bool DrawCustomButton(const char* Label, const ImVec2& Size={0,0}) {
        Label = Tr(Label);
        auto* W=ImGui::GetCurrentWindow(); if(W->SkipItems)return false;
        auto& G=*GImGui; const ImGuiID Id=W->GetID(Label);
        const ImVec2 TextSize=ImGui::CalcTextSize(Label,nullptr,true);
        const ImVec2 Actual=ImGui::CalcItemSize(Size,TextSize.x+G.Style.FramePadding.x*2,TextSize.y+G.Style.FramePadding.y*2);
        const auto P=ImGui::GetCursorScreenPos();
        const bool Hover=!(G.CurrentItemFlags&ImGuiItemFlags_Disabled)&&ImGui::IsWindowHovered()&&ImGui::IsMouseHoveringRect(P,{P.x+Actual.x,P.y+Actual.y});
        auto* Store=ImGui::GetStateStorage();
        float T=Store->GetFloat(Id,0);T=std::clamp(T+(Hover?1.f:-1.f)*G.IO.DeltaTime/.2f,0.f,1.f);Store->SetFloat(Id,T);
        float E=1-(1-T)*(1-T);const ImVec4 Base=ThemeColors::SURFACE;
        ImVec4 Face{Base.x+.06f*E,Base.y+.06f*E,Base.z+.06f*E,1};
        ImGui::PushStyleColor(ImGuiCol_Button,Face);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Face);
        const bool Pressed=ImGui::Button(Label,Size);ImGui::PopStyleColor(2);HoverHelp::Control(Label,U8("点击执行此操作。"));return Pressed;
    }
    inline bool DrawCustomCheckbox(const char* Label, bool* Value, const char* Description=nullptr) {
        // Feature switches remain prominent; subordinate choices use compact square checks.
        struct CheckboxStyle {
            CheckboxStyle() {
                const bool Parameter=g_ParameterDepth>0;
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,Parameter?ImVec2(4,2):ImVec2(10,7));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,Parameter?3.f:8.f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg,Parameter?ThemeColors::BG:ImVec4(.10f,.18f,.28f,1));
            }
            ~CheckboxStyle(){ImGui::PopStyleColor();ImGui::PopStyleVar(2);}
        } ScopedStyle;
        if (ImGui::CalcTextSize(Label,nullptr,true).x+ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x <= ImGui::GetContentRegionAvail().x)
            { const bool Changed=ImGui::Checkbox(Label,Value); HoverHelp::Control(Label,Description?Description:g_ParameterDepth?U8("勾选启用此子选项，取消勾选将停用。主功能开启时应用。"):U8("点击开启或关闭此功能；下方参数用于调整此功能，关闭后保留参数值。")); return Changed; }
        ImGui::PushID(Label);bool Changed=ImGui::Checkbox("##toggle",Value); HoverHelp::Control(Label,Description?Description:g_ParameterDepth?U8("勾选启用此子选项，取消勾选将停用。主功能开启时应用。"):U8("点击开启或关闭此功能；下方参数用于调整此功能，关闭后保留参数值。"));
        ImGui::SameLine();ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(Label,ImGui::FindRenderedTextEnd(Label));ImGui::PopTextWrapPos(); if(ImGui::IsItemHovered()) HoverHelp::Control(Label,Description?Description:g_ParameterDepth?U8("勾选启用此子选项，取消勾选将停用。主功能开启时应用。"):U8("点击开启或关闭此功能；下方参数用于调整此功能，关闭后保留参数值。"));
        if (!(GImGui->CurrentItemFlags&ImGuiItemFlags_Disabled) && ImGui::IsItemClicked()) {*Value=!*Value;Changed=true;}
        ImGui::PopID();return Changed;
    }
    inline bool DrawCustomSliderFloat(const char* Label,float* V,float Min,float Max,const char* Format="%.1f",float Step=1,const char* Unit=nullptr) {
        if(Max<=Min) return false;
        ImGui::PushID(Label); ImGui::TextUnformatted(Label,ImGui::FindRenderedTextEnd(Label));
        std::string F=Format; if(Unit && *Unit) { F+=' '; for(const char* P=Unit;*P;++P) { F+=*P; if(*P=='%') F+='%'; } }
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool Changed=ImGui::SliderFloat("##value",V,Min,Max,F.c_str(),ImGuiSliderFlags_AlwaysClamp);
        if(Changed) {
            if(Step>0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::TempInputIsActive(ImGui::GetItemID())) *V=Min+std::round((*V-Min)/Step)*Step;
            *V=PhoenixInterface::FiniteClamp(*V,Min,Max,Min);
        }
        HoverHelp::Control(Label,U8("拖动调整此参数；Ctrl + 点击输入精确数值，方向键微调。"));
        HelpText(Tr(U8("Ctrl + 点击输入精确数值；方向键微调。范围 %.2f – %.2f")),Min,Max);
        ImGui::PopID(); return Changed;
    }
    inline void DrawCustomColorPicker(const char* Id,float* Color,bool Active,const char* Text) {
        if(!Active) return;
        ImGui::PushID(Id);
        if(Text && *Text) { ImGui::TextUnformatted(Tr(Text)); ImGui::SameLine(); }
        if(ImGui::ColorButton("##color",{Color[0],Color[1],Color[2],Color[3]},ImGuiColorEditFlags_NoTooltip,{32,24}))
            ColorPalette::Show(Color,ImGui::GetItemRectMax());
        HoverHelp::Remember(U8("打开调色板：拖动选色区、色相条或透明度条；也可输入 HEX，使用预设色、复制、粘贴和还原。"));
        ImGui::PopID();
    }
    inline void DrawCustomColorPicker(const char* Id,float* Color,const char* Text) { DrawCustomColorPicker(Id,Color,true,Text); }
    inline void DrawMultiColorPicker(const char* Text,bool Active,std::initializer_list<ColorPickerData> Colors) {
        if(!Active) return;
        if(Text && *Text) ImGui::TextUnformatted(Tr(Text));
        int I=0; for(const auto& C:Colors) { if(I++) ImGui::SameLine(); DrawCustomColorPicker(C.label_id,C.col_ptr,true,nullptr); }
    }
    inline void DrawColorPickerRow(const char* Label,bool* Enabled,const char* Id,float* Color) {
        DrawCustomCheckbox(Label,Enabled); if(*Enabled) { ImGui::SameLine(); DrawCustomColorPicker(Id,Color,true,nullptr); }
    }
    inline bool CustomSelectable(const char* Label,bool Selected,float=8) { return ImGui::Selectable(Label,Selected); }
    inline bool DrawCustomCombo(const char* Label,int* Current,const std::vector<const char*>& Items,const char* Suffix=nullptr) {
        if(!Current || Items.empty()) return false;
        *Current=std::clamp(*Current,0,static_cast<int>(Items.size())-1);
        std::string Preview=Items[*Current]; if(Suffix) Preview+=Suffix;
        ImGui::PushID(Label);
        if(Label[0]!='#') ImGui::TextUnformatted(Label);
        ImGui::SetNextItemWidth(-FLT_MIN); bool Changed=false;
        if(ImGui::BeginCombo("##select",Preview.c_str())) {
            for(int I=0;I<static_cast<int>(Items.size());++I) { if(ImGui::Selectable(Items[I],I==*Current)) { *Current=I;Changed=true; } if(I==*Current) ImGui::SetItemDefaultFocus(); }
            ImGui::EndCombo();
        }
        HoverHelp::Control(Label,U8("点击展开并选择一项，选中后立即应用此参数。")); ImGui::PopID();return Changed;
    }
    inline bool DrawCustomCombo(const char* Label,int* Current,const char* Items[],int Count,const char* Suffix=nullptr) { return DrawCustomCombo(Label,Current,std::vector<const char*>(Items,Items+Count),Suffix); }
    inline bool DrawCustomMultiCombo(const char* Label,std::vector<CustomComboItem>& Items,const char* Placeholder=nullptr) {
        int Count=0; for(auto& I:Items) { if(I.checkbox_ptr) I.selected=*I.checkbox_ptr; Count+=I.selected; }
        std::string Preview;
        for(const auto& I:Items) if(I.selected && I.label) { if(!Preview.empty()) Preview+=", "; Preview+=I.label; }
        if(!Count) Preview=Placeholder?Placeholder:Tr(U8("未选择"));
        bool Changed=false; ImGui::PushID(Label);
        ImGui::TextUnformatted(Label,ImGui::FindRenderedTextEnd(Label));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if(ImGui::BeginCombo("##choices",Preview.c_str())) {
            auto SetAll=[&](bool Selected) { for(auto& I:Items) { Changed |= I.selected!=Selected; I.selected=Selected; if(I.checkbox_ptr)*I.checkbox_ptr=Selected; } };
            if(ImGui::SmallButton(Tr(U8("全选")))) SetAll(true);
            ImGui::SameLine(); if(ImGui::SmallButton(Tr(U8("全不选")))) SetAll(false);
            ImGui::Separator();
            for(size_t N=0;N<Items.size();++N) { auto& I=Items[N]; ImGui::PushID(static_cast<int>(N));
                if(ImGui::Checkbox(I.label,&I.selected)) { if(I.checkbox_ptr)*I.checkbox_ptr=I.selected; Changed=true; }
                HoverHelp::Control(I.label,U8("勾选启用此子选项，取消勾选将停用。主功能开启时应用。"));
                if(I.selected && I.color_ptr) { ImGui::SameLine();DrawCustomColorPicker("color",I.color_ptr,true,nullptr); }
                ImGui::PopID();
            } ImGui::EndCombo();
        } HoverHelp::Control(Label,U8("点击展开并勾选需要的类型，可同时选择多项。全选与全不选只调整此组筛选，不开启主功能。")); ImGui::PopID(); return Changed;
    }
    inline bool DrawChoiceGroup(const char* Label,std::initializer_list<CustomComboItem> Choices) {
        std::vector<CustomComboItem> Items(Choices);
        return DrawCustomMultiCombo(Tr(Label),Items);
    }
    inline std::string KeyName(unsigned Key) {
        if(!Key) return Tr(U8("未绑定")); std::string Name;
        if(Key&PhoenixInterface::Ctrl)Name+="Ctrl + "; if(Key&PhoenixInterface::Alt)Name+="Alt + "; if(Key&PhoenixInterface::Shift)Name+="Shift + ";
        Name+=g_Util::GetKeyName(Key&0xff);return Name;
    }
    inline void DrawKeyBindButton(const char* Label,UINT& Key,const char* Description=nullptr) {
        Label = Tr(Label);
        ImGui::PushID(Label); ImGui::TextUnformatted(Label); ImGui::SameLine();
        const bool Recording=g_MDX12::g_MenuState::g_pCurrentBindingKey==&Key;
        if(ImGui::Button(Recording?Tr(U8("按组合键… Esc 取消")):KeyName(Key).c_str())) {
            PhoenixShortcuts::BindingError.clear(); g_MDX12::g_MenuState::g_pCurrentBindingKey=&Key;
        }
        HoverHelp::Control(Label,Description?Description:U8("点击开始绑定，再按按键或组合键；Esc 取消。重复绑定会提示冲突。"));
        if(&Key!=&PhoenixShortcuts::MenuKey && &Key!=&PhoenixShortcuts::StopKey) {
            ImGui::SameLine(); if(ImGui::SmallButton(Tr(U8("清除")))) { PhoenixShortcuts::Assign(Key,0); g_MDX12::g_MenuState::g_pCurrentBindingKey=nullptr; }
            HoverHelp::Control(Label,U8("清除此功能的快捷键，功能开关与参数保持当前设置。"));
        }
        ImGui::PopID();
    }
    inline void BeginTabRegion(const char* Id) { ImGui::BeginChild(Id,{0,0},false); ImGui::PushTextWrapPos(0); }
    inline void EndTabRegion() { ImGui::PopTextWrapPos(); ImGui::EndChild(); }
    inline void DrawAnimatedSeparator() { ImGui::Separator(); }
}
