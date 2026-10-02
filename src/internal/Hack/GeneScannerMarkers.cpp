#include "GeneScannerMarkers.h"
#include "../Config/Configs.h"
#include "../Util/Util.h"
#include "../../external/CppSDK/SDK.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <Windows.h>

namespace {
    struct Candidate {
        float Latitude;
        float Longitude;
        int SpeciesIndex;
        std::string Label;
    };
    std::mutex g_DataMutex;
    std::string g_Status = "自动标注已关闭";
    std::vector<Candidate> g_Candidates;
    constexpr const char* kScanTagPrefix = "PhoenixGeneScan_";
    unsigned g_NextTag = 1;
    bool g_WasEnabled = false;
    bool g_HasActiveTarget = false;
    int g_ActiveSpecies = -1;
    uintptr_t g_ActiveActor = 0;
    SDK::FVector g_ActiveLocation{};
    std::string g_ActiveMap;
    std::chrono::steady_clock::time_point g_NextCheck{};

    void SetStatus(std::string Text) {
        std::scoped_lock Lock(g_DataMutex);
        g_Status = std::move(Text);
    }
    SDK::AShooterPlayerController* GetController() {
        auto* PC = g_Util::GetLocalPC();
        auto* Type = SDK::AShooterPlayerController::StaticClass();
        return PC && Type && PC->IsA(Type)
            ? static_cast<SDK::AShooterPlayerController*>(PC) : nullptr;
    }
    SDK::FProperty* FindOwnProperty(SDK::UClass* Type, const char* Name) {
        if (!Type) return nullptr;
        for (SDK::FField* Field = Type->ChildProperties; Field; Field = Field->Next)
            if (Field->Name.ToString() == Name)
                return static_cast<SDK::FProperty*>(Field);
        return nullptr;
    }
    enum class ScanState { Ready, Inactive, Unsupported };
    ScanState ReadScanTarget(SDK::AShooterPlayerController* PC, SDK::FVector& Location,
                        int& SpeciesIndex, uintptr_t& TrackedActor, std::string& Reason) {
        auto* CharacterType = SDK::AShooterCharacter::StaticClass();
        if (!PC || !PC->Pawn || !CharacterType || !PC->Pawn->IsA(CharacterType)) {
            Reason = "等待人物就绪，请先下坐骑";
            return ScanState::Inactive;
        }
        auto* Character = static_cast<SDK::AShooterCharacter*>(PC->Pawn);
        auto* Weapon = Character->CurrentWeapon;
        if (!Weapon || !Weapon->Class || Weapon->Class->GetName() != "WeapGeneScanner_C") {
            Reason = "请手持基因扫描仪并开启找龙追踪";
            return ScanState::Inactive;
        }
        // The scanner's IsTracking blueprint combines these two replicated fields.
        // Its separate Success output is not used as a tracking-state gate.
        auto* IndexProperty = FindOwnProperty(Weapon->Class, "CurrentlyScanningSpeciesIndex");
        auto* LocationProperty = FindOwnProperty(Weapon->Class, "CurrentlyTrackedTargetLocation");
        if (!IndexProperty || IndexProperty->Offset != 4716 ||
            IndexProperty->ElementSize != sizeof(int) ||
            !IndexProperty->ClassPrivate || IndexProperty->ClassPrivate->Name.ToString() != "IntProperty" ||
            !LocationProperty || LocationProperty->Offset != 4752 ||
            LocationProperty->ElementSize != sizeof(SDK::FVector) ||
            !LocationProperty->ClassPrivate || LocationProperty->ClassPrivate->Name.ToString() != "StructProperty")
        {
            Reason = "扫描仪字段未匹配：物种索引或目标坐标";
            return ScanState::Unsupported;
        }
        const auto* Bytes = reinterpret_cast<const unsigned char*>(Weapon);
        std::memcpy(&SpeciesIndex, Bytes + IndexProperty->Offset, sizeof(SpeciesIndex));
        std::memcpy(&Location, Bytes + LocationProperty->Offset, sizeof(Location));
        TrackedActor = 0;
        if (auto* TrackProperty = FindOwnProperty(Weapon->Class, "GameplayComp_GeneScanner_Track");
            TrackProperty && TrackProperty->Offset == 4640 &&
            TrackProperty->ElementSize == sizeof(void*)) {
            SDK::UObject* Component = nullptr;
            std::memcpy(&Component, Bytes + TrackProperty->Offset, sizeof(Component));
            if (Component && Component->Class &&
                Component->Class->GetName() == "GameComp_GeneScanner_Track_C") {
                if (auto* ActorProperty = FindOwnProperty(Component->Class, "CurrentlyTrackedActor");
                    ActorProperty && ActorProperty->Offset == 640 &&
                    ActorProperty->ElementSize == sizeof(void*)) {
                    std::memcpy(&TrackedActor,
                        reinterpret_cast<const unsigned char*>(Component) + ActorProperty->Offset,
                        sizeof(TrackedActor));
                }
            }
        }
        if (SpeciesIndex < 0) {
            Reason = "扫描仪尚未选择追踪物种";
            return ScanState::Inactive;
        }
        if (!std::isfinite(Location.X) || !std::isfinite(Location.Y) ||
            !std::isfinite(Location.Z)) {
            Reason = "扫描仪返回了无效目标坐标";
            return ScanState::Unsupported;
        }
        if (Location.X == 0.0 && Location.Y == 0.0 && Location.Z == 0.0) {
            Reason = "物种已选择，等待扫描仪返回目标坐标";
            return ScanState::Inactive;
        }
        return ScanState::Ready;
    }
    std::string SpeciesName(int Index) {
        auto* Data = SDK::UPrimalGameData::BPGetGameData();
        if (!Data || !Data->DinoEntriesObjects.IsValid() ||
            Index < 0 || Index >= Data->DinoEntriesObjects.Num())
            return "物种 " + std::to_string(Index);
        auto* Entry = Data->DinoEntriesObjects[Index];
        if (!Entry || !Entry->IsA(SDK::UPrimalDinoEntry::StaticClass()))
            return "物种 " + std::to_string(Index);
        auto Name = Entry->DinoDescriptiveName.ToString();
        return Name.empty() ? Entry->DinoNameTag.ToString() : Name;
    }
    std::wstring ToWide(const std::string& Text) {
        const int Length = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(),
            static_cast<int>(Text.size()), nullptr, 0);
        if (Length <= 0) return {};
        std::wstring Wide(Length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, Text.c_str(),
            static_cast<int>(Text.size()), Wide.data(), Length);
        return Wide;
    }
    bool IsScanTag(const SDK::FName& Tag) {
        return Tag.ToString().starts_with(kScanTagPrefix);
    }
    SDK::UShooterGameUserSettings* GetMapSettings() {
        auto* Settings = SDK::UVictoryCore::GetShooterGameUserSettings();
        if (!Settings) return nullptr;
        const auto Count = Settings->SavedMinimapMarks.Num();
        return Count >= 0 && Count <= 10000 &&
            (Count == 0 || Settings->SavedMinimapMarks.IsValid()) ? Settings : nullptr;
    }
    const SDK::FMinimapMark* FindWaypointTemplate(
        SDK::UShooterGameUserSettings* Settings, const SDK::FName& MapName) {
        if (!Settings) return nullptr;
        const SDK::FMinimapMark* Fallback = nullptr;
        for (const auto& Mark : Settings->SavedMinimapMarks) {
            if (Mark.ID != 232 || !Mark.MarkIcon || IsScanTag(Mark.CustomTag)) continue;
            if (Mark.MapName.ToString() == MapName.ToString()) return &Mark;
            if (!Fallback) Fallback = &Mark;
        }
        return Fallback;
    }
    bool ClearNativeMarkers(SDK::AShooterPlayerController* PC) {
        auto* Settings = GetMapSettings();
        if (!PC || !Settings) { SetStatus("地图导航点尚未就绪"); return false; }
        std::vector<std::pair<SDK::FName, SDK::FName>> Tags;
        for (const auto& Mark : Settings->SavedMinimapMarks)
            if (IsScanTag(Mark.CustomTag)) Tags.emplace_back(Mark.CustomTag, Mark.MapName);
        for (const auto& [Tag, MapName] : Tags)
            PC->RemoveMinimapMark(Tag, MapName);
        for (const auto& Mark : Settings->SavedMinimapMarks) {
            if (IsScanTag(Mark.CustomTag)) {
                SetStatus("游戏尚未移除旧扫描导航点");
                return false;
            }
        }
        {
            std::scoped_lock Lock(g_DataMutex);
            g_Candidates.clear();
        }
        g_HasActiveTarget = false;
        g_ActiveSpecies = -1;
        g_ActiveActor = 0;
        g_ActiveMap.clear();
        return true;
    }
    bool MarkTarget(SDK::UWorld* World, SDK::AShooterPlayerController* PC,
                    const SDK::FVector& Location, int SpeciesIndex, uintptr_t TrackedActor,
                    const SDK::FString& MapText) {
        if (!World || !PC) { SetStatus("游戏世界或人物尚未就绪"); return false; }
        const auto GPS = SDK::UVictoryCore::CalculateGPSCoordinates(World, Location);
        const float Latitude = static_cast<float>(GPS.X) * 0.1f;
        const float Longitude = static_cast<float>(GPS.Y) * 0.1f;
        if (Latitude < 0.0f || Latitude > 100.0f ||
            Longitude < 0.0f || Longitude > 100.0f) {
            SetStatus("扫描目标坐标超出地图范围：" + std::to_string(GPS.X) + ", " +
                      std::to_string(GPS.Y));
            return false;
        }
        const std::string Name = SpeciesName(SpeciesIndex);
        auto* Settings = GetMapSettings();
        if (!Settings) { SetStatus("游戏导航点列表尚未就绪"); return false; }
        const auto MapName = SDK::UKismetStringLibrary::Conv_StringToName(MapText);
        const auto* Template = FindWaypointTemplate(Settings, MapName);
        if (!Template) { SetStatus("请先在地图中手动创建一个导航点以初始化图标"); return false; }
        const std::string Label = "待驯服 " + Name;
        const auto WideLabel = ToWide(Label);
        if (WideLabel.empty()) { SetStatus("无法转换导航点名称"); return false; }
        std::string NewTag;
        for (;;) {
            NewTag = std::string(kScanTagPrefix) + std::to_string(g_NextTag++);
            bool Taken = false;
            for (const auto& Existing : Settings->SavedMinimapMarks) {
                if (Existing.CustomTag.ToString() == NewTag &&
                    Existing.MapName.ToString() == MapName.ToString()) {
                    Taken = true;
                    break;
                }
            }
            if (!Taken) break;
        }
        const auto WideTag = ToWide(NewTag);
        const SDK::FString TagText(WideTag.c_str());
        auto Mark = *Template;
        Mark.Name = SDK::FString(WideLabel.c_str());
        Mark.CustomTag = SDK::UKismetStringLibrary::Conv_StringToName(TagText);
        Mark.Location = Location;
        Mark.MapName = MapName;
        Mark.Color = {0.0f, 1.0f, 0.75f, 1.0f};
        Mark.IconColor = {1.0f, 1.0f, 1.0f, 1.0f};
        Mark.bIsShowing = 0;
        Mark.bIsShowingText = 1;
        Mark.CharacterID = -1;
        Mark.CharacterIsPlayer = 0;
        const int Before = Settings->SavedMinimapMarks.Num();
        PC->NewMinimapMark(Mark);
        if (Settings->SavedMinimapMarks.Num() <= Before) {
            SetStatus("游戏未接受导航点，请检查地图导航点数量上限"); return false;
        }
        {
            std::scoped_lock Lock(g_DataMutex);
            g_Candidates.push_back({Latitude, Longitude, SpeciesIndex, Label});
            g_Status = "已创建导航点 " + Name + "，纬度 " + std::to_string(Latitude) +
                       " 经度 " + std::to_string(Longitude);
        }
        g_HasActiveTarget = true;
        g_ActiveSpecies = SpeciesIndex;
        g_ActiveActor = TrackedActor;
        g_ActiveLocation = Location;
        g_ActiveMap = MapText.ToString();
        return true;
    }
}

namespace g_GeneScannerMarkers {
    std::string GetStatus() {
        std::scoped_lock Lock(g_DataMutex);
        return g_Status;
    }
    std::vector<MarkerInfo> GetCandidates() {
        std::scoped_lock Lock(g_DataMutex);
        std::vector<MarkerInfo> Result;
        Result.reserve(g_Candidates.size());
        for (const auto& Point : g_Candidates)
            Result.push_back({Point.Label, Point.Latitude, Point.Longitude});
        return Result;
    }
    void Tick(SDK::UWorld* World) {
        auto* PC = GetController();
        const bool Enabled = g_Config::bAutoGeneScannerMarker;
        const auto Now = std::chrono::steady_clock::now();
        if (!Enabled) {
            if (g_WasEnabled || g_HasActiveTarget) {
                if (Now < g_NextCheck) return;
                g_NextCheck = Now + std::chrono::seconds(1);
                if (ClearNativeMarkers(PC)) {
                    g_WasEnabled = false;
                    SetStatus("自动标注已关闭，扫描导航点已清除");
                }
            }
            return;
        }
        if (Now < g_NextCheck) return;
        g_NextCheck = Now + std::chrono::milliseconds(250);
        if (!World || !PC) {
            SetStatus("等待游戏世界和人物就绪");
            return;
        }
        if (!g_WasEnabled) {
            if (!ClearNativeMarkers(PC)) return;
            g_WasEnabled = true;
            SetStatus("等待基因扫描仪开始追踪");
        }
        SDK::FVector Location{};
        int SpeciesIndex = -1;
        uintptr_t TrackedActor = 0;
        std::string Reason;
        const auto State = ReadScanTarget(PC, Location, SpeciesIndex, TrackedActor, Reason);
        if (State != ScanState::Ready) {
            if (State == ScanState::Inactive && g_HasActiveTarget) {
                if (!ClearNativeMarkers(PC)) return;
                SetStatus(Reason + "；旧扫描导航点已清除");
            } else SetStatus(Reason);
            return;
        }
        const auto MapText = SDK::UVictoryCore::BPGetPrimaryMapName(World);
        const double DX = Location.X - g_ActiveLocation.X;
        const double DY = Location.Y - g_ActiveLocation.Y;
        const bool Changed = !g_HasActiveTarget ||
            SpeciesIndex != g_ActiveSpecies ||
            MapText.ToString() != g_ActiveMap ||
            (TrackedActor && g_ActiveActor && TrackedActor != g_ActiveActor) ||
            DX * DX + DY * DY > 20000.0 * 20000.0;
        if (!Changed) return;
        if (!ClearNativeMarkers(PC)) return;
        MarkTarget(World, PC, Location, SpeciesIndex, TrackedActor, MapText);
    }
}
