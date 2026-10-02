#pragma once
#include "InterfaceMotion.h"
// Included inside g_DrawImGui, after ThemeColors and Tr.
namespace HoverHelp {
    inline const char* Guide="";
    inline ImGuiID Anchor=0, Candidate=0, Previous=0;
    inline std::string Text, Visible;
    inline ImVec2 Position{};
    inline float Alpha=0, Dwell=0;
    inline bool Specific=false;
    inline ImRect AnchorBounds{};
    inline void BeginFrame() { Anchor=Candidate=0; Text.clear(); Guide=""; }
    inline void Remember(const char* CustomGuide=nullptr) {
        Anchor=ImGui::GetItemID();
        AnchorBounds=GImGui->LastItemData.Rect;
        if (!Anchor) Anchor=ImGui::GetID("##text_help");
        if (!g_Config::bShowFeatureHelp || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
        Candidate=Anchor; Text=CustomGuide?Tr(CustomGuide):""; Specific=false;
    }
    inline void Control(const char* Label,const char* Usage) {
        Remember();
        if(Candidate!=Anchor || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
        Text=Label; Text+="\n\n"; Text+=Tr(Usage);
        Specific=true;
    }
    template<typename... T> void Add(const char* Format,T... Args) {
        if (!g_Config::bShowFeatureHelp || !Candidate || Candidate!=Anchor) return;
        // Plain status text and headings must not attach their help to a previous control.
        const auto& Bounds=GImGui->LastItemData.Rect;
        if(Bounds.Min.x!=AnchorBounds.Min.x || Bounds.Min.y!=AnchorBounds.Min.y ||
           Bounds.Max.x!=AnchorBounds.Max.x || Bounds.Max.y!=AnchorBounds.Max.y) return;
        const int N=std::snprintf(nullptr,0,Format,Args...);
        if (N<=0 || N>16384) return;
        std::string S(N+1,'\0'); std::snprintf(S.data(),S.size(),Format,Args...); S.resize(N);
        if (!Specific) { Text.clear(); Specific=true; }
        if (Text.find(S)==std::string::npos) { if (!Text.empty()) Text+="\n\n"; Text+=S; }
    }
    inline void Draw() {
        const float Dt=ImGui::GetIO().DeltaTime;
        if (Candidate!=Previous) Dwell=0; else Dwell+=Dt;
        Previous=Candidate;
        const bool Show=g_Config::bShowFeatureHelp && Candidate && Dwell>=.25f && !Text.empty();
        if (Show) {
            Visible=Text;
            const auto P=ImGui::GetMousePos(); Position={P.x+16,P.y+22};
        }
        Alpha=PhoenixMotion::Approach(Alpha,Show?1.f:0.f,Dt,.12f);
        if (Alpha<=0 || Visible.empty()) return;
        const auto Display=ImGui::GetIO().DisplaySize;
        float Width=std::min(480.f,std::max(180.f,Display.x-48));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*Alpha);
        ImGui::SetNextWindowPos({std::clamp(Position.x,8.f,std::max(8.f,Display.x-Width-36)),std::clamp(Position.y,8.f,std::max(8.f,Display.y-100))});
        ImGui::SetNextWindowSizeConstraints({0,0},{Width+32,std::max(100.f,Display.y-24)});
        ImGui::Begin("##PhoenixHoverHelp",nullptr,ImGuiWindowFlags_Tooltip|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+Width);
        ImGui::TextUnformatted(Visible.c_str()); ImGui::PopTextWrapPos();
        auto Size=ImGui::GetWindowSize(); auto Pos=ImGui::GetWindowPos();
        if (Pos.y+Size.y>Display.y-8) ImGui::SetWindowPos({Pos.x,std::max(8.f,Display.y-Size.y-8)});
        ImGui::End(); ImGui::PopStyleVar();
    }
}
