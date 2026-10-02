#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace PhoenixExpansion {
    // Keep the camera clamp centered on the current view. A fixed nearly-full
    // range still traps yaw at its world-space seam (observed at 179.79 deg).
    inline std::pair<float,float> MountedYawWindow(double ControlYaw) {
        const double Center = std::isfinite(ControlYaw) ? std::remainder(ControlYaw,360.0) : 0.0;
        return {static_cast<float>(Center-170.0),static_cast<float>(Center+170.0)};
    }
    enum class Use { None, Claim, Imprint, Ride, Activate, Extract, RemoveBola };
    inline std::string Normalize(std::string Text) {
        std::string Result;
        for (unsigned char C : Text) {
            if (C <= 32 || C == '[' || C == ']') continue;
            Result += (C >= 'A' && C <= 'Z') ? char(C + 'a' - 'A') : char(C);
        }
        return Result;
    }
    inline Use ClassifyUse(const std::string& Text) {
        const auto S = Normalize(Text);
        const auto Has = [&](std::string_view V) { return S.find(V) != std::string::npos; };
        // Never confuse destructive or inverse actions with affirmative ones.
        if (Has("unclaim") || Has("取消认领") || Has("放生") || Has("放棄") || Has("取消认养") ||
            Has("disable") || Has("deactivate") || Has("turnoff") || Has("关闭") || Has("關閉")) return Use::None;
        if (Has("removebola") || Has("解开流星锤") || Has("移除流星锤")) return Use::RemoveBola;
        if (Has("extractembryo") || Has("提取胚胎") || Has("抽取胚胎")) return Use::Extract;
        if (S == "claim" || Has("claimbaby") || Has("认领") || Has("認領") || Has("认养")) return Use::Claim;
        if (Has("cuddle") || Has("拥抱") || Has("擁抱") || Has("抚摸")) return Use::Imprint;
        if (S == "ride" || S == "骑乘" || S == "騎乘") return Use::Ride;
        if (S == "turnon" || S == "activate" || S == "开启" || S == "启动" || S == "開啟") return Use::Activate;
        return Use::None;
    }
    enum class Supply { None, Gacha, Baby, Crop, Turret };
    inline bool MatchesSupply(Supply Kind, const std::string& ClassName) {
        const auto S = Normalize(ClassName);
        const auto Has = [&](std::string_view V) { return S.find(V) != std::string::npos; };
        switch (Kind) {
        case Supply::Gacha: return Has("seed") && (Has("planty") || Has("speciesy"));
        case Supply::Baby: return Has("primalitemconsumable_rawmeat") && !Has("spoiled");
        case Supply::Crop: return Has("primalitemconsumable_fertilizer") && !Has("seed");
        case Supply::Turret: return Has("primalitemammo_advancedriflebullet");
        default: return false;
        }
    }
    struct RadarPoint { double X = 0, Y = 0; bool Visible = false; };
    inline RadarPoint ProjectRadar(double DX, double DY, double YawDegrees, double RangeUnits) {
        if (!std::isfinite(DX) || !std::isfinite(DY) || !std::isfinite(YawDegrees) ||
            !std::isfinite(RangeUnits) || RangeUnits <= 0 || std::hypot(DX, DY) > RangeUnits) return {};
        const double A = YawDegrees * 0.017453292519943295;
        return {(-DX * std::sin(A) + DY * std::cos(A)) / RangeUnits,
                -(DX * std::cos(A) + DY * std::sin(A)) / RangeUnits, true};
    }
}
