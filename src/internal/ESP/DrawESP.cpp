// DrawESP.cpp
#include "../../external/CppSDK/SDK.hpp"
#include "../../external/Shadow-Gui/include/Shadow.h"
#include "../CheatData/DynamicData.hpp"
#include "ESP.h"
#include "../Config/Configs.h"
#include "DrawESP.h"
#include "../Util/Util.h"
#include "../Language/LanguageManager.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <memory>
#include <cstdio>
#include <chrono>
#include <cctype>
#include <cstring>
#include <array>

namespace {
    // 将 g_Util 中的 ImU32 等价格式转换为 FLinearColor
    Shadow::Color U32ToFLinearColor(uint32_t color) {
        float r = (float)(color & 0xFF) / 255.0f;
        float g = (float)((color >> 8) & 0xFF) / 255.0f;
        float b = (float)((color >> 16) & 0xFF) / 255.0f;
        float a = (float)((color >> 24) & 0xFF) / 255.0f;
        return Shadow::Color{ r, g, b, a };
    }

    Shadow::Color GetHealthColorLinear(float percentage, float* colorMax, float* colorMin) {
        percentage = std::clamp(percentage, 0.0f, 1.0f);

        float r = colorMin[0] + (colorMax[0] - colorMin[0]) * percentage;
        float g = colorMin[1] + (colorMax[1] - colorMin[1]) * percentage;
        float b = colorMin[2] + (colorMax[2] - colorMin[2]) * percentage;
        float a = colorMin[3] + (colorMax[3] - colorMin[3]) * percentage;

        return Shadow::Color{ r, g, b, a };
    }

    bool EqualsResourceFieldName(const std::string& Left, const char* Right) {
        if (!Right || Left.size() != std::strlen(Right)) return false;
        for (size_t i = 0; i < Left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(Left[i])) !=
                std::tolower(static_cast<unsigned char>(Right[i]))) {
                return false;
            }
        }
        return true;
    }

    SDK::FProperty* FindResourceProperty(const SDK::UStruct* Type, const char* PropertyName) {
        for (const SDK::UStruct* Current = Type; Current; Current = Current->SuperStruct) {
            for (SDK::FField* Field = Current->ChildProperties; Field; Field = Field->Next) {
                if (EqualsResourceFieldName(Field->Name.ToString(), PropertyName)) {
                    return static_cast<SDK::FProperty*>(Field);
                }
            }
        }
        return nullptr;
    }

    SDK::UObject* ReadResourceObjectProperty(SDK::UObject* Object, const char* PropertyName) {
        if (!Object || !Object->Class) return nullptr;
        SDK::FProperty* Property = FindResourceProperty(Object->Class, PropertyName);
        if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
            Property->ElementSize < sizeof(SDK::UObject*)) return nullptr;

        const std::string PropertyType = Property->ClassPrivate->Name.ToString();
        if (!EqualsResourceFieldName(PropertyType, "ObjectProperty") &&
            !EqualsResourceFieldName(PropertyType, "ClassProperty")) {
            return nullptr;
        }
        return *reinterpret_cast<SDK::UObject**>(reinterpret_cast<uint8_t*>(Object) + Property->Offset);
    }

    bool ReadResourceObjectArray(SDK::UObject* Object, const char* PropertyName,
        SDK::UObject*** OutData, int32_t* OutNum) {
        if (!OutData || !OutNum || !Object || !Object->Class) return false;
        *OutData = nullptr;
        *OutNum = 0;

        SDK::FProperty* Property = FindResourceProperty(Object->Class, PropertyName);
        if (!Property || !Property->ClassPrivate || Property->Offset < 0 ||
            !EqualsResourceFieldName(Property->ClassPrivate->Name.ToString(), "ArrayProperty")) {
            return false;
        }

        SDK::FArrayProperty* ArrayProperty = static_cast<SDK::FArrayProperty*>(Property);
        if (!ArrayProperty->InnerProperty || !ArrayProperty->InnerProperty->ClassPrivate) return false;
        const std::string InnerType = ArrayProperty->InnerProperty->ClassPrivate->Name.ToString();
        if (!EqualsResourceFieldName(InnerType, "ObjectProperty") &&
            !EqualsResourceFieldName(InnerType, "WeakObjectProperty")) {
            return false;
        }

        struct RawObjectArray {
            SDK::UObject** Data;
            int32_t Num;
            int32_t Max;
        };
        const RawObjectArray* Array = reinterpret_cast<const RawObjectArray*>(
            reinterpret_cast<const uint8_t*>(Object) + Property->Offset);
        if (Array->Num < 0 || Array->Num > 4096 || Array->Max < Array->Num ||
            Array->Max > 16384 || (Array->Num > 0 && !Array->Data)) {
            return false;
        }

        *OutData = Array->Data;
        *OutNum = Array->Num;
        return true;
    }

    std::string NormalizeResourceIdentity(std::string Value) {
        Value.erase(std::remove_if(Value.begin(), Value.end(), [](unsigned char Character) {
            return !std::isalnum(Character);
            }), Value.end());
        std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Character) {
            return static_cast<char>(std::tolower(Character));
            });
        return Value;
    }

    void AppendResourceObjectIdentity(std::string& Identity, SDK::UObject* Object) {
        if (!Object || !Object->Class || Object->IsDefaultObject()) return;
        Identity += Object->GetFullName();
        Identity += Object->Class->GetName();
    }
}

namespace g_DrawESP {
    static constexpr float FADE_IN_TIME = 0.10f;
    static constexpr float FADE_OUT_TIME = 0.20f;

    struct CachedFlag {
        std::string       text;
        Shadow::Color color;
        g_ESP::FlagPos    pos;
    };

    struct CachedBar {
        float                 currentValue;
        float                 maxValue;
        Shadow::Color     color;
        g_ESP::BarPos         pos;
        g_ESP::BarOrientation orientation;
    };

    // -----------------------------------------------------------------------
    // 实体类型枚举，加入 ShooterCharacter(玩家) 和 PrimalDinoCharacter(生物) 分离
    // -----------------------------------------------------------------------
    enum class ActorType : uint8_t {
        Unknown = 0,
        ShooterCharacter,
        PrimalDinoCharacter,
        PrimalCharacter, // Fallback对于其它不可预知的基础生物/NPC
        PhysicsVolume,
        DroppedItem,
        PrimalStructure,
    };

    ActorType ClassifyESPActor(SDK::AActor* Actor) {
        if (!Actor || !Actor->Class) return ActorType::Unknown;
        if (Actor->IsA(SDK::AShooterCharacter::StaticClass())) {
            return ActorType::ShooterCharacter;
        }
        if (Actor->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            return ActorType::PrimalDinoCharacter;
        }
        if (Actor->IsA(SDK::APrimalCharacter::StaticClass())) {
            return ActorType::PrimalCharacter;
        }
        if (Actor->IsA(SDK::APhysicsVolume::StaticClass())) {
            return ActorType::PhysicsVolume;
        }
        if (Actor->IsA(SDK::ADroppedItem::StaticClass())) {
            return ActorType::DroppedItem;
        }
        if (Actor->IsA(SDK::APrimalStructure::StaticClass())) {
            return ActorType::PrimalStructure;
        }
        return ActorType::Unknown;
    }

    struct ESPEntry {
        uintptr_t      actorKey = 0;
        int32_t        objectIndex = -1;
        ActorType      actorType = ActorType::Unknown;
        SDK::FVector   lastWorldLoc{};
        g_ESP::BoxRect cachedRect;
        std::string    name;

        std::vector<CachedFlag> flags;
        std::vector<CachedBar>  bars;

        Shadow::Color boxColor{ 0,0,0,0 };
        Shadow::Color nameColor{ 0,0,0,0 };
        Shadow::Color distanceColor{ 0,0,0,0 };
        float  configBoxAlpha = 1.0f;
        float  targetAlpha = 0.0f;
        float  alpha = 0.0f;
        float  lastSeenTime = 0.0f;
        bool   aliveThisFrame = false;
        bool   isOOF = false;
        bool   isItem = false;
        float  cachedHP = 0.0f;
        float  cachedMaxHP = 0.0f;
        float  cachedTorpor = 0.0f;
        float  cachedMaxTorpor = 0.0f;

        bool shouldDrawBox = false;
        bool shouldDrawHealthBar = false;
        bool shouldDrawName = false;
        bool shouldDrawDistance = false;
        bool shouldDrawTorpor = false;
    };

    static std::unordered_map<uintptr_t, ESPEntry> s_entries;

    struct WaterCandidate {
        SDK::AActor* actor;
        float           dist;
        SDK::FVector2D  screenPos;
        SDK::FVector    surfaceLoc;
    };

    static std::vector<WaterCandidate> waterCandidates;
    static std::vector<uintptr_t>      s_toErase;

    enum class ResourceType : uint8_t {
        None = 0,
        Metal,
        Crystal,
        Obsidian,
        Oil,
        Pearl,
        ElementShard,
        Count,
    };

    struct ResourceMarker {
        SDK::FVector Location{};
        ResourceType Type = ResourceType::None;
        float DistanceSquared = 0.0f;
        std::vector<SDK::UPrimitiveComponent*> HighlightComponents;
    };

    struct ResourceHighlightState {
        int32_t ObjectIndex = -1;
        bool RenderCustomDepth = false;
        int32_t StencilValue = 0;
        SDK::ERendererStencilMask StencilWriteMask = SDK::ERendererStencilMask::ERSM_Default;
    };

    struct ResourceCameraHighlightState {
        SDK::AShooterPlayerCameraManager* CameraManager = nullptr;
        int32_t ObjectIndex = -1;
        SDK::UMaterialInterface* OutlineMaterial = nullptr;
        bool OutlineLoadAttempted = false;
        bool BlendableAdded = false;
        bool BlendableReplaced = false;
        int ReplacedBlendableIndex = -1;
        SDK::FWeightedBlendable ReplacedBlendable{};
        bool ExistingWeightChanged = false;
        int ExistingBlendableIndex = -1;
        float ExistingBlendableWeight = 0.0f;
    };

    struct TekNightVisionState {
        SDK::AShooterPlayerCameraManager* CameraManager = nullptr;
        int32_t ObjectIndex = -1;
        SDK::UMaterialInterface* Material = nullptr;
        std::chrono::steady_clock::time_point NextLoadAttempt =
            std::chrono::steady_clock::time_point::min();
        uint32_t LoadAttempts = 0;
        bool BlendableAdded = false;
        bool BlendableReplaced = false;
        int ReplacedBlendableIndex = -1;
        SDK::FWeightedBlendable ReplacedBlendable{};
        bool ExistingWeightChanged = false;
        int ExistingBlendableIndex = -1;
        float ExistingBlendableWeight = 0.0f;
    };

    struct ResourceRenderPipelineState {
        SDK::UGameViewportClient* ViewportClient = nullptr;
        int32_t ViewportObjectIndex = -1;
        bool PostProcessingEnabled = false;
        bool HasViewportState = false;
        int CustomDepthMode = -1;
        bool HasCustomDepthMode = false;
        int NaniteCustomDepthMode = -1;
        bool HasNaniteCustomDepthMode = false;
    };

    static std::vector<ResourceMarker> s_resourceMarkers;
    static std::unordered_map<SDK::UPrimitiveComponent*, ResourceHighlightState>
        s_resourceHighlightStates;
    static ResourceCameraHighlightState s_resourceCameraHighlightState{};
    static TekNightVisionState s_tekNightVisionState{};
    static std::string s_tekNightVisionStatus = "已关闭";
    static ResourceRenderPipelineState s_resourceRenderPipelineState{};
    static SDK::UWorld* s_resourceWorld = nullptr;
    static std::chrono::steady_clock::time_point s_nextResourceScan =
        std::chrono::steady_clock::time_point::min();
    static ResourceESPDiagnostics s_resourceDiagnostics{};

    bool IsResourceTypeEnabled(ResourceType Type);

    ResourceType ClassifyResourceIdentity(const std::string& RawIdentity) {
        const std::string Identity = NormalizeResourceIdentity(RawIdentity);
        if (Identity.empty()) return ResourceType::None;

        // Check the specific resource names before the harvesting component.
        // Crystal and obsidian nodes often use a metal-capable harvest component.
        if (Identity.find("elementshard") != std::string::npos ||
            Identity.find("shardsofelement") != std::string::npos ||
            Identity.find("elementrock") != std::string::npos) {
            return ResourceType::ElementShard;
        }
        if (Identity.find("obsidian") != std::string::npos) return ResourceType::Obsidian;
        if (Identity.find("crystal") != std::string::npos) return ResourceType::Crystal;
        if (Identity.find("silica") != std::string::npos ||
            Identity.find("pearl") != std::string::npos ||
            Identity.find("clam") != std::string::npos) return ResourceType::Pearl;
        if (Identity.find("oilharvest") != std::string::npos ||
            Identity.find("oilrock") != std::string::npos ||
            Identity.find("oilnode") != std::string::npos ||
            Identity.find("oilvein") != std::string::npos) return ResourceType::Oil;

        if (Identity.find("requiresmetal") != std::string::npos) return ResourceType::None;
        if (Identity.find("metalharvest") != std::string::npos ||
            Identity.find("richmetal") != std::string::npos ||
            Identity.find("rockmetal") != std::string::npos ||
            Identity.find("metalrock") != std::string::npos ||
            Identity.find("metalnode") != std::string::npos ||
            Identity.find("metalore") != std::string::npos) return ResourceType::Metal;
        return ResourceType::None;
    }

    ResourceType ClassifyResourceItemIdentity(const std::string& RawIdentity) {
        const std::string Identity = NormalizeResourceIdentity(RawIdentity);
        if (Identity.empty()) return ResourceType::None;

        if (Identity.find("elementshard") != std::string::npos &&
            Identity.find("unstableelementshard") == std::string::npos) {
            return ResourceType::ElementShard;
        }
        if (Identity.find("obsidian") != std::string::npos) return ResourceType::Obsidian;
        if (Identity.find("crystal") != std::string::npos) return ResourceType::Crystal;
        if (Identity.find("blackpearl") != std::string::npos ||
            Identity.find("silicon") != std::string::npos ||
            Identity.find("silica") != std::string::npos ||
            Identity.find("pearl") != std::string::npos) return ResourceType::Pearl;
        if (Identity.find("resourceoil") != std::string::npos ||
            Identity.find("oilresource") != std::string::npos) return ResourceType::Oil;
        if (Identity.find("metalingot") == std::string::npos &&
            (Identity.find("resourcemetal") != std::string::npos ||
                Identity.find("metalresource") != std::string::npos ||
                Identity.find("metalore") != std::string::npos ||
                Identity.find("scrapmetal") != std::string::npos)) {
            return ResourceType::Metal;
        }
        return ResourceType::None;
    }

    int ResourceTypePriority(ResourceType Type) {
        switch (Type) {
        case ResourceType::ElementShard: return 6;
        case ResourceType::Obsidian: return 5;
        case ResourceType::Crystal: return 4;
        case ResourceType::Pearl: return 3;
        case ResourceType::Oil: return 2;
        case ResourceType::Metal: return 1;
        default: return 0;
        }
    }

    ResourceType ClassifyHarvestingComponent(SDK::UPrimalHarvestingComponent* Component) {
        if (!Component || !Component->Class) return ResourceType::None;

        // A normal stone node can contain a small metal by-product. Treating the
        // mere presence of metal as a metal node caused nearly every cave rock
        // to be marked. Only use harvest data when a selected resource is the
        // dominant yield; explicit mesh/component names are handled separately.
        std::array<double, static_cast<size_t>(ResourceType::Count)> TypeScores{};
        double OtherResourceScore = 0.0;
        auto InspectEntries = [&](const SDK::TArray<SDK::FHarvestResourceEntry>& Entries) {
            if (Entries.Num() < 0 || Entries.Num() > 512) return;
            for (int32_t EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex) {
                const SDK::FHarvestResourceEntry& Entry = Entries[EntryIndex];
                SDK::UClass* ResourceClass = Entry.ResourceItem;
                if (!ResourceClass || !ResourceClass->Class) continue;

                std::string Identity = ResourceClass->GetFullName();
                Identity += ResourceClass->GetName();
                const ResourceType Candidate = ClassifyResourceItemIdentity(Identity);

                const double Weight = std::isfinite(Entry.EntryWeight) && Entry.EntryWeight > 0.0f
                    ? static_cast<double>(Entry.EntryWeight) : 1.0;
                const double MinimumQuantity = std::max(0, Entry.OverrideQuantityMin);
                const double MaximumQuantity = std::max(0, Entry.OverrideQuantityMax);
                const double Quantity = std::max(1.0, (MinimumQuantity + MaximumQuantity) * 0.5);
                const double Score = Weight * Quantity;

                const size_t CandidateIndex = static_cast<size_t>(Candidate);
                if (Candidate != ResourceType::None && CandidateIndex < TypeScores.size()) {
                    TypeScores[CandidateIndex] = std::max(TypeScores[CandidateIndex], Score);
                }
                else {
                    OtherResourceScore = std::max(OtherResourceScore, Score);
                }
            }
        };

        InspectEntries(Component->HarvestResourceEntries);
        InspectEntries(Component->BaseHarvestResourceEntries);

        // Lunar shard rocks commonly yield more stone/metal than shards. The
        // presence of the real Element Shard item is nevertheless definitive
        // and must win over the generic dominant-yield heuristic.
        if (TypeScores[static_cast<size_t>(ResourceType::ElementShard)] > 0.0) {
            return ResourceType::ElementShard;
        }

        ResourceType BestType = ResourceType::None;
        double BestScore = 0.0;
        for (size_t TypeIndex = 1;
            TypeIndex < static_cast<size_t>(ResourceType::Count); ++TypeIndex) {
            const ResourceType Candidate = static_cast<ResourceType>(TypeIndex);
            const double Score = TypeScores[TypeIndex];
            if (Score > BestScore || (Score == BestScore &&
                ResourceTypePriority(Candidate) > ResourceTypePriority(BestType))) {
                BestScore = Score;
                BestType = Candidate;
            }
        }
        if (BestType == ResourceType::None || BestScore <= 0.0) return ResourceType::None;

        double CompetingScore = OtherResourceScore;
        for (size_t TypeIndex = 1;
            TypeIndex < static_cast<size_t>(ResourceType::Count); ++TypeIndex) {
            if (TypeIndex != static_cast<size_t>(BestType)) {
                CompetingScore = std::max(CompetingScore, TypeScores[TypeIndex]);
            }
        }
        return CompetingScore <= 0.0 || BestScore >= CompetingScore * 1.25
            ? BestType : ResourceType::None;
    }

    ResourceType ClassifyHarvestingClass(SDK::UClass* ComponentClass) {
        SDK::UClass* HarvestingClass = SDK::UPrimalHarvestingComponent::StaticClass();
        if (!HarvestingClass || !ComponentClass || !ComponentClass->ClassDefaultObject ||
            !ComponentClass->ClassDefaultObject->Class ||
            !ComponentClass->ClassDefaultObject->IsA(HarvestingClass)) {
            return ResourceType::None;
        }
        return ClassifyHarvestingComponent(
            static_cast<SDK::UPrimalHarvestingComponent*>(ComponentClass->ClassDefaultObject));
    }

    bool IsResourceTypeEnabled(ResourceType Type) {
        switch (Type) {
        case ResourceType::Metal: return g_Config::bResourceMetal;
        case ResourceType::Crystal: return g_Config::bResourceCrystal;
        case ResourceType::Obsidian: return g_Config::bResourceObsidian;
        case ResourceType::Oil: return g_Config::bResourceOil;
        case ResourceType::Pearl: return g_Config::bResourcePearl;
        case ResourceType::ElementShard: return g_Config::bResourceElementShard;
        default: return false;
        }
    }

    bool IsResourceTypeNeeded(ResourceType Type) {
        return IsResourceTypeEnabled(Type) ||
            (g_Config::bAutoHarvestTekStriderElementShard &&
                Type == ResourceType::ElementShard);
    }

    const char* GetResourceTypeLabel(ResourceType Type) {
        switch (Type) {
        case ResourceType::Metal: return LanguageManager::Visuals_Menu::ResourceMetal;
        case ResourceType::Crystal: return LanguageManager::Visuals_Menu::ResourceCrystal;
        case ResourceType::Obsidian: return LanguageManager::Visuals_Menu::ResourceObsidian;
        case ResourceType::Oil: return LanguageManager::Visuals_Menu::ResourceOil;
        case ResourceType::Pearl: return LanguageManager::Visuals_Menu::ResourcePearl;
        case ResourceType::ElementShard:
            return LanguageManager::Visuals_Menu::ResourceElementShard;
        default: return "";
        }
    }

    Shadow::Color GetResourceTypeColor(ResourceType Type) {
        const float* Color = nullptr;
        switch (Type) {
        case ResourceType::Metal: Color = g_Config::ResourceMetalColor; break;
        case ResourceType::Crystal: Color = g_Config::ResourceCrystalColor; break;
        case ResourceType::Obsidian: Color = g_Config::ResourceObsidianColor; break;
        case ResourceType::Oil: Color = g_Config::ResourceOilColor; break;
        case ResourceType::Pearl: Color = g_Config::ResourcePearlColor; break;
        case ResourceType::ElementShard: Color = g_Config::ResourceElementShardColor; break;
        default: return Shadow::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        }
        return Shadow::Color{ Color[0], Color[1], Color[2], Color[3] };
    }

    float ResourceDistanceSquared(const SDK::FVector& A, const SDK::FVector& B) {
        const double X = A.X - B.X;
        const double Y = A.Y - B.Y;
        const double Z = A.Z - B.Z;
        return static_cast<float>(X * X + Y * Y + Z * Z);
    }

    bool IsFiniteResourceVector(const SDK::FVector& Value) {
        return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
    }

    SDK::FVector TransformResourcePosition(const SDK::FTransform& Transform,
        const SDK::FVector& LocalPosition) {
        SDK::FVector Scaled{
            LocalPosition.X * Transform.Scale3D.X,
            LocalPosition.Y * Transform.Scale3D.Y,
            LocalPosition.Z * Transform.Scale3D.Z
        };

        double QX = Transform.Rotation.X;
        double QY = Transform.Rotation.Y;
        double QZ = Transform.Rotation.Z;
        double QW = Transform.Rotation.W;
        const double QuaternionLength = std::sqrt(QX * QX + QY * QY + QZ * QZ + QW * QW);
        if (!std::isfinite(QuaternionLength) || QuaternionLength < 0.000001) {
            return SDK::FVector{
                Transform.Translation.X + Scaled.X,
                Transform.Translation.Y + Scaled.Y,
                Transform.Translation.Z + Scaled.Z
            };
        }

        QX /= QuaternionLength;
        QY /= QuaternionLength;
        QZ /= QuaternionLength;
        QW /= QuaternionLength;

        const double TX = 2.0 * (QY * Scaled.Z - QZ * Scaled.Y);
        const double TY = 2.0 * (QZ * Scaled.X - QX * Scaled.Z);
        const double TZ = 2.0 * (QX * Scaled.Y - QY * Scaled.X);

        return SDK::FVector{
            Transform.Translation.X + Scaled.X + QW * TX + (QY * TZ - QZ * TY),
            Transform.Translation.Y + Scaled.Y + QW * TY + (QZ * TX - QX * TZ),
            Transform.Translation.Z + Scaled.Z + QW * TZ + (QX * TY - QY * TX)
        };
    }

    bool IsLiveResourcePrimitive(SDK::UPrimitiveComponent* Component, int32_t ObjectIndex) {
        if (!Component || ObjectIndex < 0 ||
            SDK::UObject::GObjects->GetByIndex(ObjectIndex) != Component) {
            return false;
        }
        SDK::UClass* PrimitiveClass = SDK::UPrimitiveComponent::StaticClass();
        return PrimitiveClass && Component->Class && Component->IsA(PrimitiveClass);
    }

    void RestoreResourceHighlight(SDK::UPrimitiveComponent* Component,
        const ResourceHighlightState& State) {
        if (!IsLiveResourcePrimitive(Component, State.ObjectIndex)) return;
        Component->SetCustomDepthStencilValue(State.StencilValue);
        Component->SetCustomDepthStencilWriteMask(State.StencilWriteMask);
        Component->SetRenderCustomDepth(State.RenderCustomDepth);
    }

    bool IsLiveResourceCamera(SDK::AShooterPlayerCameraManager* CameraManager,
        int32_t ObjectIndex) {
        if (!CameraManager || ObjectIndex < 0 ||
            SDK::UObject::GObjects->GetByIndex(ObjectIndex) != CameraManager) {
            return false;
        }
        SDK::UClass* CameraManagerClass = SDK::AShooterPlayerCameraManager::StaticClass();
        return CameraManagerClass && CameraManager->Class && CameraManager->IsA(CameraManagerClass);
    }

    SDK::UMaterialInterface* FindResourceOutlineMaterial() {
        constexpr const char* CandidateNames[] = {
            "PP_ASA_CustomDepthOutline_MIC",
            "ESPMaterialOutline_MIC",
            "PP_HuntTargetOutline_MIC",
            "PP_StriderRadarOutlines_MIC",
            "MI_HuntingOutlinePPV"
        };
        SDK::UClass* MaterialClass = SDK::UMaterialInterface::StaticClass();
        if (!MaterialClass) return nullptr;

        for (const char* CandidateName : CandidateNames) {
            SDK::UObject* Object = SDK::UObject::FindObjectFast<>(CandidateName);
            if (Object && Object->Class && Object->IsA(MaterialClass)) {
                return static_cast<SDK::UMaterialInterface*>(Object);
            }
        }

        if (!s_resourceCameraHighlightState.OutlineLoadAttempted) {
            s_resourceCameraHighlightState.OutlineLoadAttempted = true;
            constexpr const wchar_t* CandidatePaths[] = {
                L"/Game/PrimalEarth/Effects/PostProcess/PP_ASA_CustomDepthOutline_MIC.PP_ASA_CustomDepthOutline_MIC",
                L"/Game/PrimalEarth/Effects/PostProcess/ESPMaterialOutline_MIC.ESPMaterialOutline_MIC",
                L"/Game/Genesis/Missions/Hunt/PP_HuntTargetOutline_MIC.PP_HuntTargetOutline_MIC",
                L"/Game/Genesis2/Effects/PostProcess/PP_StriderRadarOutlines_MIC.PP_StriderRadarOutlines_MIC"
            };
            for (const wchar_t* CandidatePath : CandidatePaths) {
                SDK::UObject* Object = SDK::UVictoryCore::BPLoadObject(
                    SDK::FString(CandidatePath));
                if (Object && Object->Class && Object->IsA(MaterialClass)) {
                    return static_cast<SDK::UMaterialInterface*>(Object);
                }
            }
        }
        return nullptr;
    }

    void EnsureResourceOutlineBlendable(SDK::AShooterPlayerCameraManager* CameraManager) {
        if (!CameraManager || CameraManager != s_resourceCameraHighlightState.CameraManager) return;

        SDK::UMaterialInterface*& OutlineMaterial =
            s_resourceCameraHighlightState.OutlineMaterial;
        if (!OutlineMaterial || OutlineMaterial->Index < 0 ||
            SDK::UObject::GObjects->GetByIndex(OutlineMaterial->Index) != OutlineMaterial) {
            OutlineMaterial = FindResourceOutlineMaterial();
        }
        if (!OutlineMaterial) {
            s_resourceDiagnostics.OutlineMaterialStatus = "not loaded";
            return;
        }

        s_resourceDiagnostics.OutlineMaterialStatus = OutlineMaterial->GetFullName();
        auto& Blendables = CameraManager->PPMaterials.WeightedBlendables.Array;
        const int Count = Blendables.Num();
        if (Count < 0 || Count > 4096 || Blendables.Max() < Count || Blendables.Max() > 4096) {
            s_resourceDiagnostics.OutlineMaterialStatus += " (invalid PP array)";
            return;
        }

        for (int Index = 0; Index < Count; ++Index) {
            if (Blendables[Index].Object != OutlineMaterial) continue;
            if (!s_resourceCameraHighlightState.BlendableAdded &&
                !s_resourceCameraHighlightState.BlendableReplaced &&
                !s_resourceCameraHighlightState.ExistingWeightChanged) {
                s_resourceCameraHighlightState.ExistingWeightChanged = true;
                s_resourceCameraHighlightState.ExistingBlendableIndex = Index;
                s_resourceCameraHighlightState.ExistingBlendableWeight = Blendables[Index].Weight;
            }
            Blendables[Index].Weight = 1.0f;
            s_resourceDiagnostics.OutlineMaterialStatus += " (active)";
            return;
        }

        SDK::FWeightedBlendable OutlineBlendable{};
        OutlineBlendable.Weight = 1.0f;
        OutlineBlendable.Object = OutlineMaterial;
        if (Blendables.Add(OutlineBlendable)) {
            s_resourceCameraHighlightState.BlendableAdded = true;
            s_resourceDiagnostics.OutlineMaterialStatus += " (added)";
            return;
        }

        if (Count > 0 && !s_resourceCameraHighlightState.BlendableReplaced) {
            int ReplacementIndex = -1;
            for (int Index = Count - 1; Index >= 0; --Index) {
                if (Blendables[Index].Object == s_tekNightVisionState.Material) continue;
                ReplacementIndex = Index;
                break;
            }
            if (ReplacementIndex >= 0) {
                s_resourceCameraHighlightState.BlendableReplaced = true;
                s_resourceCameraHighlightState.ReplacedBlendableIndex = ReplacementIndex;
                s_resourceCameraHighlightState.ReplacedBlendable = Blendables[ReplacementIndex];
                Blendables[ReplacementIndex] = OutlineBlendable;
                s_resourceDiagnostics.OutlineMaterialStatus += " (replaced free effect slot)";
                return;
            }
        }

        s_resourceDiagnostics.OutlineMaterialStatus += " (no PP slot)";
    }

    void RestoreResourceOutlineBlendable(SDK::AShooterPlayerCameraManager* CameraManager) {
        if (!CameraManager || !s_resourceCameraHighlightState.OutlineMaterial) return;
        auto& Blendables = CameraManager->PPMaterials.WeightedBlendables.Array;
        const int Count = Blendables.Num();
        if (Count < 0 || Count > 4096) return;

        if (s_resourceCameraHighlightState.BlendableReplaced) {
            const int Index = s_resourceCameraHighlightState.ReplacedBlendableIndex;
            if (Index >= 0 && Index < Count &&
                Blendables[Index].Object == s_resourceCameraHighlightState.OutlineMaterial) {
                Blendables[Index] = s_resourceCameraHighlightState.ReplacedBlendable;
            }
        }
        else if (s_resourceCameraHighlightState.BlendableAdded) {
            for (int Index = Blendables.Num() - 1; Index >= 0; --Index) {
                if (Blendables[Index].Object == s_resourceCameraHighlightState.OutlineMaterial) {
                    Blendables.Remove(Index);
                }
            }
        }
        else if (s_resourceCameraHighlightState.ExistingWeightChanged) {
            const int Index = s_resourceCameraHighlightState.ExistingBlendableIndex;
            if (Index >= 0 && Index < Count &&
                Blendables[Index].Object == s_resourceCameraHighlightState.OutlineMaterial) {
                Blendables[Index].Weight =
                    s_resourceCameraHighlightState.ExistingBlendableWeight;
            }
        }
    }

    void RestoreResourceCameraHighlight() {
        if (IsLiveResourceCamera(s_resourceCameraHighlightState.CameraManager,
            s_resourceCameraHighlightState.ObjectIndex)) {
            RestoreResourceOutlineBlendable(s_resourceCameraHighlightState.CameraManager);
        }
        s_resourceCameraHighlightState = {};
        s_resourceDiagnostics.CameraStencilEnabled = false;
        s_resourceDiagnostics.CameraPPBlendables = 0;
        s_resourceDiagnostics.CameraPPBlendableCapacity = 0;
        s_resourceDiagnostics.CameraPPAdjusters = 0;
        s_resourceDiagnostics.CameraBlendCache = 0;
        s_resourceDiagnostics.CameraPPMaterials.clear();
        s_resourceDiagnostics.OutlineMaterialStatus.clear();
    }

    SDK::AShooterCharacter* ResolveLocalTekHelmetOwner(SDK::APlayerController* LocalPC) {
        if (!LocalPC || !LocalPC->Pawn || !LocalPC->Pawn->Class) return nullptr;

        if (LocalPC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
            SDK::APrimalDinoCharacter* Dino =
                static_cast<SDK::APrimalDinoCharacter*>(LocalPC->Pawn);
            SDK::AShooterCharacter* Rider = Dino->Rider.Get();
            return Rider && Rider->Class && Rider->IsA(SDK::AShooterCharacter::StaticClass())
                ? Rider : nullptr;
        }
        if (LocalPC->Pawn->IsA(SDK::AShooterCharacter::StaticClass())) {
            return static_cast<SDK::AShooterCharacter*>(LocalPC->Pawn);
        }
        return nullptr;
    }

    bool HasEquippedTekHelmet(SDK::APlayerController* LocalPC) {
        SDK::AShooterCharacter* Character = ResolveLocalTekHelmetOwner(LocalPC);
        if (!Character || !Character->MyInventoryComponent) return false;

        const auto& EquippedItems = Character->MyInventoryComponent->EquippedItems;
        if (EquippedItems.Num() < 0 || EquippedItems.Num() > 256) return false;
        for (int Index = 0; Index < EquippedItems.Num(); ++Index) {
            SDK::UPrimalItem* Item = EquippedItems[Index];
            if (!Item || !Item->Class ||
                Item->MyEquipmentType != SDK::EPrimalEquipmentType::Hat) {
                continue;
            }

            std::string Identity = Item->GetFullName();
            Identity += Item->GetName();
            Identity += Item->Class->GetFullName();
            Identity += Item->Class->GetName();
            Identity = NormalizeResourceIdentity(std::move(Identity));
            if (Identity.find("tekhelmet") != std::string::npos) return true;
        }
        return false;
    }

    SDK::UMaterialInterface* FindTekNightVisionMaterial() {
        SDK::UClass* MaterialClass = SDK::UMaterialInterface::StaticClass();
        if (!MaterialClass) return nullptr;

        constexpr const char* CandidateNames[] = {
            "TEKNightVision_PP_MIC",
            "TEKNightVision_PP_MAT"
        };
        for (const char* CandidateName : CandidateNames) {
            SDK::UObject* Object = SDK::UObject::FindObjectFast<>(CandidateName);
            if (Object && Object->Class && Object->IsA(MaterialClass)) {
                return static_cast<SDK::UMaterialInterface*>(Object);
            }
        }

        const auto Now = std::chrono::steady_clock::now();
        if (Now < s_tekNightVisionState.NextLoadAttempt) return nullptr;
        s_tekNightVisionState.NextLoadAttempt = Now + std::chrono::seconds(2);
        ++s_tekNightVisionState.LoadAttempts;

        // Asset loading can fail while a map or DLC package is still being
        // mounted. Retry instead of permanently latching the first failure.
        constexpr const wchar_t* CandidatePaths[] = {
            L"/Game/PrimalEarth/Effects/PostProcess/TEKNightVision_PP_MIC.TEKNightVision_PP_MIC",
            L"/Game/PrimalEarth/Effects/PostProcess/TEKNightVision_PP_MAT.TEKNightVision_PP_MAT",
            L"/Game/PrimalEarth/Effects/PostProcess/TEKNightVision_PP_MIC",
            L"/Game/PrimalEarth/Effects/PostProcess/TEKNightVision_PP_MAT"
        };
        for (const wchar_t* CandidatePath : CandidatePaths) {
            SDK::UObject* Object = SDK::UVictoryCore::BPLoadObject(
                SDK::FString(CandidatePath));
            if (Object && Object->Class && Object->IsA(MaterialClass)) {
                return static_cast<SDK::UMaterialInterface*>(Object);
            }
        }
        return nullptr;
    }

    void RestoreTekNightVisionBlendable(SDK::AShooterPlayerCameraManager* CameraManager) {
        if (!CameraManager || !s_tekNightVisionState.Material) return;
        auto& Blendables = CameraManager->PPMaterials.WeightedBlendables.Array;
        const int Count = Blendables.Num();
        if (Count < 0 || Count > 4096) return;

        if (s_tekNightVisionState.BlendableReplaced) {
            const int Index = s_tekNightVisionState.ReplacedBlendableIndex;
            if (Index >= 0 && Index < Count &&
                Blendables[Index].Object == s_tekNightVisionState.Material) {
                Blendables[Index] = s_tekNightVisionState.ReplacedBlendable;
            }
        }
        else if (s_tekNightVisionState.BlendableAdded) {
            for (int Index = Blendables.Num() - 1; Index >= 0; --Index) {
                if (Blendables[Index].Object == s_tekNightVisionState.Material) {
                    Blendables.Remove(Index);
                }
            }
        }
        else if (s_tekNightVisionState.ExistingWeightChanged) {
            const int Index = s_tekNightVisionState.ExistingBlendableIndex;
            if (Index >= 0 && Index < Count &&
                Blendables[Index].Object == s_tekNightVisionState.Material) {
                Blendables[Index].Weight = s_tekNightVisionState.ExistingBlendableWeight;
            }
        }
    }

    void RestoreTekNightVision() {
        if (IsLiveResourceCamera(s_tekNightVisionState.CameraManager,
            s_tekNightVisionState.ObjectIndex)) {
            RestoreTekNightVisionBlendable(s_tekNightVisionState.CameraManager);
        }
        s_tekNightVisionState = {};
    }

    bool EnsureTekNightVisionBlendable(SDK::AShooterPlayerCameraManager* CameraManager) {
        if (!CameraManager || CameraManager != s_tekNightVisionState.CameraManager) return false;

        SDK::UMaterialInterface*& Material = s_tekNightVisionState.Material;
        if (!Material || Material->Index < 0 ||
            SDK::UObject::GObjects->GetByIndex(Material->Index) != Material) {
            Material = FindTekNightVisionMaterial();
        }
        if (!Material) {
            s_tekNightVisionStatus = std::format(
                "夜视材质暂未加载，2秒后重试（第{}次）",
                s_tekNightVisionState.LoadAttempts);
            return false;
        }

        auto& Blendables = CameraManager->PPMaterials.WeightedBlendables.Array;
        const int Count = Blendables.Num();
        if (Count < 0 || Count > 4096 || Blendables.Max() < Count ||
            Blendables.Max() > 4096) {
            s_tekNightVisionStatus = "镜头后处理数组无效";
            return false;
        }

        for (int Index = 0; Index < Count; ++Index) {
            if (Blendables[Index].Object != Material) continue;
            if (!s_tekNightVisionState.BlendableAdded &&
                !s_tekNightVisionState.BlendableReplaced &&
                !s_tekNightVisionState.ExistingWeightChanged) {
                s_tekNightVisionState.ExistingWeightChanged = true;
                s_tekNightVisionState.ExistingBlendableIndex = Index;
                s_tekNightVisionState.ExistingBlendableWeight = Blendables[Index].Weight;
            }
            Blendables[Index].Weight = 1.0f;
            s_tekNightVisionStatus = "已启用：" + Material->GetName();
            return true;
        }

        SDK::FWeightedBlendable NightVisionBlendable{};
        NightVisionBlendable.Weight = 1.0f;
        NightVisionBlendable.Object = Material;
        if (Blendables.Add(NightVisionBlendable)) {
            s_tekNightVisionState.BlendableAdded = true;
            s_tekNightVisionStatus = "已启用：" + Material->GetName();
            return true;
        }

        // The generated SDK cannot grow a live TArray. When the two camera PP
        // slots are already occupied, night vision deliberately replaces the
        // Tek helmet overlay slot first. Resource outlining uses a different
        // slot, so the two effects remain independent and the scanner overlay
        // cannot be accidentally reactivated.
        if (Count > 0 && !s_tekNightVisionState.BlendableReplaced) {
            int ReplacementIndex = -1;
            for (int Index = 0; Index < Count; ++Index) {
                SDK::UObject* Object = Blendables[Index].Object;
                if (Object == s_resourceCameraHighlightState.OutlineMaterial) continue;
                std::string Identity = Object && Object->Class
                    ? NormalizeResourceIdentity(Object->GetFullName() + Object->GetName())
                    : std::string{};
                if (Identity.find("tekhelmet") != std::string::npos &&
                    Identity.find("overlay") != std::string::npos) {
                    ReplacementIndex = Index;
                    break;
                }
                if (ReplacementIndex < 0) ReplacementIndex = Index;
            }
            if (ReplacementIndex >= 0) {
                s_tekNightVisionState.BlendableReplaced = true;
                s_tekNightVisionState.ReplacedBlendableIndex = ReplacementIndex;
                s_tekNightVisionState.ReplacedBlendable = Blendables[ReplacementIndex];
                Blendables[ReplacementIndex] = NightVisionBlendable;
                s_tekNightVisionStatus = "已启用（替换头盔扫描覆盖层）：" +
                    Material->GetName();
                return true;
            }
        }

        s_tekNightVisionStatus = "镜头后处理槽位不足";
        return false;
    }

    void MaintainTekNightVision(SDK::APlayerController* LocalPC) {
        if (!g_Config::bTekHelmetNightVision) {
            RestoreTekNightVision();
            s_tekNightVisionStatus = "已关闭";
            return;
        }
        if (!LocalPC || !LocalPC->PlayerCameraManager) {
            RestoreTekNightVision();
            s_tekNightVisionStatus = "等待游戏镜头";
            return;
        }
        SDK::APlayerCameraManager* BaseCameraManager = LocalPC->PlayerCameraManager;
        SDK::UClass* ShooterCameraManagerClass = SDK::AShooterPlayerCameraManager::StaticClass();
        if (!ShooterCameraManagerClass || !BaseCameraManager->Class ||
            !BaseCameraManager->IsA(ShooterCameraManagerClass)) {
            RestoreTekNightVision();
            s_tekNightVisionStatus = "当前镜头不兼容";
            return;
        }

        auto* CameraManager = static_cast<SDK::AShooterPlayerCameraManager*>(BaseCameraManager);
        if (s_tekNightVisionState.CameraManager != CameraManager) {
            RestoreTekNightVision();
            s_tekNightVisionState.CameraManager = CameraManager;
            s_tekNightVisionState.ObjectIndex = CameraManager->Index;
        }
        EnsureTekNightVisionBlendable(CameraManager);
    }

    bool IsLiveResourceViewport(SDK::UGameViewportClient* ViewportClient,
        int32_t ObjectIndex) {
        if (!ViewportClient || ObjectIndex < 0 ||
            SDK::UObject::GObjects->GetByIndex(ObjectIndex) != ViewportClient) {
            return false;
        }
        SDK::UClass* ViewportClass = SDK::UGameViewportClient::StaticClass();
        return ViewportClass && ViewportClient->Class && ViewportClient->IsA(ViewportClass);
    }

    void RestoreResourceRenderPipeline() {
        if (s_resourceRenderPipelineState.HasViewportState &&
            IsLiveResourceViewport(s_resourceRenderPipelineState.ViewportClient,
                s_resourceRenderPipelineState.ViewportObjectIndex)) {
            constexpr uint8_t PostProcessMask = 1u << 2;
            uint8_t& ShowFlagByte =
                s_resourceRenderPipelineState.ViewportClient->EngineShowFlags[0];
            if (s_resourceRenderPipelineState.PostProcessingEnabled) {
                ShowFlagByte |= PostProcessMask;
            }
            else {
                ShowFlagByte &= static_cast<uint8_t>(~PostProcessMask);
            }
        }

        if (s_resourceRenderPipelineState.HasCustomDepthMode && s_resourceWorld) {
            const std::wstring Command = L"r.CustomDepth " +
                std::to_wstring(s_resourceRenderPipelineState.CustomDepthMode);
            SDK::UKismetSystemLibrary::ExecuteConsoleCommand(
                s_resourceWorld, SDK::FString(Command.c_str()), nullptr);
        }
        if (s_resourceRenderPipelineState.HasNaniteCustomDepthMode && s_resourceWorld) {
            const std::wstring Command = L"r.Nanite.CustomDepth " +
                std::to_wstring(s_resourceRenderPipelineState.NaniteCustomDepthMode);
            SDK::UKismetSystemLibrary::ExecuteConsoleCommand(
                s_resourceWorld, SDK::FString(Command.c_str()), nullptr);
        }

        s_resourceRenderPipelineState = {};
        s_resourceDiagnostics.CustomDepthMode = -1;
        s_resourceDiagnostics.NaniteCustomDepthMode = -1;
        s_resourceDiagnostics.PostProcessingEnabled = false;
        s_resourceDiagnostics.PreRenderWrites = 0;
    }

    void ApplyResourceRenderPipeline(SDK::UWorld* World) {
        if (!World || !g_Config::bResourceModelHighlight ||
            s_resourceHighlightStates.empty()) {
            RestoreResourceRenderPipeline();
            return;
        }

        if (!s_resourceRenderPipelineState.HasCustomDepthMode) {
            s_resourceRenderPipelineState.CustomDepthMode =
                SDK::UKismetSystemLibrary::GetConsoleVariableIntValue(
                    SDK::FString(L"r.CustomDepth"));
            s_resourceRenderPipelineState.HasCustomDepthMode = true;
            if (s_resourceRenderPipelineState.CustomDepthMode != 3) {
                SDK::UKismetSystemLibrary::ExecuteConsoleCommand(
                    World, SDK::FString(L"r.CustomDepth 3"), nullptr);
            }
        }
        s_resourceDiagnostics.CustomDepthMode =
            SDK::UKismetSystemLibrary::GetConsoleVariableIntValue(
                SDK::FString(L"r.CustomDepth"));

        if (!s_resourceRenderPipelineState.HasNaniteCustomDepthMode) {
            s_resourceRenderPipelineState.NaniteCustomDepthMode =
                SDK::UKismetSystemLibrary::GetConsoleVariableIntValue(
                    SDK::FString(L"r.Nanite.CustomDepth"));
            s_resourceRenderPipelineState.HasNaniteCustomDepthMode = true;
            if (s_resourceRenderPipelineState.NaniteCustomDepthMode != 1) {
                SDK::UKismetSystemLibrary::ExecuteConsoleCommand(
                    World, SDK::FString(L"r.Nanite.CustomDepth 1"), nullptr);
            }
        }
        s_resourceDiagnostics.NaniteCustomDepthMode =
            SDK::UKismetSystemLibrary::GetConsoleVariableIntValue(
                SDK::FString(L"r.Nanite.CustomDepth"));

        SDK::UEngine* Engine = SDK::UEngine::GetEngine();
        SDK::UGameViewportClient* ViewportClient = Engine ? Engine->GameViewport : nullptr;
        if (!ViewportClient) {
            s_resourceDiagnostics.PostProcessingEnabled = false;
            return;
        }

        if (!s_resourceRenderPipelineState.HasViewportState ||
            s_resourceRenderPipelineState.ViewportClient != ViewportClient) {
            if (s_resourceRenderPipelineState.HasViewportState) {
                RestoreResourceRenderPipeline();
                ApplyResourceRenderPipeline(World);
                return;
            }
            constexpr uint8_t PostProcessMask = 1u << 2;
            s_resourceRenderPipelineState.ViewportClient = ViewportClient;
            s_resourceRenderPipelineState.ViewportObjectIndex = ViewportClient->Index;
            s_resourceRenderPipelineState.PostProcessingEnabled =
                (ViewportClient->EngineShowFlags[0] & PostProcessMask) != 0;
            s_resourceRenderPipelineState.HasViewportState = true;
        }

        constexpr uint8_t PostProcessMask = 1u << 2;
        ViewportClient->EngineShowFlags[0] |= PostProcessMask;
        s_resourceDiagnostics.PostProcessingEnabled =
            (ViewportClient->EngineShowFlags[0] & PostProcessMask) != 0;
    }

    void ApplyResourceCameraHighlight(SDK::APlayerController* LocalPC) {
        if (!g_Config::bResourceModelHighlight || s_resourceHighlightStates.empty() ||
            !LocalPC || !LocalPC->PlayerCameraManager) {
            RestoreResourceCameraHighlight();
            return;
        }

        SDK::APlayerCameraManager* BaseCameraManager = LocalPC->PlayerCameraManager;
        SDK::UClass* ShooterCameraManagerClass = SDK::AShooterPlayerCameraManager::StaticClass();
        if (!ShooterCameraManagerClass || !BaseCameraManager->Class ||
            !BaseCameraManager->IsA(ShooterCameraManagerClass)) {
            RestoreResourceCameraHighlight();
            return;
        }

        auto* CameraManager = static_cast<SDK::AShooterPlayerCameraManager*>(BaseCameraManager);
        if (s_resourceCameraHighlightState.CameraManager != CameraManager) {
            RestoreResourceCameraHighlight();
            s_resourceCameraHighlightState = {
                CameraManager,
                CameraManager->Index
            };
        }

        // These ShooterPlayerCameraManager flags belong to ARK's Tek helmet
        // scanner pipeline. CustomDepth plus our own outline material is enough
        // for resource highlighting; forcing these flags also activates the
        // helmet overlay and couples two otherwise unrelated features.
        EnsureResourceOutlineBlendable(CameraManager);
        s_resourceDiagnostics.CameraStencilEnabled = CameraManager->bHasCustomDepthStencil;

        const auto SafeCount = [](int Count) {
            return Count >= 0 && Count <= 4096 ? Count : -1;
        };
        s_resourceDiagnostics.CameraPPBlendables =
            SafeCount(CameraManager->PPMaterials.WeightedBlendables.Array.Num());
        s_resourceDiagnostics.CameraPPBlendableCapacity =
            SafeCount(CameraManager->PPMaterials.WeightedBlendables.Array.Max());
        s_resourceDiagnostics.CameraPPAdjusters =
            SafeCount(CameraManager->PPMaterials.PostProcessSettingsMaterialAdjusters.Num());
        s_resourceDiagnostics.CameraBlendCache =
            SafeCount(CameraManager->PostProcessBlendCache.Num());

        s_resourceDiagnostics.CameraPPMaterials.clear();
        const auto& Blendables = CameraManager->PPMaterials.WeightedBlendables.Array;
        const int BlendableCount = SafeCount(Blendables.Num());
        for (int Index = 0; Index >= 0 && Index < BlendableCount && Index < 4; ++Index) {
            const SDK::FWeightedBlendable& Blendable = Blendables[Index];
            SDK::UObject* Object = Blendable.Object;
            std::string Name = "<null>";
            if (Object && Object->Index >= 0 &&
                SDK::UObject::GObjects->GetByIndex(Object->Index) == Object && Object->Class) {
                Name = Object->GetName();
            }

            char WeightText[32]{};
            std::snprintf(WeightText, sizeof(WeightText), "%.2f", Blendable.Weight);
            if (!s_resourceDiagnostics.CameraPPMaterials.empty()) {
                s_resourceDiagnostics.CameraPPMaterials += " | ";
            }
            s_resourceDiagnostics.CameraPPMaterials += Name;
            s_resourceDiagnostics.CameraPPMaterials += "@";
            s_resourceDiagnostics.CameraPPMaterials += WeightText;
        }
    }

    void RestoreAllResourceHighlights() {
        for (const auto& [Component, State] : s_resourceHighlightStates) {
            RestoreResourceHighlight(Component, State);
        }
        s_resourceHighlightStates.clear();
        RestoreResourceCameraHighlight();
        RestoreResourceRenderPipeline();
        s_resourceDiagnostics.HighlightedComponents = 0;
        s_resourceDiagnostics.StencilValue = 0;
    }

    void ApplyResourceHighlights(const std::vector<ResourceMarker>& Markers) {
        std::unordered_set<SDK::UPrimitiveComponent*> DesiredComponents;
        if (g_Config::bResourceModelHighlight) {
            for (const ResourceMarker& Marker : Markers) {
                for (SDK::UPrimitiveComponent* Component : Marker.HighlightComponents) {
                    if (Component) DesiredComponents.insert(Component);
                }
            }
        }

        for (auto Iterator = s_resourceHighlightStates.begin();
            Iterator != s_resourceHighlightStates.end();) {
            if (!DesiredComponents.contains(Iterator->first)) {
                RestoreResourceHighlight(Iterator->first, Iterator->second);
                Iterator = s_resourceHighlightStates.erase(Iterator);
            }
            else {
                ++Iterator;
            }
        }

        if (DesiredComponents.empty()) {
            s_resourceDiagnostics.HighlightedComponents = 0;
            s_resourceDiagnostics.StencilValue = 0;
            return;
        }

        uint8_t ElementStencil = SDK::AShooterPlayerCameraManager::GetColorCodedStencil(
            SDK::EStencilAlliance::Element, 1.0f);
        if (ElementStencil == 0) {
            ElementStencil = static_cast<uint8_t>(SDK::EStencilAlliance::Element);
        }
        s_resourceDiagnostics.StencilValue = ElementStencil;

        for (SDK::UPrimitiveComponent* Component : DesiredComponents) {
            if (!Component || !Component->Class) continue;
            const int32_t ObjectIndex = Component->Index;
            if (!IsLiveResourcePrimitive(Component, ObjectIndex)) continue;

            if (!s_resourceHighlightStates.contains(Component)) {
                s_resourceHighlightStates.emplace(Component, ResourceHighlightState{
                    ObjectIndex,
                    Component->bRenderCustomDepth != 0,
                    Component->CustomDepthStencilValue,
                    Component->CustomDepthStencilWriteMask
                    });
            }

            if (Component->CustomDepthStencilValue != ElementStencil) {
                Component->SetCustomDepthStencilValue(ElementStencil);
            }
            if (Component->CustomDepthStencilWriteMask != SDK::ERendererStencilMask::ERSM_255) {
                Component->SetCustomDepthStencilWriteMask(SDK::ERendererStencilMask::ERSM_255);
            }
            if (!Component->bRenderCustomDepth) {
                Component->SetRenderCustomDepth(true);
            }
        }
        s_resourceDiagnostics.HighlightedComponents =
            static_cast<int>(s_resourceHighlightStates.size());
    }

    std::string BuildInstancedResourceIdentity(SDK::UInstancedStaticMeshComponent* Component) {
        if (!Component || !Component->Class) return {};

        std::string Identity = Component->GetFullName();
        Identity += Component->Class->GetName();
        AppendResourceObjectIdentity(Identity, Component->StaticMesh);
        AppendResourceObjectIdentity(Identity, Component->FoliageTypeReference);
        AppendResourceObjectIdentity(Identity, Component->AttachedComponentClass);
        AppendResourceObjectIdentity(Identity, Component->DestroyedMeshActorClass);
        return Identity;
    }

    void RefreshResourceMarkers(SDK::UWorld* World, SDK::APlayerController* LocalPC,
        const std::chrono::steady_clock::time_point Now) {
        if (!World || !LocalPC || !LocalPC->Pawn) {
            s_resourceMarkers.clear();
            s_resourceDiagnostics = {};
            return;
        }

        if (s_resourceWorld != World) {
            RestoreAllResourceHighlights();
            s_resourceWorld = World;
            s_resourceMarkers.clear();
            s_nextResourceScan = std::chrono::steady_clock::time_point::min();
        }
        if (Now < s_nextResourceScan) return;
        s_nextResourceScan = Now + std::chrono::milliseconds(750);

        if (!g_Config::bResourceMetal && !g_Config::bResourceCrystal &&
            !g_Config::bResourceObsidian && !g_Config::bResourceOil &&
            !g_Config::bResourcePearl && !g_Config::bResourceElementShard &&
            !g_Config::bAutoHarvestTekStriderElementShard) {
            s_resourceMarkers.clear();
            s_resourceDiagnostics = {};
            return;
        }

        ResourceESPDiagnostics Diagnostics{};

        const SDK::FVector PlayerLocation = LocalPC->Pawn->K2_GetActorLocation();
        float RequestedDistance = g_Config::ResourceESPMaxDistance;
        if (g_Config::bAutoHarvestResources) {
            RequestedDistance = std::max(RequestedDistance,
                g_Config::AutoHarvestSearchRadiusMeters + 5.0f);
        }
        const float MaximumDistance = std::clamp(RequestedDistance, 25.0f, 1000.0f);
        const float MaximumDistanceUU = MaximumDistance * 100.0f;
        const float MaximumDistanceSquared = MaximumDistanceUU * MaximumDistanceUU;

        std::vector<ResourceMarker> Candidates;
        Candidates.reserve(512);
        std::unordered_set<SDK::AActor*> VisitedActors;
        std::unordered_set<SDK::UInstancedStaticMeshComponent*> VisitedComponents;

        SDK::UClass* ActorClass = SDK::AActor::StaticClass();
        SDK::UClass* TargetableActorClass = SDK::APrimalTargetableActor::StaticClass();
        SDK::UClass* FoliageActorClass = SDK::AInstancedFoliageActor::StaticClass();
        SDK::UClass* MeshComponentClass = SDK::UMeshComponent::StaticClass();
        SDK::UClass* LevelClass = SDK::ULevel::StaticClass();

        auto AddActorResource = [&](SDK::AActor* Actor) {
            if (!TargetableActorClass || !Actor || !Actor->Class || Actor->IsDefaultObject() ||
                !Actor->IsA(TargetableActorClass)) return;

            SDK::APrimalTargetableActor* TargetableActor =
                static_cast<SDK::APrimalTargetableActor*>(Actor);
            std::string Identity = Actor->GetFullName();
            Identity += Actor->Class->GetName();
            AppendResourceObjectIdentity(Identity, TargetableActor->MyHarvestingComponent);
            const ResourceType HarvestType =
                ClassifyHarvestingComponent(TargetableActor->MyHarvestingComponent);
            ResourceType Type = HarvestType == ResourceType::ElementShard
                ? HarvestType : ClassifyResourceIdentity(Identity);
            if (Type == ResourceType::None) Type = HarvestType;
            if (!IsResourceTypeNeeded(Type)) return;

            SDK::FVector BoundsOrigin = Actor->K2_GetActorLocation();
            SDK::FVector BoundsExtent{};
            Actor->GetActorBounds(false, &BoundsOrigin, &BoundsExtent, true);
            if (!IsFiniteResourceVector(BoundsOrigin)) BoundsOrigin = Actor->K2_GetActorLocation();
            const float DistanceSquared = ResourceDistanceSquared(BoundsOrigin, PlayerLocation);
            if (std::isfinite(DistanceSquared) && DistanceSquared <= MaximumDistanceSquared) {
                ResourceMarker Marker{};
                Marker.Location = BoundsOrigin;
                Marker.Type = Type;
                Marker.DistanceSquared = DistanceSquared;

                if (MeshComponentClass) {
                    SDK::TArray<SDK::UActorComponent*> MeshComponents =
                        Actor->K2_GetComponentsByClass(MeshComponentClass);
                    if (MeshComponents.Num() >= 0 && MeshComponents.Num() <= 4096) {
                        Marker.HighlightComponents.reserve(MeshComponents.Num());
                        for (int32_t MeshIndex = 0; MeshIndex < MeshComponents.Num(); ++MeshIndex) {
                            SDK::UActorComponent* MeshComponent = MeshComponents[MeshIndex];
                            if (MeshComponent && MeshComponent->Class &&
                                MeshComponent->IsA(MeshComponentClass)) {
                                Marker.HighlightComponents.push_back(
                                    static_cast<SDK::UPrimitiveComponent*>(
                                        static_cast<SDK::UMeshComponent*>(MeshComponent)));
                            }
                        }
                    }
                }
                Candidates.push_back(std::move(Marker));
            }
        };

        auto AddFoliageResources = [&](SDK::AActor* Actor) {
            if (!FoliageActorClass || !Actor || !Actor->Class || Actor->IsDefaultObject() ||
                !Actor->IsA(FoliageActorClass)) return;

            SDK::UClass* ComponentClass = SDK::UInstancedStaticMeshComponent::StaticClass();
            if (!ComponentClass) return;
            SDK::TArray<SDK::UActorComponent*> Components =
                Actor->K2_GetComponentsByClass(ComponentClass);
            if (Components.Num() < 0 || Components.Num() > 8192) return;

            for (int32_t ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex) {
                SDK::UActorComponent* ActorComponent = Components[ComponentIndex];
                if (!ActorComponent || !ActorComponent->Class ||
                    !ActorComponent->IsA(ComponentClass)) continue;

                SDK::UInstancedStaticMeshComponent* Component =
                    static_cast<SDK::UInstancedStaticMeshComponent*>(ActorComponent);
                if (!VisitedComponents.insert(Component).second) continue;
                ++Diagnostics.InstanceComponents;

                ResourceType ComponentType =
                    ClassifyResourceIdentity(BuildInstancedResourceIdentity(Component));
                const ResourceType AttachedType =
                    ClassifyHarvestingClass(Component->AttachedComponentClass);
                if (AttachedType == ResourceType::ElementShard ||
                    ComponentType == ResourceType::None) {
                    ComponentType = AttachedType;
                }
                const bool CanResolveFromSample = Component->AttachedComponentClass != nullptr;
                if (!IsResourceTypeNeeded(ComponentType) && !CanResolveFromSample) continue;

                SDK::TArray<int32_t> InstanceIndices = Component->GetInstancesOverlappingSphere(
                    PlayerLocation, MaximumDistanceUU, true);
                if (InstanceIndices.Num() < 0 || InstanceIndices.Num() > 1000000) continue;
                Diagnostics.OverlappingInstances += InstanceIndices.Num();

                // Some foliage Blueprints populate their harvesting data only on the live
                // component. Sample at most a few instances once per ISM component rather
                // than invoking the Blueprint helper for every nearby rock/tree.
                const bool NeedElementShardSample =
                    g_Config::bAutoHarvestTekStriderElementShard &&
                    ComponentType != ResourceType::ElementShard;
                if ((!IsResourceTypeNeeded(ComponentType) || NeedElementShardSample) &&
                    CanResolveFromSample) {
                    const int32_t SampleLimit = std::min(InstanceIndices.Num(), 4);
                    for (int32_t SampleOffset = 0; SampleOffset < SampleLimit; ++SampleOffset) {
                        const int32_t SampleIndex = InstanceIndices[SampleOffset];
                        if (SampleIndex < 0 || !Component->GetInstanceVisibility(SampleIndex)) continue;
                        const ResourceType SampleType = ClassifyHarvestingComponent(
                            SDK::UVictoryCore::GetHarvestingComponentFromHitBodyIndex(
                                Component, SampleIndex));
                        if (SampleType == ResourceType::ElementShard ||
                            (!IsResourceTypeNeeded(ComponentType) &&
                                IsResourceTypeNeeded(SampleType))) {
                            ComponentType = SampleType;
                            break;
                        }
                    }
                }
                if (!IsResourceTypeNeeded(ComponentType)) continue;

                SDK::FBoxSphereBounds LocalBounds{};
                const bool HasLocalBounds = Component->StaticMesh && Component->StaticMesh->Class;
                if (HasLocalBounds) LocalBounds = Component->StaticMesh->GetBounds();

                const int32_t ComponentLimit = std::min(InstanceIndices.Num(), 96);
                for (int32_t InstanceOffset = 0; InstanceOffset < ComponentLimit; ++InstanceOffset) {
                    const int32_t InstanceIndex = InstanceIndices[InstanceOffset];
                    if (InstanceIndex < 0 || !Component->GetInstanceVisibility(InstanceIndex)) continue;

                    SDK::FTransform Transform{};
                    if (!Component->GetInstanceTransform(InstanceIndex, &Transform, true)) continue;
                    SDK::FVector ResourceCenter = Transform.Translation;
                    if (HasLocalBounds && IsFiniteResourceVector(LocalBounds.Origin)) {
                        ResourceCenter = TransformResourcePosition(Transform, LocalBounds.Origin);
                    }
                    const float DistanceSquared = ResourceDistanceSquared(ResourceCenter, PlayerLocation);
                    if (!std::isfinite(DistanceSquared) || DistanceSquared > MaximumDistanceSquared) continue;

                    ResourceMarker Marker{};
                    Marker.Location = ResourceCenter;
                    Marker.Type = ComponentType;
                    Marker.DistanceSquared = DistanceSquared;
                    Marker.HighlightComponents.push_back(Component);
                    Candidates.push_back(std::move(Marker));
                    if (Candidates.size() >= 768) break;
                }
                if (Candidates.size() >= 768) break;
            }
        };

        auto VisitLevel = [&](SDK::ULevel* Level) {
            if (!Level) return;
            const int32_t ActorCount = Level->Actors.Num();
            if (ActorCount < 0 || ActorCount > 200000) return;
            ++Diagnostics.Levels;
            Diagnostics.Actors += ActorCount;

            for (int32_t ActorIndex = 0; ActorIndex < ActorCount; ++ActorIndex) {
                SDK::AActor* Actor = Level->Actors[ActorIndex];
                if (!ActorClass || !Actor || !Actor->Class || !Actor->IsA(ActorClass)) continue;
                if (!VisitedActors.insert(Actor).second) continue;
                AddActorResource(Actor);
                AddFoliageResources(Actor);
                if (Candidates.size() >= 768) break;
            }
        };

        const int32_t LevelCount = World->Levels.Num();
        if (LevelCount >= 0 && LevelCount <= 4096) {
            for (int32_t LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex) {
                SDK::ULevel* Level = World->Levels[LevelIndex];
                if (!LevelClass || !Level || !Level->Class || !Level->IsA(LevelClass)) continue;
                VisitLevel(Level);
                if (Candidates.size() >= 768) break;
            }
        }
        if (LevelClass && World->PersistentLevel && World->PersistentLevel->Class &&
            World->PersistentLevel->IsA(LevelClass)) {
            VisitLevel(World->PersistentLevel);
        }

        std::sort(Candidates.begin(), Candidates.end(), [](const ResourceMarker& Left,
            const ResourceMarker& Right) {
                return Left.DistanceSquared < Right.DistanceSquared;
            });
        if (Candidates.size() > 192) Candidates.resize(192);
        s_resourceMarkers = std::move(Candidates);
        Diagnostics.Markers = static_cast<int>(s_resourceMarkers.size());
        s_resourceDiagnostics = Diagnostics;
    }

    ResourceESPDiagnostics GetResourceESPDiagnostics() {
        return s_resourceDiagnostics;
    }

    bool FindNearestResourceTarget(const SDK::FVector& Origin, float MaxDistanceMeters,
        SDK::FVector* OutLocation, float* OutDistanceMeters, std::string* OutTypeName,
        const std::vector<SDK::FVector>* ExcludedLocations, float ExclusionRadiusMeters,
        bool ElementShardOnly) {
        if (!OutLocation || !IsFiniteResourceVector(Origin) || s_resourceMarkers.empty()) {
            return false;
        }

        const double MaximumUU = std::clamp(static_cast<double>(MaxDistanceMeters), 1.0, 1000.0) * 100.0;
        const double MaximumSquared = MaximumUU * MaximumUU;
        const double ExclusionUU = std::max(0.0, static_cast<double>(ExclusionRadiusMeters)) * 100.0;
        const double ExclusionSquared = ExclusionUU * ExclusionUU;
        const ResourceMarker* Best = nullptr;
        double BestDistanceSquared = MaximumSquared;

        for (const ResourceMarker& Marker : s_resourceMarkers) {
            if (!IsFiniteResourceVector(Marker.Location) ||
                (ElementShardOnly
                    ? Marker.Type != ResourceType::ElementShard
                    : !IsResourceTypeEnabled(Marker.Type))) continue;
            bool Excluded = false;
            if (ExcludedLocations && ExclusionSquared > 0.0) {
                for (const SDK::FVector& ExcludedLocation : *ExcludedLocations) {
                    if (IsFiniteResourceVector(ExcludedLocation) &&
                        ResourceDistanceSquared(Marker.Location, ExcludedLocation) <= ExclusionSquared) {
                        Excluded = true;
                        break;
                    }
                }
            }
            if (Excluded) continue;
            const double DistanceSquared = ResourceDistanceSquared(Marker.Location, Origin);
            if (!std::isfinite(DistanceSquared) || DistanceSquared > BestDistanceSquared) continue;
            BestDistanceSquared = DistanceSquared;
            Best = &Marker;
        }

        if (!Best) return false;
        *OutLocation = Best->Location;
        if (OutDistanceMeters) {
            *OutDistanceMeters = static_cast<float>(std::sqrt(BestDistanceSquared) * 0.01);
        }
        if (OutTypeName) *OutTypeName = GetResourceTypeLabel(Best->Type);
        return true;
    }

    bool FindResourceTargetNear(const SDK::FVector& ExpectedLocation, float ToleranceMeters,
        SDK::FVector* OutLocation, std::string* OutTypeName, bool ElementShardOnly) {
        if (!OutLocation || !IsFiniteResourceVector(ExpectedLocation) || s_resourceMarkers.empty()) {
            return false;
        }

        const double ToleranceUU = std::clamp(static_cast<double>(ToleranceMeters), 0.1, 10.0) * 100.0;
        const double ToleranceSquared = ToleranceUU * ToleranceUU;
        const ResourceMarker* Best = nullptr;
        double BestDistanceSquared = ToleranceSquared;
        for (const ResourceMarker& Marker : s_resourceMarkers) {
            if (!IsFiniteResourceVector(Marker.Location) ||
                (ElementShardOnly
                    ? Marker.Type != ResourceType::ElementShard
                    : !IsResourceTypeEnabled(Marker.Type))) continue;
            const double DistanceSquared = ResourceDistanceSquared(Marker.Location, ExpectedLocation);
            if (!std::isfinite(DistanceSquared) || DistanceSquared > BestDistanceSquared) continue;
            BestDistanceSquared = DistanceSquared;
            Best = &Marker;
        }
        if (!Best) return false;
        *OutLocation = Best->Location;
        if (OutTypeName) *OutTypeName = GetResourceTypeLabel(Best->Type);
        return true;
    }

    void ResetResourceModelHighlights() {
        RestoreAllResourceHighlights();
    }

    void ResetTekHelmetNightVision() {
        RestoreTekNightVision();
        s_tekNightVisionStatus = "已关闭";
    }

    std::string GetTekHelmetNightVisionStatus() {
        return s_tekNightVisionStatus;
    }

    void MaintainResourceModelHighlightState() {
        if (!g_Config::bResourceModelHighlight || s_resourceHighlightStates.empty()) return;

        if (IsLiveResourceCamera(s_resourceCameraHighlightState.CameraManager,
            s_resourceCameraHighlightState.ObjectIndex)) {
            s_resourceDiagnostics.CameraStencilEnabled =
                s_resourceCameraHighlightState.CameraManager->bHasCustomDepthStencil;
        }

        if (s_resourceRenderPipelineState.HasViewportState &&
            IsLiveResourceViewport(s_resourceRenderPipelineState.ViewportClient,
                s_resourceRenderPipelineState.ViewportObjectIndex)) {
            constexpr uint8_t PostProcessMask = 1u << 2;
            s_resourceRenderPipelineState.ViewportClient->EngineShowFlags[0] |= PostProcessMask;
            s_resourceDiagnostics.PostProcessingEnabled = true;
        }

        if (s_resourceDiagnostics.PreRenderWrites < 999999) {
            ++s_resourceDiagnostics.PreRenderWrites;
        }
    }

    void DrawResourceESP(SDK::UCanvas* Canvas) {
        if (Canvas) {
            MaintainTekNightVision(_DD::LocalSPC);
        }
        else {
            RestoreTekNightVision();
        }

        // Resource ESP is independent of the player/dino ESP master switch.
        const bool DrawResourceVisuals = g_Config::bResourceESPEnabled;
        const bool NeedResourceScan = DrawResourceVisuals || g_Config::bAutoHarvestResources;
        if (!Canvas || !NeedResourceScan) {
            RestoreAllResourceHighlights();
            s_resourceMarkers.clear();
            s_resourceWorld = nullptr;
            s_resourceDiagnostics = {};
            return;
        }

        SDK::UWorld* World = _DD::World;
        SDK::APlayerController* LocalPC = _DD::LocalSPC;
        if (!World || !LocalPC || !LocalPC->Pawn) {
            RestoreAllResourceHighlights();
            s_resourceMarkers.clear();
            return;
        }

        RefreshResourceMarkers(World, LocalPC, std::chrono::steady_clock::now());
        if (DrawResourceVisuals) {
            ApplyResourceHighlights(s_resourceMarkers);
            ApplyResourceCameraHighlight(LocalPC);
            ApplyResourceRenderPipeline(World);
        }
        else {
            RestoreAllResourceHighlights();
        }
        if (!DrawResourceVisuals || !g_Config::bResourceShowLabels) return;

        Shadow::ShadowIO& IO = Shadow::GetIO();
        Shadow::ShadowDrawList* DrawList = Shadow::GetBackgroundDrawList();
        if (!DrawList) return;

        for (const ResourceMarker& Marker : s_resourceMarkers) {
            const Shadow::Color Color = GetResourceTypeColor(Marker.Type);
            SDK::FVector2D ScreenPosition{};
            if (!LocalPC->ProjectWorldLocationToScreen(Marker.Location, &ScreenPosition, false)) continue;
            const float ScreenX = static_cast<float>(ScreenPosition.X);
            const float ScreenY = static_cast<float>(ScreenPosition.Y);
            if (ScreenX < 0.0f || ScreenX >= IO.DisplaySize.x ||
                ScreenY < 0.0f || ScreenY >= IO.DisplaySize.y) continue;

            const Shadow::Color BackColor{ 0.0f, 0.0f, 0.0f, Color.a * 0.85f };
            DrawList->AddLine({ ScreenX - 5.0f, ScreenY }, { ScreenX + 5.0f, ScreenY },
                BackColor, 4.0f);
            DrawList->AddLine({ ScreenX, ScreenY - 5.0f }, { ScreenX, ScreenY + 5.0f },
                BackColor, 4.0f);
            DrawList->AddLine({ ScreenX - 5.0f, ScreenY }, { ScreenX + 5.0f, ScreenY },
                Color, 2.0f);
            DrawList->AddLine({ ScreenX, ScreenY - 5.0f }, { ScreenX, ScreenY + 5.0f },
                Color, 2.0f);

            g_ESP::BoxRect Rect;
            Rect.topLeft = Shadow::Vec2{ ScreenX - 5.0f, ScreenY - 5.0f };
            Rect.bottomRight = Shadow::Vec2{ ScreenX + 5.0f, ScreenY + 5.0f };
            Rect.valid = true;

            const float DistanceMeters = std::sqrt(Marker.DistanceSquared) * 0.01f;
            std::string Label = "[";
            Label += GetResourceTypeLabel(Marker.Type);
            Label += "] (" + std::to_string(static_cast<int>(DistanceMeters)) + "m)";

            g_ESP::FlagManager Flags;
            Flags.Reset();
            Flags.AddFlag(Canvas, Rect, Label, Color, g_ESP::FlagPos::Right, 1.0f, nullptr);
        }
    }

    void DrawESP(SDK::UCanvas* Canvas)
    {
        if (!Canvas) return;

        switch (g_Config::ESPScaleIdx)
        {
        case 0:
            g_Config::ESPScale = 0.75f;
            break;
        case 1:
            g_Config::ESPScale = 1.00f;
            break;
        case 2:
            g_Config::ESPScale = 1.25f;
            break;
        case 3:
            g_Config::ESPScale = 1.5f;
            break;
        case 4:
            g_Config::ESPScale = 2.0f;
            break;
        }

        if (!g_Config::bESPEnabled) {
            s_entries.clear();
            waterCandidates.clear();
            return;
        }

        SDK::UWorld* World = _DD::World;
        if (!World || !World->GameState) return;

        SDK::APlayerController* LocalPC = _DD::LocalSPC;
        if (!LocalPC || !LocalPC->Pawn) {
            for (auto& kv : s_entries) {
                kv.second.targetAlpha = 0.0f;
                kv.second.aliveThisFrame = false;
            }
            return;
        }

        SDK::AShooterPlayerState* LocalPS = _DD::LocalSPS;
        if (!LocalPS) {
            for (auto& kv : s_entries) {
                kv.second.targetAlpha = 0.0f;
                kv.second.aliveThisFrame = false;
            }
            return;
        }

        Shadow::ShadowIO& IO = Shadow::GetIO();

        const float screenW = IO.DisplaySize.x;
        const float screenH = IO.DisplaySize.y;
        const float deltaTime = IO.DeltaTime;

        SDK::APrimalCharacter* LocalChar = _DD::LocalSC;
        if (!LocalChar && LocalPC->Pawn && LocalPC->Pawn->Class &&
            LocalPC->Pawn->IsA(SDK::APrimalCharacter::StaticClass())) {
            LocalChar = static_cast<SDK::APrimalCharacter*>(LocalPC->Pawn);
        }

        // ASA streams creatures and players across multiple ULevels. Looking
        // only at PersistentLevel misses actors living in streamed sub-levels.
        static std::vector<SDK::AActor*> Actors;
        Actors.clear();
        const int32_t PersistentActorCount = World->PersistentLevel
            ? World->PersistentLevel->Actors.Num() : 0;
        if (PersistentActorCount > 0 && PersistentActorCount <= 200000) {
            Actors.reserve(static_cast<size_t>(PersistentActorCount));
        }
        auto AppendLevelActors = [&](SDK::ULevel* Level) {
            if (!Level) return;
            const int32_t Count = Level->Actors.Num();
            if (Count < 0 || Count > 200000) return;
            for (int32_t ActorIndex = 0; ActorIndex < Count; ++ActorIndex) {
                SDK::AActor* Actor = Level->Actors[ActorIndex];
                if (Actor) Actors.push_back(Actor);
            }
        };

        const int32_t LevelCount = World->Levels.Num();
        bool IncludedPersistentLevel = false;
        if (LevelCount >= 0 && LevelCount <= 4096) {
            for (int32_t LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex) {
                SDK::ULevel* Level = World->Levels[LevelIndex];
                IncludedPersistentLevel |= Level == World->PersistentLevel;
                AppendLevelActors(Level);
            }
        }
        if (!IncludedPersistentLevel) AppendLevelActors(World->PersistentLevel);

        waterCandidates.clear();

        const char* rawEntityFilter = g_Config::entitySearchBuf;
        const bool  hasEntityFilter = g_Config::bEnableFilter && rawEntityFilter[0] != '\0';
        const char* rawStructFilter = g_Config::structureSearchBuf;
        const bool  hasStructureFilter = g_Config::bEnableStructureFilter && rawStructFilter[0] != '\0';

        for (auto& kv : s_entries)
            kv.second.aliveThisFrame = false;

        for (SDK::AActor* TargetActor : Actors) {
            if (!TargetActor || TargetActor == LocalPC->Pawn || TargetActor == _DD::LocalSC) continue;

            const ActorType CurrentActorType = ClassifyESPActor(TargetActor);
            if (CurrentActorType == ActorType::Unknown) continue;

            uintptr_t  key = reinterpret_cast<uintptr_t>(TargetActor);
            ESPEntry& entry = s_entries[key];

            // UObject addresses can be reused after destruction. Reset cached
            // type/render data when the object index no longer matches.
            if (entry.objectIndex != TargetActor->Index) {
                entry = ESPEntry{};
            }

            entry.actorKey = key;
            entry.objectIndex = TargetActor->Index;
            entry.actorType = CurrentActorType;
            entry.lastSeenTime = 0.0f;

            if (TargetActor->bHidden) {
                entry.targetAlpha = 0.0f;
                entry.aliveThisFrame = false;
                continue;
            }

            const SDK::FVector actorLoc = TargetActor->K2_GetActorLocation();
            entry.lastWorldLoc = actorLoc;
            entry.aliveThisFrame = true;

            // ============================================================
            // Branch: PrimalCharacter (包含了 ShooterCharacter, PrimalDinoCharacter 以及其它派生)
            // ============================================================
            if (entry.actorType == ActorType::ShooterCharacter || entry.actorType == ActorType::PrimalDinoCharacter || entry.actorType == ActorType::PrimalCharacter) {
                SDK::APrimalCharacter* TargetChar = static_cast<SDK::APrimalCharacter*>(TargetActor);
                SDK::APlayerState* TargetPS = TargetChar->PlayerState;

                const float dist = (LocalPC && LocalPC->Pawn && TargetActor) ? LocalPC->Pawn->GetDistanceTo(TargetActor) * 0.01f : 0.0f;
                const bool isDead = TargetChar->IsDead();

                // 将生物类型判定作为一个布尔值，用于区分全局ESP应用规则
                // Non-player APrimalCharacter subclasses are creature/NPC
                // fallbacks and should obey the dino display settings.
                const bool isDino = (entry.actorType != ActorType::ShooterCharacter);

                if (isDead) {
                    g_ESP::RelationType relation = g_ESP::GetRelation(TargetChar, LocalChar);

                    bool   bShowRagdoll = false;
                    float* RagdollCol = nullptr;
                    float* DistCol = nullptr;

                    if (isDino) {
                        if (relation == g_ESP::RelationType::Team) {
                            bShowRagdoll = g_Config::bTeamDinoDrawRagdoll;
                            RagdollCol = g_Config::TeamDinoRagdollColor;
                            DistCol = g_Config::TeamDinoDistanceColor;
                        }
                        else {
                            bShowRagdoll = g_Config::bDinoDrawRagdoll;
                            RagdollCol = g_Config::DinoRagdollColor;
                            DistCol = g_Config::DinoDistanceColor;
                        }
                    }
                    else {
                        // ShooterCharacter (Player) 逻辑保持
                        if (relation == g_ESP::RelationType::Team) {
                            if (!g_Config::bESPTeamEnabled) {
                                entry.targetAlpha = 0.0f;
                                entry.aliveThisFrame = false;
                                continue;
                            }
                            bShowRagdoll = g_Config::bDrawRagdollTeam;
                            RagdollCol = g_Config::RagdollColorTeam;
                            DistCol = g_Config::RagdollColorTeam;
                        }
                        else {
                            bShowRagdoll = g_Config::bDrawRagdoll;
                            RagdollCol = g_Config::RagdollColor;
                            DistCol = g_Config::RagdollColor;
                        }
                    }

                    if (!bShowRagdoll) {
                        entry.targetAlpha = 0.0f;
                        entry.aliveThisFrame = false;
                        continue;
                    }

                    g_ESP::BoxRect rect = g_ESP::GetBox(Canvas, TargetActor, 0.5f, false);

                    if (!rect.valid) {
                        entry.targetAlpha = 0.0f;
                        continue;
                    }

                    entry.cachedRect = rect;
                    entry.boxColor = Shadow::Color{ RagdollCol[0], RagdollCol[1], RagdollCol[2], RagdollCol[3] };
                    entry.distanceColor = Shadow::Color{ DistCol[0], DistCol[1], DistCol[2], DistCol[3] };
                    entry.configBoxAlpha = RagdollCol[3];
                    entry.targetAlpha = 1.0f;
                    entry.aliveThisFrame = true;

                    if (isDino) {
                        if (relation == g_ESP::RelationType::Team) {
                            entry.shouldDrawBox = g_Config::bTeamDinoDrawBox;
                            entry.shouldDrawName = g_Config::bTeamDinoDrawName;
                            entry.shouldDrawDistance = g_Config::bTeamDinoDrawDistance;
                        }
                        else {
                            entry.shouldDrawBox = g_Config::bDinoDrawBox;
                            entry.shouldDrawName = g_Config::bDinoDrawName;
                            entry.shouldDrawDistance = g_Config::bDinoDrawDistance;
                        }
                    }
                    else {
                        if (relation == g_ESP::RelationType::Team) {
                            entry.shouldDrawBox = g_Config::bDrawBoxTeam;
                            entry.shouldDrawName = g_Config::bDrawNameTeam;
                            entry.shouldDrawDistance = g_Config::bDrawDistanceTeam;
                        }
                        else {
                            entry.shouldDrawBox = g_Config::bDrawBox;
                            entry.shouldDrawName = g_Config::bDrawName;
                            entry.shouldDrawDistance = g_Config::bDrawDistance;
                        }
                    }

                    entry.shouldDrawHealthBar = false;
                    entry.shouldDrawTorpor = false;
                    entry.flags.clear();
                    entry.bars.clear();

                    std::string deadLabel;
                    if (entry.shouldDrawName) {
                        deadLabel = TargetPS
                            ? TargetPS->GetPlayerName().ToString()
                            : TargetChar->GetDescriptiveName().ToString();
                    }
                    if (entry.shouldDrawDistance) {
                        if (!deadLabel.empty()) deadLabel += "  ";
                        deadLabel += "[" + std::to_string((int)dist) + "m]";
                    }
                    if (!deadLabel.empty()) {
                        entry.flags.push_back({ std::move(deadLabel),
                            entry.shouldDrawName ? entry.boxColor : entry.distanceColor,
                            g_ESP::FlagPos::Bottom });
                    }
                    continue;
                }

                if (hasEntityFilter) {
                    std::string nameForESP = TargetPS
                        ? TargetPS->GetPlayerName().ToString()
                        : TargetChar->GetDescriptiveName().ToString();
                    if (!g_Util::IsEntityMatchMulti(nameForESP, rawEntityFilter)) {
                        entry.targetAlpha = 0.0f;
                        entry.aliveThisFrame = false;
                        continue;
                    }
                }

                g_ESP::RelationType relation = g_ESP::GetRelation(TargetChar, LocalChar);

                bool   bDrawBox = false, bDrawHealthBar = false, bDrawName = false;
                bool   bDrawGrowth = false, bDrawDistance = false, bDrawTorpor = false;
                float* BoxColor = nullptr;
                float* NameColor = nullptr;
                float* DistanceColor = nullptr;
                float* TorporColor = nullptr;

                if (isDino) {
                    if (relation == g_ESP::RelationType::Team) {
                        bDrawBox = g_Config::bTeamDinoDrawBox;
                        BoxColor = g_Config::TeamDinoBoxColor;
                        bDrawHealthBar = g_Config::bTeamDinoDrawHealthBar;
                        bDrawName = g_Config::bTeamDinoDrawName;
                        NameColor = g_Config::TeamDinoNameColor;
                        bDrawDistance = g_Config::bTeamDinoDrawDistance;
                        DistanceColor = g_Config::TeamDinoDistanceColor;
                        bDrawTorpor = g_Config::bTeamDinoDrawTorpor;
                        TorporColor = g_Config::TeamDinoTorporColor;
                        bDrawGrowth = false; // 生物通常不应用玩家类型的Growth特征
                    }
                    else {
                        bDrawBox = g_Config::bDinoDrawBox;
                        BoxColor = g_Config::DinoBoxColor;
                        bDrawHealthBar = g_Config::bDinoDrawHealthBar;
                        bDrawName = g_Config::bDinoDrawName;
                        NameColor = g_Config::DinoNameColor;
                        bDrawDistance = g_Config::bDinoDrawDistance;
                        DistanceColor = g_Config::DinoDistanceColor;
                        bDrawTorpor = g_Config::bDinoDrawTorpor;
                        TorporColor = g_Config::DinoTorporColor;
                        bDrawGrowth = false;
                    }
                }
                else {
                    if (relation == g_ESP::RelationType::Team) {
                        if (!g_Config::bESPTeamEnabled) {
                            entry.targetAlpha = 0.0f;
                            entry.aliveThisFrame = false;
                            continue;
                        }
                        bDrawBox = g_Config::bDrawBoxTeam;
                        BoxColor = g_Config::BoxColorTeam;
                        bDrawHealthBar = g_Config::bDrawHealthBarTeam;
                        bDrawName = g_Config::bDrawNameTeam;
                        NameColor = g_Config::NameColorTeam;
                        bDrawGrowth = g_Config::bDrawGrowthTeam;
                        bDrawDistance = g_Config::bDrawDistanceTeam;
                        DistanceColor = g_Config::DistanceColorTeam;
                        bDrawTorpor = g_Config::bDrawTorporTeam;
                        TorporColor = g_Config::TorporColorTeam;
                    }
                    else {
                        bDrawBox = g_Config::bDrawBox;
                        BoxColor = g_Config::BoxColor;
                        bDrawHealthBar = g_Config::bDrawHealthBar;
                        bDrawName = g_Config::bDrawName;
                        NameColor = g_Config::NameColor;
                        bDrawGrowth = g_Config::bDrawGrowth;
                        bDrawDistance = g_Config::bDrawDistance;
                        DistanceColor = g_Config::DistanceColor;
                        bDrawTorpor = g_Config::bDrawTorpor;
                        TorporColor = g_Config::TorporColor;
                    }
                }

                g_ESP::BoxRect rect = g_ESP::GetBox(Canvas, TargetActor, 0.5f, true);

                entry.cachedRect = rect;
                entry.boxColor = Shadow::Color{ BoxColor[0], BoxColor[1], BoxColor[2], BoxColor[3] };
                entry.nameColor = Shadow::Color{ NameColor[0], NameColor[1], NameColor[2], NameColor[3] };
                entry.distanceColor = Shadow::Color{ DistanceColor[0], DistanceColor[1], DistanceColor[2], DistanceColor[3] };
                entry.configBoxAlpha = BoxColor[3];
                entry.isItem = false;
                entry.isOOF = false;
                entry.cachedHP = TargetChar->GetHealth();
                entry.cachedMaxHP = TargetChar->GetMaxHealth();

                SDK::UPrimalCharacterStatusComponent* StatusComp = TargetChar->GetCharacterStatusComponent();
                if (StatusComp) {
                    entry.cachedTorpor = StatusComp->CurrentStatusValues[(int)SDK::EPrimalCharacterStatusValue::Torpidity];
                    entry.cachedMaxTorpor = StatusComp->MaxStatusValues[(int)SDK::EPrimalCharacterStatusValue::Torpidity];
                }
                else {
                    entry.cachedTorpor = 0.0f;
                    entry.cachedMaxTorpor = 0.0f;
                }

                entry.shouldDrawBox = bDrawBox;
                entry.shouldDrawHealthBar = bDrawHealthBar;
                entry.shouldDrawName = bDrawName;
                entry.shouldDrawDistance = bDrawDistance;
                entry.shouldDrawTorpor = bDrawTorpor;

                if (bDrawName) {
                    const char* genderSuffix = TargetActor->IsFemale() ? "-F" : "-M";
                    entry.name = TargetPS
                        ? TargetPS->GetPlayerName().ToString() + genderSuffix
                        : TargetChar->GetDescriptiveName().ToString() + genderSuffix;
                }
                else {
                    entry.name.clear();
                }

                entry.flags.clear();
                entry.bars.clear();

                std::string primaryLabel = bDrawName ? entry.name : std::string{};
                if (bDrawDistance) {
                    if (!primaryLabel.empty()) primaryLabel += "  ";
                    primaryLabel += "[" + std::to_string((int)dist) + "m]";
                }
                if (!primaryLabel.empty()) {
                    entry.flags.push_back({ std::move(primaryLabel),
                        bDrawName ? entry.nameColor : entry.distanceColor,
                        g_ESP::FlagPos::Bottom });
                }

                if (bDrawHealthBar) {
                    const float healthPct = (entry.cachedMaxHP > 0.0f) ? (entry.cachedHP / entry.cachedMaxHP) : 0.0f;
                    float* colMax = (relation == g_ESP::RelationType::Team) ? g_Config::TeamHealthColor1 : g_Config::HealthBarColor1;
                    float* colMin = (relation == g_ESP::RelationType::Team) ? g_Config::TeamHealthColor2 : g_Config::HealthBarColor2;

                    const Shadow::Color hpCol = GetHealthColorLinear(healthPct, colMax, colMin);

                    entry.flags.push_back({ std::to_string((int)entry.cachedHP), hpCol, g_ESP::FlagPos::Left });
                    entry.bars.push_back({ entry.cachedHP, entry.cachedMaxHP, hpCol, g_ESP::BarPos::Left, g_ESP::BarOrientation::Vertical });
                }

                if (bDrawTorpor && entry.cachedMaxTorpor > 0.0f) {
                    const Shadow::Color torporCol = Shadow::Color{ TorporColor[0], TorporColor[1], TorporColor[2], TorporColor[3] };
                    entry.flags.push_back({
                        std::to_string((int)entry.cachedTorpor) + "/" + std::to_string((int)entry.cachedMaxTorpor),
                        torporCol, g_ESP::FlagPos::Bottom
                        });
                    entry.bars.push_back({ entry.cachedTorpor, entry.cachedMaxTorpor, torporCol, g_ESP::BarPos::Bottom, g_ESP::BarOrientation::Horizontal });
                }

                const bool onScreen = rect.valid && rect.bottomRight.x > 0.0f &&
                    rect.topLeft.x < screenW && rect.bottomRight.y > 0.0f &&
                    rect.topLeft.y < screenH;
                if (onScreen) {
                    entry.targetAlpha = 1.0f;
                    entry.isOOF = false;
                }
                else {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                }
            }

            // ============================================================
            // Branch: WaterVolume
            // ============================================================
            else if (entry.actorType == ActorType::PhysicsVolume && g_Config::bDrawWater) {
                SDK::APhysicsVolume* PV = static_cast<SDK::APhysicsVolume*>(TargetActor);
                if (!PV->bWaterVolume && !PV->bDynamicWaterVolume) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }

                SDK::FVector Origin, Extend;
                TargetActor->GetActorBounds(false, &Origin, &Extend, false);
                const SDK::FVector WaterSurfaceLoc = { Origin.X, Origin.Y, Origin.Z + Extend.Z };
                const float dist = (LocalPC && LocalPC->Pawn && TargetActor) ? LocalPC->Pawn->GetDistanceTo(TargetActor) * 0.01f : 0.0f;

                entry.aliveThisFrame = true;
                entry.targetAlpha = 1.0f;

                waterCandidates.push_back({ TargetActor, dist, SDK::FVector2D{0,0}, WaterSurfaceLoc });
            }

            // ============================================================
            // Branch: DroppedItem
            // ============================================================
            else if (entry.actorType == ActorType::DroppedItem && g_Config::bDrawDroppedItems) {
                SDK::ADroppedItem* DroppedItem = static_cast<SDK::ADroppedItem*>(TargetActor);
                SDK::UPrimalItem* Item = DroppedItem ? DroppedItem->MyItem : nullptr;

                if (!Item) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }

                const float dist = (LocalPC && LocalPC->Pawn && TargetActor) ? LocalPC->Pawn->GetDistanceTo(TargetActor) * 0.01f : 0.0f;
                if (dist > g_Config::DroppedItemMaxDistance) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }

                // 进行新加入的物品类别过滤拦截
                const std::string className = Item->Class ? Item->Class->GetName() : "";
                const int quantity = Item->ItemQuantity;

                if (!g_Util::IsDroppedItemAllowed(className, quantity)) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }

                SDK::FVector2D screenPos;
                bool bIsProjected = LocalPC && LocalPC->ProjectWorldLocationToScreen(actorLoc, &screenPos, false);
                bool bOnScreen = bIsProjected && (screenPos.X > 0 && screenPos.X < screenW && screenPos.Y > 0 && screenPos.Y < screenH);

                if (bIsProjected) {
                    entry.cachedRect.topLeft = Shadow::Vec2{ (float)(screenPos.X - 5), (float)(screenPos.Y - 5) };
                    entry.cachedRect.bottomRight = Shadow::Vec2{ (float)(screenPos.X + 5), (float)(screenPos.Y + 5) };
                    entry.cachedRect.valid = true;
                }

                if (!bOnScreen) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                }
                else {
                    entry.targetAlpha = 1.0f;
                    entry.aliveThisFrame = true;
                    entry.isItem = true;
                }

                std::string itemName;
                if (Item->CustomItemName.IsValid() && !Item->CustomItemName.ToString().empty()) {
                    itemName = Item->CustomItemName.ToString();
                }
                else if (Item->DescriptiveNameBase.IsValid()) {
                    itemName = Item->DescriptiveNameBase.ToString();
                }
                else {
                    itemName = Item->Class ? Item->Class->GetName() : "Unknown Item";
                }

                const Shadow::Color finalCol = U32ToFLinearColor(g_Util::ResolveDroppedItemColor(className, Item->ItemRating, quantity));

                entry.flags.clear();
                entry.bars.clear();

                std::string label = "[" + itemName + "";
                if (quantity > 1) label += " x" + std::to_string(quantity);
                if (Item->bIsBlueprint) label = "[BP] " + label;

                entry.flags.push_back({ std::move(label) + "] (" + std::to_string((int)dist) + "m" + ")", finalCol, g_ESP::FlagPos::Right });

                entry.boxColor = finalCol;
                entry.nameColor = Shadow::Color{ g_Config::DroppedItemNameColor[0], g_Config::DroppedItemNameColor[1], g_Config::DroppedItemNameColor[2], g_Config::DroppedItemNameColor[3] };
                entry.shouldDrawBox = false;
                entry.shouldDrawHealthBar = false;
                entry.shouldDrawName = false;
                entry.shouldDrawDistance = true;
                entry.shouldDrawTorpor = false;
            }

            // ============================================================
            // Branch: PrimalStructure
            // ============================================================
            else if (entry.actorType == ActorType::PrimalStructure) {
                // 如果两项结构开关都没开，则直接跳过拦截
                if (!g_Config::bDrawStructures && !g_Config::bTeamDrawStructures) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }

                SDK::APrimalStructure* Structure = static_cast<SDK::APrimalStructure*>(TargetActor);

                if (Structure->Health <= 0.0f) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }

                // Team ownership is represented by TargetingTeam. OwnerName is
                // only a display string and may contain the player rather than
                // the tribe, so it is used solely as a legacy fallback.
                bool isTeam = false;
                if (LocalChar) {
                    const int32_t LocalTeam = LocalChar->TargetingTeam;
                    const int32_t StructureTeam = Structure->TargetingTeam;
                    isTeam = LocalTeam != 0 && StructureTeam != 0 &&
                        LocalTeam == StructureTeam;
                    if (!isTeam && (LocalTeam == 0 || StructureTeam == 0)) {
                        const std::string LocalTribe = LocalChar->TribeName.ToString();
                        const std::string OwnerName = Structure->OwnerName.ToString();
                        isTeam = !LocalTribe.empty() && LocalTribe != "None" &&
                            LocalTribe == OwnerName;
                    }
                }
                if (isTeam && !g_Config::bTeamDrawStructures) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }
                if (!isTeam && !g_Config::bDrawStructures) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }

                if (hasStructureFilter) {
                    std::string sName = Structure->GetDescriptiveName().ToString();
                    if (sName.empty() || sName == "None") sName = "Structure";
                    if (!g_Util::IsStructureMatchMulti(sName, rawStructFilter)) {
                        entry.targetAlpha = 0.0f;
                        entry.aliveThisFrame = false;
                        continue;
                    }
                }

                // 获取并使用不同的距离过滤
                float maxDist = isTeam ? g_Config::TeamStructureMaxDistance : g_Config::StructureMaxDistance;
                const float dist = (LocalPC && LocalPC->Pawn && TargetActor) ? LocalPC->Pawn->GetDistanceTo(TargetActor) * 0.01f : 0.0f;
                if (dist > maxDist) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }

                SDK::FVector2D screenPos;
                bool bProjected = LocalPC && LocalPC->ProjectWorldLocationToScreen(actorLoc, &screenPos, false);

                if (bProjected) {
                    entry.cachedRect.topLeft = Shadow::Vec2{ (float)(screenPos.X - 2), (float)(screenPos.Y - 2) };
                    entry.cachedRect.bottomRight = Shadow::Vec2{ (float)(screenPos.X + 2), (float)(screenPos.Y + 2) };
                    entry.cachedRect.valid = true;
                }

                bool bOnScreen = bProjected && (screenPos.X > 0 && screenPos.X < screenW && screenPos.Y > 0 && screenPos.Y < screenH);

                if (bOnScreen) {
                    entry.aliveThisFrame = true;
                    entry.targetAlpha = 1.0f;
                    entry.isItem = true;
                }
                else {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                }

                std::string sName = Structure->GetDescriptiveName().ToString();
                if (sName.empty() || sName == "None") sName = "Structure";

                const float curHP = Structure->Health;
                const float maxHP = Structure->MaxHealth;
                const float healthPct = (maxHP > 0.0f) ? (curHP / maxHP) : 0.0f;
                const int   hpPctInt = (int)(healthPct * 100.0f);

                float* sColMax = isTeam ? g_Config::TeamStructureHealthColor1 : g_Config::StructureHealthColor1;
                float* sColMin = isTeam ? g_Config::TeamStructureHealthColor2 : g_Config::StructureHealthColor2;

                const Shadow::Color hpColor = GetHealthColorLinear(healthPct, sColMax, sColMin);

                std::string owner = Structure->OwnerName.ToString();
                std::string ownerStf = (owner.empty() || owner == "None") ? "" : " [" + owner + "]";

                entry.flags.clear();
                entry.bars.clear();

                entry.flags.push_back({
                    "[" + sName + "]" + std::move(ownerStf) + " [" + std::to_string(hpPctInt) + "%] (" + std::to_string((int)dist) + "m" + ")",
                    hpColor,
                    g_ESP::FlagPos::Right
                    });

                entry.shouldDrawTorpor = false;
            }
            else {
                entry.targetAlpha = 0.0f;
                entry.aliveThisFrame = false;
            }
        } // end actor loop

        for (auto& kv : s_entries) {
            if (!kv.second.aliveThisFrame)
                kv.second.targetAlpha = 0.0f;
        }

        // ----------------------------------------------------------------
        // 水源过滤：只显示最近的 WaterMaxCount 个
        // ----------------------------------------------------------------
        if (g_Config::bDrawWater && !waterCandidates.empty()) {
            std::sort(waterCandidates.begin(), waterCandidates.end(),
                [](const WaterCandidate& a, const WaterCandidate& b) {
                    return a.dist < b.dist;
                });

            for (auto& wc : waterCandidates) {
                uintptr_t key = reinterpret_cast<uintptr_t>(wc.actor);
                if (s_entries.count(key)) {
                    s_entries[key].targetAlpha = 0.0f;
                    s_entries[key].aliveThisFrame = false;
                }
            }

            const int showCount = ((int)waterCandidates.size() < g_Config::WaterMaxCount)
                ? (int)waterCandidates.size()
                : g_Config::WaterMaxCount;

            const Shadow::Color waterColor = Shadow::Color{ g_Config::WaterNameColor[0], g_Config::WaterNameColor[1], g_Config::WaterNameColor[2], g_Config::WaterNameColor[3] };
            const Shadow::Color waterDistColor = Shadow::Color{ g_Config::WaterDistanceColor[0], g_Config::WaterDistanceColor[1], g_Config::WaterDistanceColor[2], g_Config::WaterDistanceColor[3] };

            static const std::string kWaterLabel = SDK::FString(L"[水源").ToString();

            for (int wi = 0; wi < showCount; wi++) {
                const WaterCandidate& wc = waterCandidates[wi];
                uintptr_t             wkey = reinterpret_cast<uintptr_t>(wc.actor);
                ESPEntry& wEntry = s_entries[wkey];

                SDK::FVector2D currentScreenPos;
                if (LocalPC && LocalPC->ProjectWorldLocationToScreen(wc.surfaceLoc, &currentScreenPos, false)) {
                    wEntry.cachedRect.topLeft = Shadow::Vec2{ (float)(currentScreenPos.X - 2), (float)(currentScreenPos.Y - 2) };
                    wEntry.cachedRect.bottomRight = Shadow::Vec2{ (float)(currentScreenPos.X + 2), (float)(currentScreenPos.Y + 2) };
                    wEntry.cachedRect.valid = true;

                    const bool onScreen = currentScreenPos.X > 0 && currentScreenPos.X < screenW
                        && currentScreenPos.Y > 0 && currentScreenPos.Y < screenH;

                    if (onScreen) {
                        wEntry.aliveThisFrame = true;
                        wEntry.targetAlpha = 1.0f;
                        wEntry.isItem = true;

                        wEntry.flags.clear();
                        wEntry.bars.clear();
                        wEntry.flags.push_back({ kWaterLabel + "] (" + std::to_string((int)wc.dist) + "m" + ")", waterColor, g_ESP::FlagPos::Right });

                        wEntry.shouldDrawBox = false;
                        wEntry.shouldDrawHealthBar = false;
                        wEntry.shouldDrawName = false;
                        wEntry.shouldDrawDistance = true;
                        wEntry.shouldDrawTorpor = false;
                    }
                    else {
                        wEntry.targetAlpha = 0.0f;
                        wEntry.aliveThisFrame = false;
                    }
                }
                else {
                    wEntry.targetAlpha = 0.0f;
                    wEntry.aliveThisFrame = false;
                }
            }
        }

        // ----------------------------------------------------------------
        // 渲染 & 淡入淡出 & 清理
        // ----------------------------------------------------------------
        s_toErase.clear();
        static std::vector<ESPEntry*> RenderEntries;
        RenderEntries.clear();

        for (auto& kv : s_entries) {
            ESPEntry& entry = kv.second;

            const float fadeTime = (entry.targetAlpha > entry.alpha) ? FADE_IN_TIME : FADE_OUT_TIME;
            entry.alpha = g_Util::ApproachAlpha(entry.alpha, entry.targetAlpha, deltaTime, fadeTime);

            if (entry.alpha <= 0.001f && entry.targetAlpha <= 0.001f && !entry.aliveThisFrame) {
                s_toErase.push_back(kv.first);
                continue;
            }

            if (entry.alpha > 0.001f) {
                RenderEntries.push_back(&entry);
            }
        }

        // Give readable labels to the visually nearest/largest targets first.
        // Farther labels that collide are suppressed by FlagManager instead of
        // being shifted away from the creature they describe.
        std::sort(RenderEntries.begin(), RenderEntries.end(), [](const ESPEntry* Left,
            const ESPEntry* Right) {
                if (Left->aliveThisFrame != Right->aliveThisFrame) {
                    return Left->aliveThisFrame > Right->aliveThisFrame;
                }
                const float LeftHeight = Left->cachedRect.valid
                    ? Left->cachedRect.bottomRight.y - Left->cachedRect.topLeft.y : 0.0f;
                const float RightHeight = Right->cachedRect.valid
                    ? Right->cachedRect.bottomRight.y - Right->cachedRect.topLeft.y : 0.0f;
                if (LeftHeight != RightHeight) return LeftHeight > RightHeight;
                return Left->actorKey < Right->actorKey;
            });

        g_ESP::BeginLabelFrame();
        for (ESPEntry* Entry : RenderEntries) {
            ESPEntry& entry = *Entry;
            if (entry.alpha > 0.001f) {
                if (!entry.isItem && entry.shouldDrawBox && entry.cachedRect.valid) {
                    Shadow::Color boxCol = entry.boxColor;

                    g_ESP::DrawBox(Canvas, entry.cachedRect, boxCol, entry.alpha);
                }

                g_ESP::BarManager bm;
                bm.Reset();
                for (const auto& bar : entry.bars)
                    bm.AddBar(Canvas, entry.cachedRect, bar.currentValue, bar.maxValue, bar.color, bar.pos, bar.orientation, entry.alpha);

                g_ESP::FlagManager fm;
                fm.Reset();
                for (const auto& f : entry.flags)
                    fm.AddFlag(Canvas, entry.cachedRect, f.text, f.color, f.pos, entry.alpha, &bm);
            }
        }

        for (uintptr_t k : s_toErase)
            s_entries.erase(k);
    }
} // namespace g_DrawESP
