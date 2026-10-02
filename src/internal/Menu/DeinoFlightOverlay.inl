void DrawDeinoFlightOverlay() {
    const auto V=PhoenixDeinoUI::Read();
    if(!g_Config::bDeinoFlight || !PhoenixDeinoUI::Fresh(V,GetTickCount64()) || g_MDX12::g_MenuState::g_isOpen) return;
    ImGui::SetNextWindowPos({24,160},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(.82f);
    if(ImGui::Begin(Tr(U8("恐爪骑乘调参")),nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings)) {
        if(V.AimJump) {
            ImGui::Text(Tr(U8("朝视角跳跃 | 力度 %.1f m/s | 保护 %.2f s")),V.Values[0],V.Values[2]);
            ImGui::Text(Tr(U8("俯仰 %+.1f° | 水平 %.1f m/s | 垂直 %+.1f m/s")),V.Pitch,V.EffectiveHorizontal,V.EffectiveUp);
        } else ImGui::Text(Tr(U8("水平 %.1f 倍 | 垂直 %+.1f m/s | 保护 %.2f s")),V.Values[0],V.EffectiveUp,V.Values[2]);
        ImGui::Text(Tr(U8("%s 面板 | %s 切预设 | 按住 %s / %s + 滚轮调水平或力度 / 手动垂直")),
            KeyName(V.Keys[0]).c_str(),KeyName(V.Keys[1]).c_str(),KeyName(V.Keys[2]).c_str(),KeyName(V.Keys[3]).c_str());
        if(std::find(V.Keys.begin(),V.Keys.end(),0)!=V.Keys.end()) ImGui::TextWrapped("%s",Tr(U8("存在快捷键冲突，对应键已停用；请在完整菜单中更换。")));
        if(V.Panel) {
            ImGui::TextUnformatted(Tr(U8("调参中：Tab 选参数，滚轮调整；空格停用，已有悬停继续。")));
            const char* Names[]={V.AimJump?"视角跳跃力度":"空中水平倍率",V.AimJump?"保存的手动垂直速度":"手动垂直速度","空中跳跃保护时间","起跳保护时间","悬停请求间隔","地面大跳倍率"};
            const char* Units[]={V.AimJump?"m/s":"x","m/s","s","s","ms","x"};
            for(int I=0;I<6;++I) ImGui::Text("%s %s: %.2f %s",I==V.Selected?">":" ",Tr(Names[I]),V.Values[I],Units[I]);
        }
        if(V.Preview) {
            ImGui::Text(Tr(U8("预计水平 %.1f–%.1f m | 停止高度差 %+.1f m | 最高上升 %.1f m")),
                std::max(0.0,V.Distance-V.Uncertainty),V.Distance+V.Uncertainty,V.Height,V.Peak);
            if(V.ServerBaseline) {
                ImGui::TextUnformatted(Tr(U8("服务器实测基准：6次建立、3次独立验证，仅适用于当前参数组合。")));
                ImGui::Text(Tr(U8("预计停止高度差 %+.1f 至 %+.1f m；范围不是精度保证。")),V.Height-V.HeightMargin,V.Height+V.HeightMargin);
            } else ImGui::TextUnformatted(Tr(U8("模型估算，尚未校准；范围为经验余量，不是精度保证。")));
            if(V.Samples) ImGui::Text(Tr(U8("本地观测 %u 次 | 上次水平 %.1f m / 高差 %+.1f m，仅供对照")),V.Samples,V.LastDistance,V.LastHeight);
            if(V.NativeArc==1) ImGui::TextUnformatted(Tr(U8("原生光束已启用，末端标注预计停止位置。")));
            else if(V.NativeArc==2) ImGui::TextUnformatted(Tr(U8("光束资源加载失败，稍后自动重试；当前使用黄色预览。")));
            else if(V.NativeArc==3) ImGui::TextUnformatted(Tr(U8("原生光束接口或创建失败，使用虚线预览。")));
            ImGui::TextUnformatted(Tr(U8("预测按静止悬停、轻按后松键计算；未检测障碍，不是落点。")));
            if(V.Suppressed) ImGui::TextUnformatted(Tr(U8("当前有按键输入：实际停止时机可能偏离预览。")));
        } else ImGui::TextUnformatted(Tr(U8("空中显示下一跳预览；地面跳跃不使用此预测。")));
    }
    ImGui::End();
    if(!V.Preview) return;
    auto* Draw=ImGui::GetBackgroundDrawList();
    auto XY=[](const auto& P){return ImVec2(P.X,P.Y);};
    const auto Color=IM_COL32(255,200,70,200);
    // The native beam replaces the yellow path, but both displays need an endpoint label.
    if(V.NativeArc!=1) {
        for(size_t I=1;I<V.Path.size();I+=2)
            if(V.Path[I-1].Valid && V.Path[I].Valid) Draw->AddLine(XY(V.Path[I-1]),XY(V.Path[I]),Color,2);
        if(V.Low.Valid && V.High.Valid) Draw->AddLine(XY(V.Low),XY(V.High),IM_COL32(255,200,70,65),7);
    }
    const auto End=V.Path.back();
    if(End.Valid) {
        const auto MarkerColor=V.NativeArc==1?IM_COL32(100,235,255,255):Color;
        Draw->AddCircle(XY(End),10,IM_COL32(0,0,0,220),24,5);
        Draw->AddCircle(XY(End),10,MarkerColor,24,2);
        Draw->AddCircleFilled(XY(End),2,MarkerColor);
        const auto* Label=Tr(U8("预计停止位置"));
        Draw->AddText({End.X+15,End.Y+1},IM_COL32(0,0,0,240),Label);
        Draw->AddText({End.X+14,End.Y},MarkerColor,Label);
    }
}
