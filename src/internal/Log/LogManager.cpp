#include "../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "LogManager.h"
#include <string>
#include <vector>
#include <mutex>

namespace g_LogManager {

    // 存储 log 的队列
    static std::vector<LogEntry> logs;
    static std::mutex log_mutex;

    void AddLog(float r, float g, float b, float a, const std::string& text) {
        std::lock_guard<std::mutex> lock(log_mutex);

        if (static_cast<int>(logs.size()) >= MAX_LOGS) {
            logs.erase(logs.begin());
        }

        LogEntry entry;
        entry.r = r;
        entry.g = g;
        entry.b = b;
        entry.base_a = a;
        entry.current_a = a;
        entry.text = text;
        entry.timer = LOG_LIFETIME;

        logs.push_back(entry);
    }

    void Run() {
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        if (draw_list == nullptr) {
            return;
        }

        float deltaTime = ImGui::GetIO().DeltaTime;
        float current_y = START_Y;

        std::lock_guard<std::mutex> lock(log_mutex);

        for (size_t i = 0; i < logs.size(); ) {
            LogEntry& log = logs[i];

            // 1. 更新计时器
            log.timer -= deltaTime;

            // 处理生命周期与淡出逻辑
            if (log.timer <= 0.0f) {
                logs.erase(logs.begin() + static_cast<std::ptrdiff_t>(i));
                // 移除后不增加索引，继续检查当前位置的新元素
                continue;
            }
            else if (log.timer < FADE_DURATION) {
                float fade_pct = log.timer / FADE_DURATION;
                log.current_a = log.base_a * fade_pct;
            }

            // 2. 处理文本尺寸
            ImVec2 size = ImGui::CalcTextSize(log.text.c_str());
            float text_h = size.y;

            // 3. 绘制
            // 将颜色组件转换为 ImU32 (RGBA)
            ImU32 col = IM_COL32(
                static_cast<int>(log.r),
                static_cast<int>(log.g),
                static_cast<int>(log.b),
                static_cast<int>(log.current_a)
            );

            ImVec2 pos = ImVec2(START_X, current_y);
            ImU32 shadow_col = IM_COL32(0, 0, 0, static_cast<int>(log.current_a));
            ImVec2 shadow_offset = ImVec2(1.0f, 1.0f);

            // 阴影
            draw_list->AddText(
                ImVec2(pos.x + shadow_offset.x, pos.y + shadow_offset.y),
                shadow_col,
                log.text.c_str(),
                log.text.c_str() + log.text.size()
            );

            // 正文
            draw_list->AddText(
                pos,
                col,
                log.text.c_str(),
                log.text.c_str() + log.text.size()
            );

            // 4. 计算下一行 Y 坐标
            current_y += text_h;

            i++;
        }
    }
}