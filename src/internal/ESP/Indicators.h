// Indicators.h
#pragma once
#define U8(str) reinterpret_cast<const char*>(u8##str)

#include <string>
#include "../../external/CppSDK/SDK.hpp"
#include "../../external/Shadow-Gui/include/Shadow.h"
#include "../Util/Util.h"
#include "../Config/Configs.h"
#include "../Hack/Hack.h"
#include "../Language/LanguageManager.h"

namespace g_Indicators {
    // 起始点
    constexpr float kStartX = 5.0f;
    constexpr float kStartY = 5.0f;

    // 行间距
    constexpr float kPaddingY = 0.0f;

    // 当前绘制的 Y 坐标偏移量
    inline float g_CurrentY = kStartY;

    // 在每帧绘制开始前调用，重置绘制位置到起始点 (5.0f, 5.0f)
    __forceinline void Reset() {
        g_CurrentY = kStartY;
    }

    __forceinline void AddTop(Shadow::Color color, const std::string& text) {
        if (text.empty()) return;
        static SDK::UFont* OpenSansRegular12 = g_Util::GetOpenSansRegular12();
        if (!OpenSansRegular12) return;

        Shadow::PushFont(OpenSansRegular12);
        Shadow::GetBackgroundDrawList()->AddText({ kStartX, g_CurrentY }, color, text);
        float textHeight = Shadow::MeasureTextSize(text).y;
        Shadow::PopFont();

        g_CurrentY += textHeight + kPaddingY;
    }

    __forceinline void OnRender() {
        if (!g_Config::bShowInfo) return;

        // 每帧开始前重置位置
        g_Indicators::Reset();

        static Shadow::Color col0 = { 1.f, 0.95f, 0.9f, 1.f };
        static Shadow::Color col1 = { 1.f, 1.f, 0.65f, 1.f };
        static Shadow::Color col2 = { 0.f, 1.f, 1.f, 1.f };
        static Shadow::Color col3 = { 0.f, 1.f, 0.f, 1.f };

        if (g_Config::bShowInfo) {
            float fps = g_Util::GetFPS();
            float server_fps = 0.f;
            int NumPlayerConnected = 0;
            int NumTamedDinos = 0;
            int MaxTamedDinos = 0;

            SDK::AShooterGameState* GS = g_Util::GetAShooterGameState();
            if (GS) {
                server_fps = GS->ServerFramerate;
                NumPlayerConnected = GS->NumPlayerConnected;
                NumTamedDinos = GS->NumTamedDinos;
                MaxTamedDinos = GS->MaxTamedDinos;
            }

            std::string fps_text = std::format("FPS: {:.0f}", fps);
            std::string server_fps_text = std::format("SV: {:.1f}", server_fps);

            std::string player = U8("玩家");
            std::string tamed = U8("驯养");
            std::string NumPlayerConnected_test = std::format("{}: {}", player, NumPlayerConnected);
            std::string TamedDinos_test = std::format("{}: {}/{}", tamed, NumTamedDinos, MaxTamedDinos);

            g_Indicators::AddTop(col0, fps_text);
            if (GS) {
                g_Indicators::AddTop(col1, server_fps_text);
                g_Indicators::AddTop(col2, NumPlayerConnected_test);
                g_Indicators::AddTop(col3, TamedDinos_test);
            }

            if (g_Config::bAutoHarvestResources) {
                static const Shadow::Color HarvestHeaderColor =
                    { 0.35f, 1.0f, 0.72f, 1.0f };
                static const Shadow::Color HarvestStatusColor =
                    { 0.82f, 0.95f, 1.0f, 1.0f };
                g_Indicators::AddTop(HarvestHeaderColor, U8("自动采集资源"));
                g_Indicators::AddTop(HarvestStatusColor,
                    g_Hack::GetAutoHarvestStatus());
            }
        }

    }
}
