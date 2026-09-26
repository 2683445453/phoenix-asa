#pragma once
// A persistent flyout lets outside-click, Escape and Done fade out together.
namespace ColorPalette {
    inline float* Target=nullptr;
    inline float Original[4]{};
    inline float Alpha=0;
    inline bool Open=false;
    inline int OpenedFrame=-1;
    inline ImVec2 Position{};
    inline void Show(float* Color,ImVec2 At) {
        Target=Color; std::copy(Color,Color+4,Original); Position=At;
        Open=true; OpenedFrame=ImGui::GetFrameCount();
    }
    inline void Draw() {
        if (!g_MDX12::g_MenuState::g_isOpen) Open=false;
        Alpha=PhoenixMotion::Approach(Alpha,Open?1.f:0.f,ImGui::GetIO().DeltaTime);
        if (!Target || Alpha<=0) { if(!Open)Target=nullptr; return; }
        const auto Display=ImGui::GetIO().DisplaySize;
        const float Width=std::min(328.f,Display.x-24);
        ImGui::SetNextWindowPos({std::clamp(Position.x,12.f,std::max(12.f,Display.x-Width-12)),std::clamp(Position.y,12.f,std::max(12.f,Display.y-500))});
        ImGui::SetNextWindowSize({Width,0});
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*Alpha);
        ImGuiWindowFlags Flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoFocusOnAppearing;
        if(!Open) Flags|=ImGuiWindowFlags_NoInputs;
        ImGui::Begin("##PhoenixPalette",nullptr,Flags);
        if (Open && ImGui::GetFrameCount()>OpenedFrame &&
            ((ImGui::IsMouseClicked(0)&&!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) || ImGui::IsKeyPressed(ImGuiKey_Escape))) Open=false;
        ImGui::BeginDisabled(!Open);
        Title(Tr(U8("调色板")));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::ColorPicker4("##spectrum",Target,ImGuiColorEditFlags_PickerHueBar|ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoSidePreview|ImGuiColorEditFlags_NoOptions);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::ColorEdit4("##hex",Target,ImGuiColorEditFlags_DisplayHex|ImGuiColorEditFlags_Uint8|ImGuiColorEditFlags_NoPicker|ImGuiColorEditFlags_NoSmallPreview|ImGuiColorEditFlags_NoOptions);
        const ImVec4 Presets[]={{.04f,.52f,1,1},{1,.45f,.38f,1},{.4f,.84f,.75f,1},{1,.78f,.42f,1},{.7f,.63f,.95f,1},{.95f,.95f,.95f,1}};
        for(int I=0;I<6;++I) {
            if(I) ImGui::SameLine(); ImGui::PushID(I);
            if(ImGui::ColorButton("##swatch",Presets[I],ImGuiColorEditFlags_NoTooltip,{32,26})) {
                Target[0]=Presets[I].x;Target[1]=Presets[I].y;Target[2]=Presets[I].z;
            } ImGui::PopID();
        }
        if(ImGui::Button(Tr(U8("复制")))){std::copy(Target,Target+4,g_saved_color);g_has_saved_color=true;}
        ImGui::SameLine();ImGui::BeginDisabled(!g_has_saved_color);
        if(ImGui::Button(Tr(U8("粘贴"))))std::copy(g_saved_color,g_saved_color+4,Target);
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button(Tr(U8("还原"))))std::copy(Original,Original+4,Target);
        if(ImGui::Button(Tr(U8("完成")),{-FLT_MIN,0}))Open=false;
        ImGui::EndDisabled();ImGui::End();ImGui::PopStyleVar();
    }
}
