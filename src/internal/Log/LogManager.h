#pragma once

namespace g_LogManager {

    struct LogEntry {
        float r;
        float g;
        float b;
        float base_a;
        float current_a;
        std::string text;
        float timer;
    };

    // 配置参数
    const int MAX_LOGS = 6;
    const float LOG_LIFETIME = 6.0f;     // 持续时间（秒）
    const float FADE_DURATION = 0.5f;    // 淡出动画时间（秒）
    const float START_X = 5.0f;
    const float START_Y = 5.0f;
    // const float LINE_SPACING = 2.0f;

    // 暴露的接口
    void AddLog(float r, float g, float b, float a, const std::string& text);
    void Run();

}