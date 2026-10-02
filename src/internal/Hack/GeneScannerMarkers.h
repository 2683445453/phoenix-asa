#pragma once

#include <string>
#include <vector>

namespace SDK { class UWorld; }

namespace g_GeneScannerMarkers {
    struct MarkerInfo {
        std::string Label;
        float Latitude;
        float Longitude;
    };
    void Tick(SDK::UWorld* World);
    std::string GetStatus();
    std::vector<MarkerInfo> GetCandidates();
}
