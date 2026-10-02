namespace {
    bool NoteExperimentWindowOpen = false;
    int NoteExperimentRange = 0;
    int NoteExperimentSelected = -1;

    void DrawNoteExperimentSample(const char* Label, const g_Hack::NoteExperimentSample& Sample) {
        ImGui::PushID(Label);
        Title(Label);
        if (!Sample.Valid) {
            ImGui::TextDisabled("%s", Tr(U8("未就绪")));
            ImGui::PopID();
            return;
        }
        if (Sample.HasExperience) ImGui::Text(Tr(U8("经验值：%.2f")), Sample.Experience);
        else ImGui::TextDisabled("%s", Tr(U8("经验值：未就绪")));
        if (!Sample.HasBuffs) ImGui::TextDisabled("%s", Tr(U8("Buff 列表：未就绪")));
        else if (Sample.Buffs.empty()) ImGui::TextDisabled("%s", Tr(U8("未检测到 Buff")));
        else if (ImGui::BeginTable("Buffs", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_BordersInnerV, {0, 145})) {
            ImGui::TableSetupColumn(Tr(U8("Buff / 实例 ID")), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(Tr(U8("剩余时间")), ImGuiTableColumnFlags_WidthFixed, 125);
            ImGui::TableHeadersRow();
            for (const auto& Buff : Sample.Buffs) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s [%d]", Buff.Name.c_str(), Buff.ObjectIndex);
                ImGui::TableSetColumnIndex(1);
                if (Buff.HasRemainingTime) ImGui::Text(Tr(U8("%.1f 秒")), Buff.RemainingSeconds);
                else ImGui::TextDisabled("%s", Tr(U8("未提供")));
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
}

void ShowNoteExperimentWindow() {
    NoteExperimentWindowOpen = true;
    g_Hack::SetNoteExperimentOpen(true);
}

void DrawNoteExperimentWindow() {
    if (!NoteExperimentWindowOpen) return;
    const auto View = g_Hack::GetNoteExperimentView();
    const auto Display = ImGui::GetIO().DisplaySize;
    const ImVec2 Maximum{std::max(240.f, Display.x - 16), std::max(240.f, Display.y - 16)};
    ImGui::SetNextWindowSize({std::min(960.f, Maximum.x), std::min(820.f, Maximum.y)}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({std::min(440.f, Maximum.x), std::min(360.f, Maximum.y)}, Maximum);
    ImGui::SetNextWindowPos({Display.x * .5f, Display.y * .5f}, ImGuiCond_Appearing, {.5f, .5f});
    const std::string WindowTitle = std::string(Tr(U8("笔记重复解锁实验"))) + "###noteExperiment";
    if (ImGui::Begin(WindowTitle.c_str(), &NoteExperimentWindowOpen, ImGuiWindowFlags_NoCollapse)) {
        const auto Position = ImGui::GetWindowPos(), Size = ImGui::GetWindowSize();
        ImGui::SetWindowPos({std::clamp(Position.x, 0.f, std::max(0.f, Display.x - Size.x)),
            std::clamp(Position.y, 0.f, std::max(0.f, Display.y - Size.y))});
        ImGui::TextWrapped("%s", Tr(U8("实验会重新请求已解锁笔记。对比是否再次出现 Buff 或计时变化；结果需结合实际效果判断。")));
        ImGui::Text("%s: %s", Tr(U8("连接状态")), View.Ready ? Tr(U8("已就绪")) : Tr(U8("未就绪")));
        ImGui::SameLine();
        ImGui::Text("%s: %s", Tr(U8("实验状态")), View.Observing ? Tr(U8("观察中")) :
            (View.Running ? Tr(U8("发送中")) : Tr(U8("空闲"))));
        if (!View.Status.empty()) ImGui::TextWrapped("%s", Tr(View.Status.c_str()));
        ImGui::Text(Tr(U8("第 %llu 轮 · 已发送 %d / %d")), static_cast<unsigned long long>(View.Round), View.Sent, View.Total);
        if (View.Total > 0) ImGui::ProgressBar(std::clamp(float(View.Sent) / View.Total, 0.f, 1.f), {-FLT_MIN, 0});
        ImGui::TextWrapped("%s: %s", Tr(U8("日志路径")), View.LogPath.empty() ? Tr(U8("未生成")) : View.LogPath.c_str());
        DrawAnimatedSeparator();

        const bool Busy = View.Running || View.Observing;
        ImGui::BeginDisabled(Busy);
        DrawCustomCombo(Tr(U8("实验范围")), &NoteExperimentRange,
            std::vector<const char*>{Tr(U8("命运之潮")), Tr(U8("所有经验笔记"))});
        if (DrawCustomButton(Tr(U8("刷新笔记列表")))) g_Hack::RefreshNoteExperiment();
        ImGui::EndDisabled();
        const auto InRange = [](const g_Hack::NoteExperimentEntry& Entry) {
            return NoteExperimentRange == 0 ? Entry.TidesOfFortune : !Entry.PreventXPBuff;
        };
        bool HasSelected = false, HasEntries = false;
        for (const auto& Entry : View.Entries) if (InRange(Entry)) {
            HasEntries = true;
            if (Entry.Index == NoteExperimentSelected) HasSelected = true;
        }
        if (!HasSelected) NoteExperimentSelected = -1;

        if (ImGui::BeginTable("NoteExperimentEntries", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable, {0, 205})) {
            ImGui::TableSetupColumn(Tr(U8("笔记 ID")), ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn(Tr(U8("笔记名称")), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(Tr(U8("本地已解锁")), ImGuiTableColumnFlags_WidthFixed, 105);
            ImGui::TableSetupColumn(Tr(U8("禁止经验 Buff")), ImGuiTableColumnFlags_WidthFixed, 115);
            ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
            for (const auto& Entry : View.Entries) if (InRange(Entry)) {
                ImGui::PushID(Entry.Index);
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(std::to_string(Entry.Index).c_str(), Entry.Index == NoteExperimentSelected,
                    ImGuiSelectableFlags_SpanAllColumns)) NoteExperimentSelected = Entry.Index;
                ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", Entry.Name.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(Entry.Unlocked ? Tr(U8("是")) : Tr(U8("否")));
                ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(Entry.PreventXPBuff ? Tr(U8("是")) : Tr(U8("否")));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!HasEntries) ImGui::TextDisabled("%s", Tr(U8("当前范围没有可用笔记")));
        ImGui::BeginDisabled(!View.Ready || Busy || NoteExperimentSelected < 0);
        if (DrawCustomButton(Tr(U8("重发所选笔记"))))
            g_Hack::RequestNoteExperiment(g_Hack::NoteExperimentScope::Selected, NoteExperimentSelected, View.CatalogGeneration);
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!View.Ready || Busy || !HasEntries);
        if (DrawCustomButton(Tr(U8("当前范围发送一轮"))))
            g_Hack::RequestNoteExperiment(NoteExperimentRange == 0 ? g_Hack::NoteExperimentScope::TidesOfFortune :
                g_Hack::NoteExperimentScope::AllXPNotes, -1, View.CatalogGeneration);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!Busy);
        if (DrawCustomButton(Tr(U8("停止实验")))) g_Hack::CancelNoteExperiment();
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", Tr(U8("整组跳过禁止经验 Buff 的笔记；单条可选作对照。每轮发送后观察 5 秒。")));
        DrawAnimatedSeparator();

        if (ImGui::GetContentRegionAvail().x >= 700 && ImGui::BeginTable("NoteExperimentSamples", 2,
            ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            DrawNoteExperimentSample(Tr(U8("发送前")), View.Before);
            ImGui::TableSetColumnIndex(1);
            DrawNoteExperimentSample(Tr(U8("发送后")), View.After);
            ImGui::EndTable();
        } else {
            DrawNoteExperimentSample(Tr(U8("发送前")), View.Before);
            DrawNoteExperimentSample(Tr(U8("发送后")), View.After);
        }
        if (View.Before.Valid && View.After.Valid && View.Before.HasExperience && View.After.HasExperience)
            ImGui::Text(Tr(U8("经验变化：%+.2f")), View.After.Experience - View.Before.Experience);
        else ImGui::TextDisabled("%s", Tr(U8("经验变化：未就绪")));
        ImGui::TextWrapped("%s", Tr(U8("经验变化也可能来自同期游戏行为；Buff 图标、实例和持续时间需结合日志判断。")));
        if (!View.Attempts.empty() && ImGui::CollapsingHeader(Tr(U8("本轮请求记录")))) {
            if (ImGui::BeginTable("NoteExperimentAttempts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                {0, 140})) {
                ImGui::TableSetupColumn(Tr(U8("笔记名称")), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(Tr(U8("解锁前")), ImGuiTableColumnFlags_WidthFixed, 85);
                ImGui::TableSetupColumn(Tr(U8("解锁后")), ImGuiTableColumnFlags_WidthFixed, 85);
                ImGui::TableHeadersRow();
                for (const auto& Attempt : View.Attempts) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%d · %s", Attempt.Index, Attempt.Name.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(Attempt.UnlockedBefore ? Tr(U8("是")) : Tr(U8("否")));
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(!Attempt.HasAfter ? Tr(U8("未就绪")) :
                        (Attempt.UnlockedAfter ? Tr(U8("是")) : Tr(U8("否"))));
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
    if (!NoteExperimentWindowOpen) g_Hack::SetNoteExperimentOpen(false);
}
