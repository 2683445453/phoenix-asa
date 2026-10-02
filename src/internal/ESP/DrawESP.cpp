// DrawESP.cpp
#include "../../external/CppSDK/SDK.hpp"
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../../external/Minimal-D3D12-Hook-ImGui/MinHook/include/MinHook.h"
#include "../../external/AOBScan/AOBScan.hpp"
#include "../../external/Shadow-Gui/include/Shadow.h"
#include "../CheatData/DynamicData.hpp"
#include "ESP.h"
#include "StructureESP.h"
#include "OverlayGeometry.h"
#include "../Legit/Aimbot/Aimbot.h"
#include "../Config/Configs.h"
#include "../Config/InterfaceRules.h"
#include "../Hack/FeatureExpansionRules.h"
#include "DrawESP.h"
#include "../Util/Util.h"
#include "../Language/LanguageManager.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <memory>
#include <mutex>
#include <cstdio>
#include <chrono>
#include <cctype>
#include <cstring>
#include <array>
#include <algorithm>
#include <limits>

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

	struct CachedActorType {
		int32_t ClassObjectIndex = -1;
		ActorType Type = ActorType::Unknown;
	};

	static std::unordered_map<SDK::UClass*, CachedActorType> s_actorTypeCache;

    ActorType ClassifyESPActor(SDK::AActor* Actor) {
        if (!Actor || !Actor->Class) return ActorType::Unknown;
		SDK::UClass* ActorClass = Actor->Class;
		const auto Cached = s_actorTypeCache.find(ActorClass);
		if (Cached != s_actorTypeCache.end() &&
			Cached->second.ClassObjectIndex == ActorClass->Index) {
			return Cached->second.Type;
		}

		ActorType Result = ActorType::Unknown;
        if (Actor->IsA(SDK::AShooterCharacter::StaticClass())) {
			Result = ActorType::ShooterCharacter;
        }
		else if (Actor->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
			Result = ActorType::PrimalDinoCharacter;
        }
		else if (Actor->IsA(SDK::APrimalCharacter::StaticClass())) {
			Result = ActorType::PrimalCharacter;
        }
		else if (Actor->IsA(SDK::APhysicsVolume::StaticClass())) {
			Result = ActorType::PhysicsVolume;
        }
		else if (Actor->IsA(SDK::ADroppedItem::StaticClass())) {
			Result = ActorType::DroppedItem;
        }
		else if (Actor->IsA(SDK::APrimalStructure::StaticClass())) {
			Result = ActorType::PrimalStructure;
        }

		s_actorTypeCache[ActorClass] = { ActorClass->Index, Result };
		return Result;
    }

    struct CachedItemTexture {
        SDK::UTexture2D* texture = nullptr;
        int32_t index = -1;
        int32_t serial = -1;
    };
    struct CachedLootItem {
        g_ESP::LootItemInfo info;
        CachedItemTexture icon;
    };
    constexpr size_t MaximumLootIcons = 24;
    CachedItemTexture CaptureTexture(SDK::UTexture2D* Icon);

    struct ESPEntry {
        uintptr_t      actorKey = 0;
        int32_t        objectIndex = -1;
        ActorType      actorType = ActorType::Unknown;
        SDK::FVector   lastWorldLoc{};
        SDK::FVector   indicatorWorldLoc{};
        g_ESP::RelationType relation = g_ESP::RelationType::Enemy;
        g_ESP::BoxRect cachedRect;
        std::string    name;
        std::string    owner;
        std::string armor, weapon, ammo;
        std::array<CachedItemTexture, 5> armorIcons{};
        CachedItemTexture weaponIcon{};
        std::array<Shadow::Color, 5> armorQualityColors{};
        Shadow::Color weaponQualityColor{};
        std::vector<CachedLootItem> lootContents;
        int lootMoreCount = 0;
        std::chrono::steady_clock::time_point nextLootRefresh{};
        std::chrono::steady_clock::time_point nextEquipmentRefresh{};
        g_ESP::Structures::Type structureType = g_ESP::Structures::Type::Other;
        bool structureTypeKnown = false;
        uint8_t localAggro = 0; // 0: no confirmed target, 1: player, 2: mount
        uint8_t creatureAggroStencil = 0;
        std::chrono::steady_clock::time_point nextAggroRefresh{};

        std::vector<CachedFlag> flags;
        std::vector<CachedBar>  bars;

        Shadow::Color boxColor{ 0,0,0,0 };
        Shadow::Color nameColor{ 0,0,0,0 };
        Shadow::Color distanceColor{ 0,0,0,0 };
        float  configBoxAlpha = 1.0f;
        float  targetAlpha = 0.0f;
        float  alpha = 0.0f;
        float  lastSeenTime = 0.0f;
        float  distanceMeters = 0.0f;
        float  labelScale = 1.0f;
        bool   aliveThisFrame = false;
        bool   isOOF = false;
        float  oofScale = 1.0f;
        bool   isItem = false;
        SDK::UTexture2D* itemIcon = nullptr;
        int32_t itemIconIndex = -1;
        int32_t itemIconSerial = -1;
        SDK::UPrimalItem* iconSourceItem = nullptr;
        Shadow::Color itemQualityColor{};
        std::chrono::steady_clock::time_point nextItemIconRefresh{};
        float  cachedHP = 0.0f;
        float  cachedMaxHP = 0.0f;
        float  cachedTorpor = 0.0f;
        float  cachedMaxTorpor = 0.0f;

        int infoSlot = -1;
        bool characterAlive = false;
        bool selectedTarget = false;
        bool shouldDrawBox = false;
        bool shouldDrawHealthBar = false;
        bool shouldDrawName = false;
        bool shouldDrawDistance = false;
        bool shouldDrawTorpor = false;

        SDK::FVector visualBoundsOrigin{};
        SDK::FVector visualBoundsExtent{};
        SDK::FVector visualBoundsActorLocation{};
        bool hasVisualBounds = false;
        std::chrono::steady_clock::time_point nextVisualBoundsRefresh =
            std::chrono::steady_clock::time_point::min();
    };

    float GetDinoLabelScale(float DistanceMeters) {
        if (!g_Config::bDinoDistanceTextScale || !std::isfinite(DistanceMeters)) {
            return 1.0f;
        }
        const float MaximumDistance = std::clamp(
            g_Config::DinoTextScaleDistance, 25.0f, 2000.0f);
        float T = std::clamp(DistanceMeters / MaximumDistance, 0.0f, 1.0f);
        T = T * T * (3.0f - 2.0f * T);
        const float NearScale = std::clamp(g_Config::DinoNearTextScale, 0.75f, 2.0f);
        const float FarScale = std::clamp(g_Config::DinoFarTextScale, 0.45f, 1.25f);
        return NearScale + (FarScale - NearScale) * T;
    }

    bool ResolveCharacterVisualBounds(SDK::APrimalCharacter* Character,
        ESPEntry&, const SDK::FVector&, const std::chrono::steady_clock::time_point,
        SDK::FVector* OutOrigin, SDK::FVector* OutExtent) {
        if (!Character || !Character->Mesh || !OutOrigin || !OutExtent) return false;
        float Radius = 0;
        SDK::UKismetSystemLibrary::GetComponentBounds(Character->Mesh, OutOrigin, OutExtent, &Radius);
        // Current rendered bounds include rotation, animation and mesh offsets.
        // Invalid streamed bounds fall back to the current capsule in GetBox.
        return std::isfinite(OutOrigin->X) && std::isfinite(OutOrigin->Y) && std::isfinite(OutOrigin->Z) &&
            std::isfinite(OutExtent->X) && std::isfinite(OutExtent->Y) && std::isfinite(OutExtent->Z) &&
            std::isfinite(Radius) && Radius > 0 && OutExtent->X > 0 && OutExtent->Y > 0 && OutExtent->Z > 0 &&
            OutExtent->X < 250000 && OutExtent->Y < 250000 && OutExtent->Z < 250000;
    }

	bool DrawOffscreenThreatIndicator(ESPEntry& Entry,
		const SDK::FVector& CameraLocation, const SDK::FRotator& CameraRotation,
		float ScreenWidth, float ScreenHeight, double TimeSeconds,
		std::vector<Shadow::Vec2>& OccupiedTips) {
		Shadow::ShadowDrawList* DrawList = Shadow::GetBackgroundDrawList();
		if (!DrawList || ScreenWidth <= 1.0f || ScreenHeight <= 1.0f) return false;

		const SDK::FVector Offset{
			Entry.indicatorWorldLoc.X - CameraLocation.X,
			Entry.indicatorWorldLoc.Y - CameraLocation.Y,
			Entry.indicatorWorldLoc.Z - CameraLocation.Z
		};
		const double Length = std::sqrt(
			Offset.X * Offset.X + Offset.Y * Offset.Y + Offset.Z * Offset.Z);
		if (!std::isfinite(Length) || Length < 1.0) return false;

		const SDK::FVector Forward =
			SDK::UKismetMathLibrary::GetForwardVector(CameraRotation);
		const SDK::FVector Right =
			SDK::UKismetMathLibrary::GetRightVector(CameraRotation);
		const SDK::FVector Up =
			SDK::UKismetMathLibrary::GetUpVector(CameraRotation);
		auto Dot = [&Offset](const SDK::FVector& Axis) {
			return Offset.X * Axis.X + Offset.Y * Axis.Y + Offset.Z * Axis.Z;
		};

		const double LocalForward = Dot(Forward);
		const double LocalRight = Dot(Right);
		const double LocalUp = Dot(Up);
		constexpr double Pi = 3.14159265358979323846;
		double Yaw = std::atan2(LocalRight, LocalForward);
		if (LocalForward < 0.0 && std::abs(LocalRight) < 0.001) {
			// Preserve a deterministic side for a target exactly behind the
			// camera; a parity choice avoids every such target piling up at right.
			Yaw = (Entry.actorKey & 1u) != 0 ? Pi : -Pi;
		}
		const double Pitch = std::atan2(LocalUp,
			std::max(0.001, std::hypot(LocalForward, LocalRight)));
		double DirectionX = Yaw / Pi;
		double DirectionY = -Pitch / (Pi * 0.5);
		const double DirectionLength = std::sqrt(
			DirectionX * DirectionX + DirectionY * DirectionY);
		if (!std::isfinite(DirectionLength) || DirectionLength < 0.001) return false;
		DirectionX /= DirectionLength;
		DirectionY /= DirectionLength;

		const float MaximumDistance = std::clamp(
			g_Config::OOFMaxDistance, 25.0f, 2000.0f);
		const float DistanceRatio = std::clamp(
			(Entry.distanceMeters - 10.0f) / std::max(1.0f, MaximumDistance - 10.0f),
			0.0f, 1.0f);
		const float SmoothDistance = DistanceRatio * DistanceRatio *
			(3.0f - 2.0f * DistanceRatio);
		const float TargetScale = 1.42f + (0.68f - 1.42f) * SmoothDistance;
		const float ScaleBlend = std::clamp(Shadow::GetIO().DeltaTime * 10.0f,
			0.0f, 1.0f);
		Entry.oofScale += (TargetScale - Entry.oofScale) * ScaleBlend;
		const float Size = std::clamp(g_Config::OOFSize, 5.0f, 30.0f) *
			std::clamp(Entry.oofScale, 0.65f, 1.45f);
		const float RadiusScale = std::clamp(g_Config::OOFRadius, 0.50f, 1.0f);
		const float RadiusX = std::max(20.0f,
			ScreenWidth * 0.5f * RadiusScale - Size - 8.0f);
		const float RadiusY = std::max(20.0f,
			ScreenHeight * 0.5f * RadiusScale - Size - 8.0f);
		const double EllipseScale = 1.0 / std::sqrt(
			DirectionX * DirectionX / (RadiusX * RadiusX) +
			DirectionY * DirectionY / (RadiusY * RadiusY));
		const Shadow::Vec2 Center{ ScreenWidth * 0.5f, ScreenHeight * 0.5f };
		const Shadow::Vec2 Tip{
			Center.x + static_cast<float>(DirectionX * EllipseScale),
			Center.y + static_cast<float>(DirectionY * EllipseScale)
		};
		const float CollisionRadius = std::max(26.0f, Size * 1.9f);
		for (const Shadow::Vec2& Occupied : OccupiedTips) {
			const float DeltaX = Tip.x - Occupied.x;
			const float DeltaY = Tip.y - Occupied.y;
			if (DeltaX * DeltaX + DeltaY * DeltaY <
				CollisionRadius * CollisionRadius) {
				return false;
			}
		}
		OccupiedTips.push_back(Tip);
		const Shadow::Vec2 Base{
			Tip.x - static_cast<float>(DirectionX) * Size * 1.35f,
			Tip.y - static_cast<float>(DirectionY) * Size * 1.35f
		};
		const Shadow::Vec2 Perpendicular{
			-static_cast<float>(DirectionY), static_cast<float>(DirectionX)
		};
		const Shadow::Vec2 Left{
			Base.x + Perpendicular.x * Size * 0.65f,
			Base.y + Perpendicular.y * Size * 0.65f
		};
		const Shadow::Vec2 RightPoint{
			Base.x - Perpendicular.x * Size * 0.65f,
			Base.y - Perpendicular.y * Size * 0.65f
		};

		const float MinimumAlpha = std::clamp(g_Config::OOFMinAlpha, 0.10f, 0.90f);
		const float MaximumAlpha = std::clamp(g_Config::OOFMaxAlpha,
			MinimumAlpha, 1.0f);
		const float BreathSpeed = std::clamp(g_Config::OOFBreathSpeed, 0.10f, 5.0f);
		const double Phase = static_cast<double>(Entry.actorKey % 997u) /
			997.0 * (Pi * 2.0);
		const float Breath = g_Config::bStaticThreatIndicators ? 1.0f : 0.72f + 0.28f * static_cast<float>(
			std::sin(TimeSeconds * BreathSpeed * Pi * 2.0 + Phase));
		const float Alpha = std::clamp(
			(MinimumAlpha + (MaximumAlpha - MinimumAlpha) * Breath) *
			Entry.alpha * g_Config::OOFColor[3], 0.0f, 1.0f);
		const float IndicatorVisibility = std::clamp(
			Entry.alpha * g_Config::OOFColor[3], 0.0f, 1.0f);
		const Shadow::Color Outline{ 0.0f, 0.0f, 0.0f,
			std::max(0.72f * IndicatorVisibility, Alpha) };
		const float EntryTintWeight = 0.72f;
		const Shadow::Color Color{
			std::clamp(Entry.boxColor.r * EntryTintWeight +
				g_Config::OOFColor[0] * (1.0f - EntryTintWeight), 0.0f, 1.0f),
			std::clamp(Entry.boxColor.g * EntryTintWeight +
				g_Config::OOFColor[1] * (1.0f - EntryTintWeight), 0.0f, 1.0f),
			std::clamp(Entry.boxColor.b * EntryTintWeight +
				g_Config::OOFColor[2] * (1.0f - EntryTintWeight), 0.0f, 1.0f),
			Alpha
		};

		DrawList->AddTriangle(Tip, Left, RightPoint, Outline, 4.0f);
		DrawList->AddTriangleFilled(Tip, Left, RightPoint, Color);
		std::string DistanceText;
		if (Entry.distanceMeters >= 1000.0f) {
			char Buffer[24]{};
			std::snprintf(Buffer, sizeof(Buffer), "%.1fkm",
				Entry.distanceMeters / 1000.0f);
			DistanceText = Buffer;
		}
		else {
			DistanceText = std::to_string(
				static_cast<int>(std::round(Entry.distanceMeters))) + "m";
		}
		const float TextScale = 1.08f + (0.82f - 1.08f) * SmoothDistance;
		Shadow::Vec2 TextSize{};
		if (Shadow::DefaultFont) {
			Shadow::PushFont(Shadow::DefaultFont, TextScale);
			TextSize = Shadow::MeasureTextSize(DistanceText);
			Shadow::PopFont();
		}
		const Shadow::Vec2 LabelCenter{
			Base.x - static_cast<float>(DirectionX) * (Size + 12.0f),
			Base.y - static_cast<float>(DirectionY) * (Size + 12.0f)
		};
		Shadow::Vec2 LabelPosition{
			LabelCenter.x - TextSize.x * 0.5f,
			LabelCenter.y - TextSize.y * 0.5f
		};
		LabelPosition.x = std::clamp(LabelPosition.x, 4.0f,
			std::max(4.0f, ScreenWidth - TextSize.x - 4.0f));
		LabelPosition.y = std::clamp(LabelPosition.y, 4.0f,
			std::max(4.0f, ScreenHeight - TextSize.y - 4.0f));
		const Shadow::Color LabelBackground{ 0.0f, 0.0f, 0.0f,
			0.58f * IndicatorVisibility };
		DrawList->AddRectFilled(
			{ LabelPosition.x - 3.0f, LabelPosition.y - 1.0f },
			{ TextSize.x + 6.0f, TextSize.y + 2.0f }, LabelBackground);
		Shadow::Color TextColor = Color;
		TextColor.a = std::max(0.82f * IndicatorVisibility, Alpha);
		DrawList->AddText(Shadow::DefaultFont, TextScale, Outline, Outline,
			LabelPosition, TextColor, DistanceText);
		return true;
	}

    static std::unordered_map<uintptr_t, ESPEntry> s_entries;

	struct CachedESPActorRef {
		SDK::AActor* Actor = nullptr;
		int32_t ObjectIndex = -1;
		ActorType Type = ActorType::Unknown;
	};

	static std::vector<CachedESPActorRef> s_cachedESPActors;
	static SDK::UWorld* s_cachedESPWorld = nullptr;
	static std::chrono::steady_clock::time_point s_nextESPActorRefresh =
		std::chrono::steady_clock::time_point::min();

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

    constexpr uint8_t EncodeFullHealthStencil(SDK::EStencilAlliance Alliance) {
        return static_cast<uint8_t>((31u << 3) |
            (static_cast<uint8_t>(Alliance) & 0x07u));
    }

    static_assert(EncodeFullHealthStencil(SDK::EStencilAlliance::Friendly) == 0xF9);
    static_assert(EncodeFullHealthStencil(SDK::EStencilAlliance::NPC) == 0xFA);
    static_assert(EncodeFullHealthStencil(SDK::EStencilAlliance::NPCFleeing) == 0xFB);
    static_assert(EncodeFullHealthStencil(SDK::EStencilAlliance::Hostile) == 0xFC);
    static_assert(EncodeFullHealthStencil(SDK::EStencilAlliance::Element) == 0xFE);

    struct HighlightObjectRef {
        SDK::UPrimitiveComponent* Component = nullptr;
        int32_t ObjectIndex = -1;
        int32_t SerialNumber = -1;
    };

    struct DesiredModelHighlight {
        int32_t ObjectIndex = -1;
        int32_t SerialNumber = -1;
        uint8_t StencilValue = 0;
        bool SolidFill = false;
    };

    struct ResourceMarker {
        SDK::FVector Location{};
        ResourceType Type = ResourceType::None;
        float DistanceSquared = 0.0f;
        std::vector<HighlightObjectRef> HighlightComponents;
    };

    struct ModelHighlightState {
        int32_t ObjectIndex = -1;
        int32_t SerialNumber = -1;
        SDK::UMaterialInterface* OriginalOverlay = nullptr;
        int32_t OriginalOverlayIndex = -1;
        int32_t OriginalOverlaySerial = -1;
        SDK::UMaterialInterface* AppliedOverlay = nullptr;
        float OriginalOverlayDistance = 0.0f;
        bool AppliedOverlayDistance = false;
        bool OriginalRenderCustomDepth = false;
        int32_t OriginalStencilValue = 0;
        SDK::ERendererStencilMask OriginalStencilWriteMask =
            SDK::ERendererStencilMask::ERSM_Default;
        bool LastAppliedRenderCustomDepth = false;
        int32_t LastAppliedStencilValue = 0;
        SDK::ERendererStencilMask LastAppliedStencilWriteMask =
            SDK::ERendererStencilMask::ERSM_Default;
    };

    struct OwnedCameraBool {
        bool Baseline = false;
        bool LastApplied = false;
        bool DidWrite = false;
    };

    struct CameraEffectSlot {
        SDK::UMaterialInterface* Material = nullptr;
        bool Added = false;
        float OriginalWeight = 0.0f;
        bool HadBlendWeight = false;
        float OriginalBlendWeight = 0.0f;
    };

    struct ResourceCameraHighlightState {
        SDK::AShooterPlayerCameraManager* CameraManager = nullptr;
        int32_t ObjectIndex = -1;
        int32_t SerialNumber = -1;
        SDK::UMaterialInterface* OutlineMaterial = nullptr;
        int32_t OutlineMaterialIndex = -1;
        int32_t OutlineMaterialSerialNumber = -1;
        bool OutlineLoadAttempted = false;
        std::chrono::steady_clock::time_point NextMaterialSearch =
            std::chrono::steady_clock::time_point::min();
        bool BlendableAdded = false;
        int AddedBlendableIndex = -1;
        bool BlendableReplaced = false;
        int ReplacedBlendableIndex = -1;
        SDK::FWeightedBlendable ReplacedBlendable{};
        bool ExistingWeightChanged = false;
        int ExistingBlendableIndex = -1;
        float ExistingBlendableWeight = 0.0f;
        float ExistingBlendableLastAppliedWeight = 0.0f;
        bool BlendablePreparedForCameraUpdate = false;
        OwnedCameraBool HasCustomDepthStencilOverride{};
        OwnedCameraBool CompleteStencilOverride{};
        OwnedCameraBool IgnoreHealthOverride{};
        CameraEffectSlot FinalCameraSlot{};
        float AppliedIntensity = -1.0f;
        float AppliedOpacity = -1.0f;
    };

    struct ResourceRenderPipelineState {
        SDK::UWorld* World = nullptr;
        int32_t WorldObjectIndex = -1;
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
    static std::unordered_map<SDK::UPrimitiveComponent*, ModelHighlightState>
        s_modelHighlightStates;
    static std::unordered_map<SDK::UPrimitiveComponent*, DesiredModelHighlight>
        s_resourceDesiredHighlights;
    static std::unordered_map<SDK::UPrimitiveComponent*, DesiredModelHighlight>
        s_actorDesiredHighlights;
    static ResourceCameraHighlightState s_resourceCameraHighlightState{};
    static ResourceRenderPipelineState s_resourceRenderPipelineState{};
    static SDK::UWorld* s_resourceWorld = nullptr;
    static std::chrono::steady_clock::time_point s_nextResourceScan =
        std::chrono::steady_clock::time_point::min();
    static ResourceESPDiagnostics s_resourceDiagnostics{};

    using FMemoryReallocFunction = void* (__fastcall*)(
        void* Original, uint64_t Size, uint32_t Alignment);

    enum class FMemoryReallocResolveState : uint8_t {
        Unresolved,
        Ready,
        Unavailable,
    };

    static FMemoryReallocFunction s_fMemoryRealloc = nullptr;
    static FMemoryReallocResolveState s_fMemoryReallocResolveState =
        FMemoryReallocResolveState::Unresolved;

    FMemoryReallocFunction ResolveFMemoryRealloc() {
        if (s_fMemoryReallocResolveState == FMemoryReallocResolveState::Ready) {
            return s_fMemoryRealloc;
        }
        if (s_fMemoryReallocResolveState == FMemoryReallocResolveState::Unavailable) {
            return nullptr;
        }

        // UE's FMemory::Realloc wrapper. This signature is unique in the
        // current ASA client and matches the local server implementation. The
        // relative operands are intentionally wildcarded across builds.
        constexpr const char* ReallocPattern =
            "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 "
            "48 8B F1 41 8B D8 48 8B 0D ? ? ? ? 48 8B FA "
            "48 85 C9 75 0C E8 ? ? ? ? 48 8B 0D ? ? ? ? "
            "48 8B 01 44 8B CB 4C 8B C7 48 8B D6 "
            "48 8B 5C 24 30 48 8B 74 24 38 48 83 C4 20 5F 48 FF 60 38";
        const AOB::Result Matches = AOB::Scan(ReallocPattern);
        if (Matches.size() != 1) {
            s_fMemoryReallocResolveState = FMemoryReallocResolveState::Unavailable;
            return nullptr;
        }

        s_fMemoryRealloc = reinterpret_cast<FMemoryReallocFunction>(Matches[0]);
        s_fMemoryReallocResolveState = FMemoryReallocResolveState::Ready;
        return s_fMemoryRealloc;
    }

    bool GrowWeightedBlendableArray(
        SDK::TArray<SDK::FWeightedBlendable>& Blendables) {
        static_assert(sizeof(SDK::FWeightedBlendable) == 0x10);
        static_assert(sizeof(SDK::TArray<SDK::FWeightedBlendable>) == 0x10);

        const int32_t Count = Blendables.Num();
        const int32_t Capacity = Blendables.Max();
        if (Count < 0 || Count > 4096 || Capacity < Count || Capacity > 4096 ||
            (Count > 0 && !Blendables.GetDataPtr())) {
            return false;
        }
        if (Capacity > Count) return true;
        if (Count >= 4096) return false;

        FMemoryReallocFunction Realloc = ResolveFMemoryRealloc();
        if (!Realloc) return false;

        const int32_t Growth = std::max(1, Count / 2);
        const int32_t NewCapacity = std::min(4096,
            std::max(4, Count + Growth));
        const uint64_t AllocationSize =
            static_cast<uint64_t>(NewCapacity) * sizeof(SDK::FWeightedBlendable);
        void* NewAllocation = Realloc(
            const_cast<SDK::FWeightedBlendable*>(Blendables.GetDataPtr()),
            AllocationSize,
            static_cast<uint32_t>(alignof(SDK::FWeightedBlendable)));
        if (!NewAllocation) return false;

        // Rebuild only the array header. The allocation remains owned by UE,
        // so later native growth/destruction uses the same allocator family.
        Blendables = SDK::TArray<SDK::FWeightedBlendable>(
            static_cast<SDK::FWeightedBlendable*>(NewAllocation),
            Count, NewCapacity);
        return true;
    }

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
        return IsResourceTypeEnabled(Type);
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

    bool TryGetLiveObjectSerial(const SDK::UObject* ExpectedObject,
        int32_t ObjectIndex, int32_t* OutSerialNumber) {
        if (!ExpectedObject || !OutSerialNumber) return false;

        SDK::TUObjectArray* ObjectArray = SDK::UObject::GObjects.GetTypedPtr();
        if (!ObjectArray || ObjectIndex < 0 || ObjectIndex >= ObjectArray->NumElements) {
            return false;
        }

        const int32_t ChunkIndex = ObjectIndex / SDK::TUObjectArray::ElementsPerChunk;
        const int32_t IndexWithinChunk = ObjectIndex % SDK::TUObjectArray::ElementsPerChunk;
        if (ChunkIndex < 0 || ChunkIndex >= ObjectArray->NumChunks) return false;

        SDK::FUObjectItem** ObjectChunks = ObjectArray->GetDecrytedObjPtr();
        if (!ObjectChunks || !ObjectChunks[ChunkIndex]) return false;
        const SDK::FUObjectItem* Item = &ObjectChunks[ChunkIndex][IndexWithinChunk];
        if (Item->Object != ExpectedObject) return false;

        // UE's FUObjectItem serial number occupies offset 0x10 in the verified
        // 0x18-byte ASA object item. Keep this read local so the generated SDK
        // layout remains untouched.
        std::memcpy(OutSerialNumber,
            reinterpret_cast<const uint8_t*>(Item) + 0x10,
            sizeof(*OutSerialNumber));
        return true;
    }

    struct SpyglassLootSnapshot {
        int32_t ObjectIndex = -1;
        std::chrono::steady_clock::time_point Received{};
        std::vector<CachedLootItem> Items;
        int MoreCount = 0;
    };
    struct SpyglassPendingRequest {
        SDK::UObject* Actor = nullptr;
        int32_t ActorIndex = -1;
        int32_t BuffIndex = -1;
        std::chrono::steady_clock::time_point Sent{};
    };
    static std::unordered_map<uintptr_t, SpyglassLootSnapshot> s_spyglassLoot;
    static std::unordered_map<uintptr_t, SpyglassPendingRequest> s_spyglassPending;
    static std::mutex s_spyglassLootMutex;
    using SpyglassProcessEventFn = void(__fastcall*)(SDK::UObject*, SDK::UFunction*, void*);
    static SpyglassProcessEventFn s_spyglassProcessEvent = nullptr;
    static SDK::UFunction* s_spyglassPushFunction = nullptr;
    static SDK::UFunction* s_spyglassRequestFunction = nullptr;
    static std::chrono::steady_clock::time_point s_nextSpyglassSearch{};

    struct SpyglassArrayView { const uint8_t* Data; int32_t Num; int32_t Max; };
    static_assert(sizeof(SpyglassArrayView) == 16);

    SDK::FProperty* FindLootField(SDK::UStruct* Type, const char* Name, const char* PropertyType) {
        for (SDK::FField* Field = Type->ChildProperties; Field; Field = Field->Next) {
            auto* Property = static_cast<SDK::FProperty*>(Field);
            if (!Property->ClassPrivate) continue;
            std::string FieldName = Field->Name.ToString();
            FieldName.resize(FieldName.find('_') == std::string::npos ? FieldName.size() : FieldName.find('_'));
            FieldName.erase(std::remove(FieldName.begin(), FieldName.end(), ' '), FieldName.end());
            if (EqualsResourceFieldName(FieldName, Name) &&
                EqualsResourceFieldName(Property->ClassPrivate->Name.ToString(), PropertyType)) return Property;
        }
        return nullptr;
    }

    void CaptureSpyglassRequest(SDK::UObject* Buff, SDK::UFunction* Function, void* Parameters) {
        if (!g_Config::bStructureShowLootContents || !Buff || !Parameters ||
            Function != s_spyglassRequestFunction || !Buff->Class ||
            Buff->Class->GetName() != "AwesomeSpyGlass_Buff_C") return;
        auto* ActorProperty = FindResourceProperty(Function, "Actor");
        if (!ActorProperty || !ActorProperty->ClassPrivate || ActorProperty->Offset < 0 ||
            ActorProperty->Offset + sizeof(SDK::UObject*) > static_cast<size_t>(Function->Size) ||
            !EqualsResourceFieldName(ActorProperty->ClassPrivate->Name.ToString(), "ObjectProperty")) return;
        SDK::UObject* Actor = nullptr;
        std::memcpy(&Actor, static_cast<const uint8_t*>(Parameters) + ActorProperty->Offset, sizeof(Actor));
        int32_t Serial = -1;
        if (!Actor || !TryGetLiveObjectSerial(Actor, Actor->Index, &Serial) ||
            !Actor->IsA(SDK::APrimalStructure::StaticClass())) return;
        std::lock_guard<std::mutex> Guard(s_spyglassLootMutex);
        if (s_spyglassPending.size() > 16) s_spyglassPending.clear();
        s_spyglassPending[reinterpret_cast<uintptr_t>(Buff)] =
            {Actor, Actor->Index, Buff->Index, std::chrono::steady_clock::now()};
    }

    void CaptureSpyglassInventory(SDK::UObject* Buff, SDK::UFunction* Function, void* Parameters) {
        if (!g_Config::bStructureShowLootContents || !Buff || !Parameters ||
            Function != s_spyglassPushFunction || !Buff->Class ||
            Buff->Class->GetName() != "AwesomeSpyGlass_Buff_C") return;
        SpyglassPendingRequest Pending{};
        {
            std::lock_guard<std::mutex> Guard(s_spyglassLootMutex);
            auto Found = s_spyglassPending.find(reinterpret_cast<uintptr_t>(Buff));
            if (Found == s_spyglassPending.end()) return;
            Pending = Found->second;
            s_spyglassPending.erase(Found);
        }
        if (Pending.BuffIndex != Buff->Index ||
            std::chrono::steady_clock::now() - Pending.Sent > std::chrono::seconds(5)) return;
        auto* Target = Pending.Actor;
        int32_t Serial = -1;
        if (!Target || !TryGetLiveObjectSerial(Target, Pending.ActorIndex, &Serial) ||
            !Target->IsA(SDK::APrimalStructure::StaticClass())) return;

        auto* ListProperty = FindResourceProperty(Function, "Item List");
        if (!ListProperty || !ListProperty->ClassPrivate || ListProperty->Offset < 0 ||
            ListProperty->Offset + sizeof(SpyglassArrayView) > static_cast<size_t>(Function->Size) ||
            !EqualsResourceFieldName(ListProperty->ClassPrivate->Name.ToString(), "ArrayProperty")) return;
        auto* ArrayProperty = static_cast<SDK::FArrayProperty*>(ListProperty);
        auto* Inner = ArrayProperty->InnerProperty;
        if (!Inner || !Inner->ClassPrivate ||
            !EqualsResourceFieldName(Inner->ClassPrivate->Name.ToString(), "StructProperty")) return;
        auto* ItemStruct = static_cast<SDK::FStructProperty*>(Inner)->Struct;
        if (!ItemStruct || ItemStruct->Size < 24 || ItemStruct->Size > 256) return;
        auto* NameProperty = FindResourceProperty(ItemStruct, "Name_2_B86A1342493C11DFFB207D81A00642B1");
        auto* QuantityProperty = FindResourceProperty(ItemStruct, "Quantity_15_1DA939714CEAA099B213BD83A79F5E52");
        if (!NameProperty || !QuantityProperty || !NameProperty->ClassPrivate ||
            !QuantityProperty->ClassPrivate || NameProperty->Offset < 0 ||
            QuantityProperty->Offset < 0 ||
            NameProperty->Offset + sizeof(SDK::FString) > static_cast<size_t>(ItemStruct->Size) ||
            QuantityProperty->Offset + sizeof(int32_t) > static_cast<size_t>(ItemStruct->Size) ||
            !EqualsResourceFieldName(NameProperty->ClassPrivate->Name.ToString(), "StrProperty") ||
            !EqualsResourceFieldName(QuantityProperty->ClassPrivate->Name.ToString(), "IntProperty")) return;

        SpyglassArrayView Array{};
        std::memcpy(&Array, static_cast<const uint8_t*>(Parameters) + ListProperty->Offset, sizeof(Array));
        if (Array.Num < 0 || Array.Num > 128 || Array.Max < Array.Num ||
            Array.Max > 10000 || (Array.Num && !Array.Data)) return;
        SpyglassLootSnapshot Snapshot{};
        Snapshot.ObjectIndex = Target->Index;
        Snapshot.Received = std::chrono::steady_clock::now();
        auto* IconProperty = FindLootField(ItemStruct, "Icon", "ObjectProperty");
        if (!IconProperty) IconProperty = FindLootField(ItemStruct, "ItemIcon", "ObjectProperty");
        auto* BlueprintProperty = FindLootField(ItemStruct, "IsBlueprint", "BoolProperty");
        if (!BlueprintProperty) BlueprintProperty = FindLootField(ItemStruct, "bIsBlueprint", "BoolProperty");
        if (!BlueprintProperty) BlueprintProperty = FindLootField(ItemStruct, "Blueprint", "BoolProperty");
        for (int32_t I = 0; I < Array.Num && Snapshot.Items.size() < MaximumLootIcons; ++I) {
            const uint8_t* Item = Array.Data + static_cast<size_t>(I) * ItemStruct->Size;
            const auto& ItemName = *reinterpret_cast<const SDK::FString*>(Item + NameProperty->Offset);
            int32_t Quantity = 0;
            std::memcpy(&Quantity, Item + QuantityProperty->Offset, sizeof(Quantity));
            if (Quantity <= 0 || Quantity > 1000000 || !ItemName.IsValid()) continue;
            std::string Name = ItemName.ToString();
            if (Name.empty() || Name.size() > 256) continue;
            CachedLootItem Loot{};
            Loot.info.Name = std::move(Name);
            Loot.info.Quantity = Quantity;
            if (IconProperty && IconProperty->Offset >= 0 &&
                IconProperty->Offset + sizeof(SDK::UObject*) <= static_cast<size_t>(ItemStruct->Size)) {
                SDK::UTexture2D* Icon = nullptr;
                std::memcpy(&Icon, Item + IconProperty->Offset, sizeof(Icon));
                Loot.icon = CaptureTexture(Icon);
            }
            if (BlueprintProperty && BlueprintProperty->Offset >= 0) {
                const auto* Bool = static_cast<SDK::FBoolProperty*>(BlueprintProperty);
                const size_t Offset = BlueprintProperty->Offset + Bool->ByteOffset;
                if (Offset < static_cast<size_t>(ItemStruct->Size) && Bool->FieldMask)
                    Loot.info.Kind = (Item[Offset] & Bool->FieldMask)
                        ? g_ESP::LootItemKind::Blueprint : g_ESP::LootItemKind::Item;
            }
            Snapshot.Items.push_back(std::move(Loot));
        }
        Snapshot.MoreCount = std::max(0, Array.Num - static_cast<int>(Snapshot.Items.size()));
        {
            std::lock_guard<std::mutex> Guard(s_spyglassLootMutex);
            if (s_spyglassLoot.size() > 512) s_spyglassLoot.clear();
            s_spyglassLoot[reinterpret_cast<uintptr_t>(Target)] = std::move(Snapshot);
        }
    }

    void __fastcall HookedSpyglassProcessEvent(SDK::UObject* Object,
        SDK::UFunction* Function, void* Parameters) {
        g_Hook::CallbackScope Guard;
        if (!g_Hook::IsStopping()) {
            if (Function == s_spyglassRequestFunction)
                CaptureSpyglassRequest(Object, Function, Parameters);
            else if (Function == s_spyglassPushFunction)
                CaptureSpyglassInventory(Object, Function, Parameters);
        }
        s_spyglassProcessEvent(Object, Function, Parameters);
    }

    void TryAttachSpyglassInventory(SDK::APrimalCharacter* LocalChar) {
        if (!g_Config::bStructureShowLootContents || !LocalChar || s_spyglassProcessEvent ||
            g_Hook::IsStopping()) return;
        const auto Now = std::chrono::steady_clock::now();
        if (Now < s_nextSpyglassSearch) return;
        s_nextSpyglassSearch = Now + std::chrono::seconds(3);
        SDK::APrimalCharacter* Owners[2]{LocalChar, nullptr};
        if (LocalChar->IsA(SDK::AShooterCharacter::StaticClass()))
            Owners[1] = static_cast<SDK::AShooterCharacter*>(LocalChar)->MountedDino.Get();
        for (auto* Owner : Owners) {
          if (!Owner) continue;
          SDK::TArray<SDK::APrimalBuff*> Buffs{};
          Owner->GetAllBuffs(&Buffs);
          if (Buffs.Num() < 0 || Buffs.Num() > 4096) continue;
          for (auto* Buff : Buffs) {
            int32_t Serial = -1;
            if (!Buff || !TryGetLiveObjectSerial(Buff, Buff->Index, &Serial) ||
                !Buff->Class || Buff->Class->GetName() != "AwesomeSpyGlass_Buff_C") continue;
            auto* Function = Buff->Class->GetFunction(
                "AwesomeSpyGlass_Buff_C", "Push Inventory Items To Client");
            auto* Request = Buff->Class->GetFunction(
                "AwesomeSpyGlass_Buff_C", "Get Inventory Items");
            if (!Function || !Function->ChildProperties || !Request ||
                !Request->ChildProperties) continue;
            void* Target = (*reinterpret_cast<void***>(Buff))[SDK::Offsets::ProcessEventIdx];
            if (!Target) continue;
            SpyglassProcessEventFn Original = nullptr;
            if (MH_CreateHook(Target, reinterpret_cast<void*>(HookedSpyglassProcessEvent),
                reinterpret_cast<void**>(&Original)) != MH_OK) continue;
            s_spyglassPushFunction = Function;
            s_spyglassRequestFunction = Request;
            s_spyglassProcessEvent = Original;
            if (MH_EnableHook(Target) == MH_OK) return;
            MH_RemoveHook(Target);
            s_spyglassPushFunction = nullptr;
            s_spyglassRequestFunction = nullptr;
            s_spyglassProcessEvent = nullptr;
          }
        }
    }

    // Names below are texture objects listed in the supplied UTexture.cpp.
    // FindObjectFast only uses assets already loaded by the game.
    const char* ItemTextureFallback(const std::string& ClassName) {
        std::string Name = ClassName;
        std::transform(Name.begin(), Name.end(), Name.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        const auto Has = [&](const char* Part) { return Name.find(Part) != std::string::npos; };
        if (Has("burlap") || Has("cloth")) {
            if (Has("hat") || Has("helmet")) return "HUD_BurlapHat_Colorize_d";
            if (Has("shirt")) return "HUD_BurlapShirt_Colorize_d";
            if (Has("pants") || Has("legs")) return "HUD_BurlapLegs_Colorize_d";
            if (Has("boots")) return "HUD_BurlapBoots_Colorize_d";
            if (Has("gloves")) return "HUD_BurlapGloves_colorize_d";
        }
        if (Has("fabricatedcrossbow")) return "HUD_FabricatedCrossbow_Icon_D";
        if (Has("crossbow")) return "HUD_ForgedCrossbow_Colorize_d";
        if (Has("machinedshotgun")) return "HUD_MachinedShotgun_Colorize_d";
        if (Has("machinedpistol")) return "HUD_MachinedPistol_Colorize_d";
        if (Has("sniper")) return "HUD_MachinedSniper_Colorize_d";
        if (Has("rifle")) return "HUD_Rifle_colorize_d";
        if (Has("pistol")) return "HUD_Pistol_colorize_d";
        if (Has("sword")) return "HUD_Sword_Colorize_d";
        if (Has("rocketlauncher")) return "HUD_RocketLauncher_Colorize_d";
        if (Has("bow")) return "HUD_Bow_colorize_d";
        if (Has("primalitemresource_wood")) return "Wood_Icon";
        if (Has("sparkpowder")) return "Sparkpowder_Icon";
        if (Has("primalitemresource_element")) return "TEKElement_Icon";
        if (Has("primalitemresource_gem")) return "Gem2_Icon";
        if (Has("primalitemconsumable_egg")) return "HUD_Egg_Colorize_d";
        return nullptr;
    }

    CachedItemTexture CaptureTexture(SDK::UTexture2D* Icon) {
        CachedItemTexture Result{};
        if (!Icon) return Result;
        const int32_t Index = Icon->Index;
        if (TryGetLiveObjectSerial(Icon, Index, &Result.serial) &&
            !(Icon->Flags & SDK::EObjectFlags::BeginDestroyed) &&
            !(Icon->Flags & SDK::EObjectFlags::FinishDestroyed) &&
            Icon->IsA(SDK::UTexture2D::StaticClass())) {
            Result.texture = Icon;
            Result.index = Index;
        }
        return Result;
    }

    CachedItemTexture CaptureItemTexture(SDK::UPrimalItem* Item) {
        if (!Item) return {};
        SDK::UTexture2D* Icon = Item->GetItemIconJIT();
        if (!Icon) Icon = Item->ItemIcon;
        if (!Icon && Item->Class) {
            if (const char* Name = ItemTextureFallback(Item->Class->GetName()))
                Icon = SDK::UObject::FindObjectFast<SDK::UTexture2D>(Name);
        }
        return CaptureTexture(Icon);
    }

    SDK::UTexture2D* LiveItemTexture(const CachedItemTexture& Ref) {
        int32_t Serial = -1;
        return TryGetLiveObjectSerial(Ref.texture, Ref.index, &Serial) &&
            Serial == Ref.serial &&
            !(Ref.texture->Flags & SDK::EObjectFlags::BeginDestroyed) &&
            !(Ref.texture->Flags & SDK::EObjectFlags::FinishDestroyed)
            ? Ref.texture : nullptr;
    }

    struct ItemQualityVisual {
        std::string name;
        Shadow::Color color{};
    };

    ItemQualityVisual ResolveItemQuality(const SDK::UPrimalItem* Item, SDK::UPrimalGameData* Data) {
        ItemQualityVisual Result{};
        if (!Item) return Result;
        int32_t Serial = -1;
        if (!Data || !TryGetLiveObjectSerial(Data, Data->Index, &Serial) ||
            (Data->Flags & SDK::EObjectFlags::BeginDestroyed) ||
            (Data->Flags & SDK::EObjectFlags::FinishDestroyed)) return Result;
        const auto& Definitions = Data->ItemQualityDefinitions;
        const int Index = Item->ItemQualityIndex;
        if (Definitions.Num() <= 0 || Definitions.Num() > 32 || !Definitions.IsValid() ||
            Index >= Definitions.Num()) return Result;
        const auto& Quality = Definitions[Index];
        if (Quality.QualityName.IsValid()) Result.name = Quality.QualityName.ToString();
        if (Index > 0 && std::isfinite(Quality.QualityColor.R) &&
            std::isfinite(Quality.QualityColor.G) && std::isfinite(Quality.QualityColor.B)) {
            Result.color = {
                std::clamp(Quality.QualityColor.R, 0.0f, 1.0f),
                std::clamp(Quality.QualityColor.G, 0.0f, 1.0f),
                std::clamp(Quality.QualityColor.B, 0.0f, 1.0f), 1.0f};
        }
        return Result;
    }

    bool IsLiveResourcePrimitive(SDK::UPrimitiveComponent* Component,
        int32_t ObjectIndex, int32_t SerialNumber = -1) {
        int32_t LiveSerialNumber = -1;
        if (!TryGetLiveObjectSerial(Component, ObjectIndex, &LiveSerialNumber) ||
            (SerialNumber >= 0 && LiveSerialNumber != SerialNumber)) {
            return false;
        }
        if ((Component->Flags & SDK::EObjectFlags::BeginDestroyed) ||
            (Component->Flags & SDK::EObjectFlags::FinishDestroyed)) {
            return false;
        }
        SDK::UClass* PrimitiveClass = SDK::UPrimitiveComponent::StaticClass();
        return PrimitiveClass && Component->Class && Component->IsA(PrimitiveClass);
    }

    HighlightObjectRef CaptureHighlightObjectRef(SDK::UPrimitiveComponent* Component) {
        HighlightObjectRef Result{};
        if (!Component) return Result;

        const int32_t ObjectIndex = Component->Index;
        int32_t SerialNumber = -1;
        if (!TryGetLiveObjectSerial(Component, ObjectIndex, &SerialNumber) ||
            !IsLiveResourcePrimitive(Component, ObjectIndex, SerialNumber)) {
            return Result;
        }

        Result.Component = Component;
        Result.ObjectIndex = ObjectIndex;
        Result.SerialNumber = SerialNumber;
        return Result;
    }

    g_ESP::Structures::Type ClassifyStructure(SDK::APrimalStructure* Structure,
        const std::string& DisplayName) {
        using namespace g_ESP::Structures;
        // Resolve the concrete blueprint before its generic SupplyCrate parent.
        if (Structure->Class) {
            const std::string Concrete = Structure->Class->GetName();
            if (Concrete.find("BeaverDam") != std::string::npos ||
                Concrete.find("DenLogs") != std::string::npos) return Type::Other;
            if (Classify(Concrete) != Type::Other) return Classify(Concrete);
        }
        // Pickup item identities survive renames and game-language changes.
        SDK::UClass* ItemClass = Structure->PickupGivesItem;
        for (SDK::UClass* Root : {ItemClass, Structure->Class}) {
            auto* Class = Root;
            for (int Depth = 0; Class && Depth < 64; ++Depth, Class = static_cast<SDK::UClass*>(Class->SuperStruct)) {
                const Type Kind = Classify(Class->GetName());
                if (Kind != Type::Other) return Kind;
            }
        }
        return Classify(DisplayName);
    }

    void CollectStructureMeshes(SDK::APrimalStructure* Structure,
        std::vector<HighlightObjectRef>& Meshes) {
        Meshes.clear();
        std::vector<SDK::USceneComponent*> Pending;
        std::unordered_set<SDK::USceneComponent*> Seen;
        const auto Append = [&](SDK::USceneComponent* Component) {
            if (Component && Pending.size() < 256 && Seen.insert(Component).second)
                Pending.push_back(Component);
        };
        Append(Structure->RootComponent);
        Append(Structure->MyStaticMesh);
        Append(Structure->CosmeticVariantStaticMesh);
        const auto AppendOwned = [&](const SDK::TArray<SDK::UActorComponent*>& Components) {
            if (!Components.IsValid() || Components.Num() < 0 || Components.Num() > 256) return;
            for (auto* Component : Components) {
                if (Component && Component->Class && Component->IsA(SDK::USceneComponent::StaticClass()))
                    Append(static_cast<SDK::USceneComponent*>(Component));
            }
        };
        AppendOwned(Structure->BlueprintCreatedComponents);
        AppendOwned(Structure->InstanceComponents);
        for (size_t I = 0; I < Pending.size(); ++I) {
            auto* Component = Pending[I];
            // Snapped structures can be attached here; each owns its own filter.
            if (!Component->Class || Component->Outer != Structure ||
                (Component->Flags & SDK::EObjectFlags::BeginDestroyed) ||
                (Component->Flags & SDK::EObjectFlags::FinishDestroyed)) continue;
            if ((Component->IsA(SDK::UStaticMeshComponent::StaticClass()) ||
                 Component->IsA(SDK::USkeletalMeshComponent::StaticClass())) &&
                Component->bVisible && !Component->bHiddenInGame) {
                const auto Ref = CaptureHighlightObjectRef(static_cast<SDK::UPrimitiveComponent*>(Component));
                if (Ref.Component) Meshes.push_back(Ref);
            }
            const auto& Children = Component->AttachChildren;
            if (Children.IsValid() && Children.Num() > 0 && Children.Num() <= 256)
                for (auto* Child : Children) Append(Child);
        }
    }

    void RestoreSolidHighlight(SDK::UPrimitiveComponent* Component,
        const ModelHighlightState& State) {
        if (State.AppliedOverlay && Component->IsA(SDK::UMeshComponent::StaticClass())) {
            auto* Mesh = static_cast<SDK::UMeshComponent*>(Component);
            if (Mesh->OverlayMaterial == State.AppliedOverlay) {
                int32_t Serial = -1;
                auto* Previous = TryGetLiveObjectSerial(State.OriginalOverlay, State.OriginalOverlayIndex, &Serial) &&
                    Serial == State.OriginalOverlaySerial ? State.OriginalOverlay : nullptr;
                Mesh->SetOverlayMaterial(Previous);
            }
            if (State.AppliedOverlayDistance && Mesh->OverlayMaterialMaxDrawDistance == 0.0f)
                Mesh->SetOverlayMaterialMaxDrawDistance(State.OriginalOverlayDistance);
        }
    }

    void RestoreModelHighlight(SDK::UPrimitiveComponent* Component,
        const ModelHighlightState& State) {
        if (!IsLiveResourcePrimitive(Component, State.ObjectIndex,
            State.SerialNumber)) return;
        RestoreSolidHighlight(Component, State);
        if (Component->CustomDepthStencilValue == State.LastAppliedStencilValue) {
            Component->SetCustomDepthStencilValue(State.OriginalStencilValue);
        }
        if (Component->CustomDepthStencilWriteMask ==
            State.LastAppliedStencilWriteMask) {
            Component->SetCustomDepthStencilWriteMask(
                State.OriginalStencilWriteMask);
        }
        if ((Component->bRenderCustomDepth != 0) ==
            State.LastAppliedRenderCustomDepth) {
            Component->SetRenderCustomDepth(State.OriginalRenderCustomDepth);
        }
    }

    bool HasActiveModelHighlights() {
        return !s_modelHighlightStates.empty();
    }

    bool IsLiveResourceCamera(SDK::AShooterPlayerCameraManager* CameraManager,
        int32_t ObjectIndex, int32_t SerialNumber = -1) {
        int32_t LiveSerialNumber = -1;
        if (!TryGetLiveObjectSerial(CameraManager, ObjectIndex,
            &LiveSerialNumber) ||
            (SerialNumber >= 0 && LiveSerialNumber != SerialNumber)) {
            return false;
        }
        if ((CameraManager->Flags & SDK::EObjectFlags::BeginDestroyed) ||
            (CameraManager->Flags & SDK::EObjectFlags::FinishDestroyed)) return false;
        SDK::UClass* CameraManagerClass = SDK::AShooterPlayerCameraManager::StaticClass();
        return CameraManagerClass && CameraManager->Class && CameraManager->IsA(CameraManagerClass);
    }

    void RestoreFinalCameraEffect(SDK::AShooterPlayerCameraManager* Camera,
        CameraEffectSlot& Slot) {
        if (!Camera || !Slot.Material) { Slot = {}; return; }
        auto& View = Camera->CameraCachePrivate.POV;
        auto& Array = View.PostProcessSettings.WeightedBlendables.Array;
        if (Array.Num() >= 0 && Array.Num() <= 4096 && Array.Max() >= Array.Num()) {
            for (int Index = Array.Num() - 1; Index >= 0; --Index) {
                if (Array[Index].Object != Slot.Material || Array[Index].Weight != 1.0f) continue;
                if (Slot.Added) Array.Remove(Index);
                else Array[Index].Weight = Slot.OriginalWeight;
                break;
            }
        }
        if (Slot.HadBlendWeight && View.PostProcessBlendWeight == 1.0f)
            View.PostProcessBlendWeight = Slot.OriginalBlendWeight;
        Slot = {};
    }

    void ApplyFinalCameraEffect(SDK::AShooterPlayerCameraManager* Camera,
        SDK::UMaterialInterface* Material, CameraEffectSlot& Slot) {
        if (!Camera || !Material || Slot.Material) return;
        auto& View = Camera->CameraCachePrivate.POV;
        auto& Array = View.PostProcessSettings.WeightedBlendables.Array;
        if (Array.Num() < 0 || Array.Num() > 4096 || Array.Max() < Array.Num()) return;
        for (int Index = 0; Index < Array.Num(); ++Index) {
            if (Array[Index].Object != Material) continue;
            Slot.Material = Material;
            Slot.OriginalWeight = Array[Index].Weight;
            Array[Index].Weight = 1.0f;
            break;
        }
        if (!Slot.Material) {
            SDK::FWeightedBlendable Entry{};
            Entry.Object = Material;
            Entry.Weight = 1.0f;
            if (!Array.Add(Entry) && !(GrowWeightedBlendableArray(Array) && Array.Add(Entry))) return;
            Slot.Material = Material;
            Slot.Added = true;
        }
        Slot.HadBlendWeight = View.PostProcessBlendWeight != 1.0f;
        Slot.OriginalBlendWeight = View.PostProcessBlendWeight;
        View.PostProcessBlendWeight = 1.0f;
    }

    std::string LowerMaterialName(std::string Name) {
        for (char& C : Name) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
        return Name;
    }

    SDK::UMaterialInterface* CreateOutlineInstance(SDK::UMaterialInterface* Parent) {
        auto* Dynamic = SDK::UKismetMaterialLibrary::CreateDynamicMaterialInstance(
            s_resourceCameraHighlightState.CameraManager, Parent, {}, SDK::EMIDCreationFlags::None);
        return Dynamic ? static_cast<SDK::UMaterialInterface*>(Dynamic) : Parent;
    }

    void TuneOutlineInstance(SDK::UMaterialInterface* Material) {
        auto& State = s_resourceCameraHighlightState;
        const float Intensity = std::clamp(g_Config::ModelHighlightIntensity, 1.0f, 8.0f);
        const float Opacity = std::clamp(g_Config::ModelHighlightOpacity, 0.25f, 1.0f);
        if (!Material || !Material->IsA(SDK::UMaterialInstanceDynamic::StaticClass()) ||
            (State.AppliedIntensity == Intensity && State.AppliedOpacity == Opacity)) return;
        auto* Dynamic = static_cast<SDK::UMaterialInstanceDynamic*>(Material);
        std::unordered_set<std::string> SeenVectors, SeenScalars;
        auto* Parent = Dynamic->Parent;
        for (int Depth = 0; Parent && Depth < 16 && Parent->IsA(SDK::UMaterialInstance::StaticClass()); ++Depth) {
            auto* Instance = static_cast<SDK::UMaterialInstance*>(Parent);
            if (Instance->VectorParameterValues.Num() >= 0 && Instance->VectorParameterValues.Num() < 2048) {
                for (const auto& Parameter : Instance->VectorParameterValues) {
                    const auto Name = LowerMaterialName(Parameter.ParameterInfo.Name.ToString());
                    if (!SeenVectors.insert(Name).second || Name.find("color") == std::string::npos) continue;
                    auto Color = Parameter.ParameterValue;
                    const float Peak = std::max({Color.R, Color.G, Color.B});
                    if (!std::isfinite(Peak) || Peak <= 0.0f) continue;
                    // Boost saturation as well as brightness to avoid pale
                    // alliance colors. Keep each native alliance hue distinct.
                    Color.R = std::pow(std::max(0.0f, Color.R / Peak), 1.6f) * Intensity;
                    Color.G = std::pow(std::max(0.0f, Color.G / Peak), 1.6f) * Intensity;
                    Color.B = std::pow(std::max(0.0f, Color.B / Peak), 1.6f) * Intensity;
                    Color.A = Opacity;
                    Dynamic->SetVectorParameterValueByInfo(Parameter.ParameterInfo, Color);
                }
            }
            if (Instance->ScalarParameterValues.Num() >= 0 && Instance->ScalarParameterValues.Num() < 2048) {
                for (const auto& Parameter : Instance->ScalarParameterValues) {
                    const auto Name = LowerMaterialName(Parameter.ParameterInfo.Name.ToString());
                    if (!SeenScalars.insert(Name).second) continue;
                    float Value = Parameter.ParameterValue;
                    if (Name.find("opacity") != std::string::npos || Name == "alpha") Value = Opacity;
                    else if (Name.find("intensity") != std::string::npos || Name.find("brightness") != std::string::npos)
                        Value = std::max(Value, 1.0f) * Intensity;
                    else if (Name == "maxdistance" || Name == "fadedistance" || Name == "max distance") Value = 1000000.0f;
                    else continue;
                    Dynamic->SetScalarParameterValueByInfo(Parameter.ParameterInfo, Value);
                }
            }
            Parent = Instance->Parent;
        }
        // These are the actual parameters on ASA's cooked ESP parent. In
        // particular ESPDepth was missed by the generic distance-name search.
        Dynamic->SetScalarParameterValue(SDK::BasicFilesImplUtils::StringToName(L"ESPDepth"), 1000000.0f);
        Dynamic->SetScalarParameterValue(SDK::BasicFilesImplUtils::StringToName(L"ESPFillIntensity"), Intensity * Opacity);
        State.AppliedIntensity = Intensity;
        State.AppliedOpacity = Opacity;
    }

    SDK::UMaterialInterface* FindResourceOutlineMaterial() {
        constexpr const char* AllianceOutlineName = "ESPMaterialOutline_MIC";
        constexpr const wchar_t* AllianceOutlinePath =
            L"/Game/PrimalEarth/Effects/PostProcess/ESPMaterialOutline_MIC.ESPMaterialOutline_MIC";
        SDK::UClass* MaterialClass = SDK::UMaterialInterface::StaticClass();
        if (!MaterialClass) return nullptr;

		const auto Now = std::chrono::steady_clock::now();
		if (Now < s_resourceCameraHighlightState.NextMaterialSearch) return nullptr;
		s_resourceCameraHighlightState.NextMaterialSearch =
			Now + std::chrono::seconds(2);

        // This is ASA's alliance-aware CustomDepth material. The generic
        // PP_ASA_CustomDepthOutline material has a single outline color, which
        // is why hostile targets appeared red while Friendly stencil 0xF9 had
        // no corresponding green result.
        SDK::UObject* Object = SDK::UObject::FindObjectFast<>(AllianceOutlineName);
        if (Object && Object->Class && Object->IsA(MaterialClass)) {
            return CreateOutlineInstance(static_cast<SDK::UMaterialInterface*>(Object));
        }

        // Asset streaming order varies between maps. Retry at the rate limited
        // interval instead of permanently giving up after an early load miss.
        s_resourceCameraHighlightState.OutlineLoadAttempted = true;
        Object = SDK::UVictoryCore::BPLoadObject(SDK::FString(AllianceOutlinePath));
        if (Object && Object->Class && Object->IsA(MaterialClass)) {
            return CreateOutlineInstance(static_cast<SDK::UMaterialInterface*>(Object));
        }
        return nullptr;
    }

    bool EnsureResourceOutlineBlendable(SDK::AShooterPlayerCameraManager* CameraManager,
        bool UpdateDiagnostics = true) {
        if (!CameraManager || CameraManager != s_resourceCameraHighlightState.CameraManager) {
            return false;
        }

        SDK::UMaterialInterface*& OutlineMaterial =
            s_resourceCameraHighlightState.OutlineMaterial;
        int32_t LiveMaterialSerial = -1;
        if (!TryGetLiveObjectSerial(OutlineMaterial,
            s_resourceCameraHighlightState.OutlineMaterialIndex,
            &LiveMaterialSerial) ||
            LiveMaterialSerial !=
                s_resourceCameraHighlightState.OutlineMaterialSerialNumber) {
            OutlineMaterial = FindResourceOutlineMaterial();
            s_resourceCameraHighlightState.AppliedIntensity = -1.0f;
            s_resourceCameraHighlightState.OutlineMaterialIndex = -1;
            s_resourceCameraHighlightState.OutlineMaterialSerialNumber = -1;
            if (OutlineMaterial) {
                const int32_t MaterialIndex = OutlineMaterial->Index;
                if (TryGetLiveObjectSerial(OutlineMaterial, MaterialIndex,
                    &LiveMaterialSerial)) {
                    s_resourceCameraHighlightState.OutlineMaterialIndex = MaterialIndex;
                    s_resourceCameraHighlightState.OutlineMaterialSerialNumber =
                        LiveMaterialSerial;
                }
                else {
                    OutlineMaterial = nullptr;
                }
            }
        }
        if (!OutlineMaterial) {
            if (UpdateDiagnostics) {
                s_resourceDiagnostics.OutlineMaterialStatus = "alliance outline not loaded";
            }
            return false;
        }

        TuneOutlineInstance(OutlineMaterial);
        if (UpdateDiagnostics) {
            s_resourceDiagnostics.OutlineMaterialStatus = OutlineMaterial->GetFullName();
        }
        auto& Blendables = CameraManager->PPMaterials.WeightedBlendables.Array;
        const int Count = Blendables.Num();
        if (Count < 0 || Count > 4096 || Blendables.Max() < Count || Blendables.Max() > 4096) {
            if (UpdateDiagnostics) {
                s_resourceDiagnostics.OutlineMaterialStatus += " (invalid PP array)";
            }
            return false;
        }

        for (int Index = 0; Index < Count; ++Index) {
            if (Blendables[Index].Object != OutlineMaterial) continue;
            if (!s_resourceCameraHighlightState.BlendableAdded &&
                !s_resourceCameraHighlightState.BlendableReplaced &&
                !s_resourceCameraHighlightState.ExistingWeightChanged &&
                Blendables[Index].Weight != 1.0f) {
                s_resourceCameraHighlightState.ExistingWeightChanged = true;
                s_resourceCameraHighlightState.ExistingBlendableIndex = Index;
                s_resourceCameraHighlightState.ExistingBlendableWeight = Blendables[Index].Weight;
                s_resourceCameraHighlightState.ExistingBlendableLastAppliedWeight = 1.0f;
            }
            Blendables[Index].Weight = 1.0f;
            if (UpdateDiagnostics) {
                s_resourceDiagnostics.OutlineMaterialStatus += " (active)";
            }
            return true;
        }

        SDK::FWeightedBlendable OutlineBlendable{};
        OutlineBlendable.Weight = 1.0f;
        OutlineBlendable.Object = OutlineMaterial;
        if (Blendables.Add(OutlineBlendable)) {
            s_resourceCameraHighlightState.BlendableAdded = true;
            s_resourceCameraHighlightState.AddedBlendableIndex = Count;
            if (UpdateDiagnostics) {
                s_resourceDiagnostics.OutlineMaterialStatus += " (added)";
            }
            return true;
        }

        if (GrowWeightedBlendableArray(Blendables) &&
            Blendables.Add(OutlineBlendable)) {
            s_resourceCameraHighlightState.BlendableAdded = true;
            s_resourceCameraHighlightState.AddedBlendableIndex = Count;
            if (UpdateDiagnostics) {
                s_resourceDiagnostics.OutlineMaterialStatus +=
                    " (added after UE array growth)";
            }
            return true;
        }

        if (Count > 0 && !s_resourceCameraHighlightState.BlendableReplaced) {
            int ReplacementIndex = -1;
            for (int Index = Count - 1; Index >= 0; --Index) {
                if (!Blendables[Index].Object || Blendables[Index].Weight <= 0.0001f) {
                    ReplacementIndex = Index;
                    break;
                }
            }
            if (ReplacementIndex >= 0) {
                s_resourceCameraHighlightState.BlendableReplaced = true;
                s_resourceCameraHighlightState.ReplacedBlendableIndex = ReplacementIndex;
                s_resourceCameraHighlightState.ReplacedBlendable = Blendables[ReplacementIndex];
                Blendables[ReplacementIndex] = OutlineBlendable;
                if (UpdateDiagnostics) {
                    s_resourceDiagnostics.OutlineMaterialStatus += " (reused inactive slot)";
                }
                return true;
            }
        }

        if (UpdateDiagnostics) {
            s_resourceDiagnostics.OutlineMaterialStatus +=
                s_fMemoryReallocResolveState ==
                    FMemoryReallocResolveState::Unavailable
                ? " (UE allocator signature unavailable)"
                : " (UE array growth failed)";
        }
        return false;
    }

    void ClearResourceOutlineBlendableMutationState() {
        s_resourceCameraHighlightState.BlendableAdded = false;
        s_resourceCameraHighlightState.AddedBlendableIndex = -1;
        s_resourceCameraHighlightState.BlendableReplaced = false;
        s_resourceCameraHighlightState.ReplacedBlendableIndex = -1;
        s_resourceCameraHighlightState.ReplacedBlendable = {};
        s_resourceCameraHighlightState.ExistingWeightChanged = false;
        s_resourceCameraHighlightState.ExistingBlendableIndex = -1;
        s_resourceCameraHighlightState.ExistingBlendableWeight = 0.0f;
        s_resourceCameraHighlightState.ExistingBlendableLastAppliedWeight = 0.0f;
    }

    void RestoreResourceOutlineBlendable(SDK::AShooterPlayerCameraManager* CameraManager) {
        RestoreFinalCameraEffect(CameraManager, s_resourceCameraHighlightState.FinalCameraSlot);
        if (!CameraManager || !s_resourceCameraHighlightState.OutlineMaterial) {
            ClearResourceOutlineBlendableMutationState();
            return;
        }
        auto& Blendables = CameraManager->PPMaterials.WeightedBlendables.Array;
        const int Count = Blendables.Num();
        if (Count < 0 || Count > 4096) {
            ClearResourceOutlineBlendableMutationState();
            return;
        }

        const auto FindOwnedOutlineIndex = [&](int PreferredIndex,
            float ExpectedWeight) {
                if (PreferredIndex >= 0 && PreferredIndex < Blendables.Num() &&
                    Blendables[PreferredIndex].Object ==
                        s_resourceCameraHighlightState.OutlineMaterial &&
                    Blendables[PreferredIndex].Weight == ExpectedWeight) {
                    return PreferredIndex;
                }
                int UniqueMatch = -1;
                int MatchCount = 0;
                for (int SearchIndex = 0; SearchIndex < Blendables.Num();
                    ++SearchIndex) {
                    if (Blendables[SearchIndex].Object ==
                            s_resourceCameraHighlightState.OutlineMaterial &&
                        Blendables[SearchIndex].Weight == ExpectedWeight) {
                        UniqueMatch = SearchIndex;
                        ++MatchCount;
                    }
                }
                return MatchCount == 1 ? UniqueMatch : -1;
            };

        if (s_resourceCameraHighlightState.BlendableReplaced) {
            const int Index = FindOwnedOutlineIndex(
                s_resourceCameraHighlightState.ReplacedBlendableIndex, 1.0f);
            if (Index >= 0) {
                Blendables[Index] = s_resourceCameraHighlightState.ReplacedBlendable;
            }
        }
        if (s_resourceCameraHighlightState.ExistingWeightChanged) {
            const int Index = FindOwnedOutlineIndex(
                s_resourceCameraHighlightState.ExistingBlendableIndex,
                s_resourceCameraHighlightState.ExistingBlendableLastAppliedWeight);
            if (Index >= 0) {
                Blendables[Index].Weight =
                    s_resourceCameraHighlightState.ExistingBlendableWeight;
            }
        }
        if (s_resourceCameraHighlightState.BlendableAdded) {
            // Native UpdateCamera may rebuild/reorder PPMaterials. The exact
            // material was absent immediately before our Add, so a single
            // match remains owned by us even if its index moved.
            const int RemovalIndex = FindOwnedOutlineIndex(
                s_resourceCameraHighlightState.AddedBlendableIndex, 1.0f);
            if (RemovalIndex >= 0) Blendables.Remove(RemovalIndex);
        }

        ClearResourceOutlineBlendableMutationState();
    }

    void PrepareOwnedCameraBool(bool& Field, OwnedCameraBool& State) {
        // Remove only the value still owned by the previous frame, then let
        // native UpdateCamera rebuild the natural Tek/buff state from scratch.
        if (State.DidWrite && Field == State.LastApplied) {
            Field = State.Baseline;
        }
        State = {};
    }

    void ApplyOwnedCameraBool(bool& Field, OwnedCameraBool& State,
        bool DesiredValue) {
        if (State.DidWrite && Field == State.LastApplied) {
            Field = DesiredValue;
            State.LastApplied = DesiredValue;
            return;
        }
        State.Baseline = Field;
        State.LastApplied = Field;
        State.DidWrite = false;

        // If ASA produced the desired value by itself during this camera
        // update, do not claim it as one of our writes.
        if (Field == DesiredValue) {
            return;
        }
        Field = DesiredValue;
        State.LastApplied = DesiredValue;
        State.DidWrite = true;
    }

    void RestoreOwnedCameraBool(bool& Field, OwnedCameraBool& State) {
        if (State.DidWrite && Field == State.LastApplied) {
            Field = State.Baseline;
        }
        State = {};
    }

    void RestoreResourceCameraHighlight() {
        if (IsLiveResourceCamera(s_resourceCameraHighlightState.CameraManager,
            s_resourceCameraHighlightState.ObjectIndex,
            s_resourceCameraHighlightState.SerialNumber)) {
            SDK::AShooterPlayerCameraManager* CameraManager =
                s_resourceCameraHighlightState.CameraManager;
            RestoreOwnedCameraBool(CameraManager->bHasCustomDepthStencil,
                s_resourceCameraHighlightState.HasCustomDepthStencilOverride);
            RestoreOwnedCameraBool(CameraManager->bCompleteCustomDepthStencilOverride,
                s_resourceCameraHighlightState.CompleteStencilOverride);
            RestoreOwnedCameraBool(CameraManager->bCustomDepthStencilIgnoreHealth,
                s_resourceCameraHighlightState.IgnoreHealthOverride);
            RestoreResourceOutlineBlendable(CameraManager);
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

    bool IsLiveResourceViewport(SDK::UGameViewportClient* ViewportClient,
        int32_t ObjectIndex) {
        if (!ViewportClient || ObjectIndex < 0 ||
            ObjectIndex >= SDK::UObject::GObjects->Num() ||
            SDK::UObject::GObjects->GetByIndex(ObjectIndex) != ViewportClient) {
            return false;
        }
        SDK::UClass* ViewportClass = SDK::UGameViewportClient::StaticClass();
        return ViewportClass && ViewportClient->Class && ViewportClient->IsA(ViewportClass);
    }

    bool IsLiveModelHighlightWorld(SDK::UWorld* World, int32_t ObjectIndex) {
        if (!World || ObjectIndex < 0 ||
            ObjectIndex >= SDK::UObject::GObjects->Num() ||
            SDK::UObject::GObjects->GetByIndex(ObjectIndex) != World) {
            return false;
        }
        SDK::UClass* WorldClass = SDK::UWorld::StaticClass();
        return WorldClass && World->Class && World->IsA(WorldClass);
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

        SDK::UWorld* CommandWorld =
            IsLiveModelHighlightWorld(s_resourceRenderPipelineState.World,
                s_resourceRenderPipelineState.WorldObjectIndex)
            ? s_resourceRenderPipelineState.World : nullptr;
        if (s_resourceRenderPipelineState.HasCustomDepthMode && CommandWorld &&
            SDK::UKismetSystemLibrary::GetConsoleVariableIntValue(
                SDK::FString(L"r.CustomDepth")) == 3) {
            const std::wstring Command = L"r.CustomDepth " +
                std::to_wstring(s_resourceRenderPipelineState.CustomDepthMode);
            SDK::UKismetSystemLibrary::ExecuteConsoleCommand(
                CommandWorld, SDK::FString(Command.c_str()), nullptr);
        }
        if (s_resourceRenderPipelineState.HasNaniteCustomDepthMode && CommandWorld &&
            SDK::UKismetSystemLibrary::GetConsoleVariableIntValue(
                SDK::FString(L"r.Nanite.CustomDepth")) == 1) {
            const std::wstring Command = L"r.Nanite.CustomDepth " +
                std::to_wstring(s_resourceRenderPipelineState.NaniteCustomDepthMode);
            SDK::UKismetSystemLibrary::ExecuteConsoleCommand(
                CommandWorld, SDK::FString(Command.c_str()), nullptr);
        }

        s_resourceRenderPipelineState = {};
        s_resourceDiagnostics.CustomDepthMode = -1;
        s_resourceDiagnostics.NaniteCustomDepthMode = -1;
        s_resourceDiagnostics.PostProcessingEnabled = false;
        s_resourceDiagnostics.PreRenderWrites = 0;
    }

    void ApplyResourceRenderPipeline(SDK::UWorld* World) {
        if (!World || !HasActiveModelHighlights()) {
            RestoreResourceRenderPipeline();
            return;
        }

        s_resourceRenderPipelineState.World = World;
        s_resourceRenderPipelineState.WorldObjectIndex = World->Index;

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
        if (!HasActiveModelHighlights() ||
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
            s_resourceCameraHighlightState.CameraManager = CameraManager;
            s_resourceCameraHighlightState.ObjectIndex = CameraManager->Index;
            TryGetLiveObjectSerial(CameraManager, CameraManager->Index,
                &s_resourceCameraHighlightState.SerialNumber);
        }

        // Keep the material resident and expose diagnostics here. The actual
        // per-frame injection is repeated before UpdateCamera so it participates
        // in the scene render rather than arriving one PostRender too late.
        EnsureResourceOutlineBlendable(CameraManager);
        RestoreResourceOutlineBlendable(CameraManager);
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

    SDK::UMaterialInstanceDynamic* GetSolidHighlightMaterial(uint8_t Stencil) {
        struct CachedFill {
            SDK::UMaterialInstanceDynamic* Material = nullptr;
            int32_t Index = -1, Serial = -1;
            float Intensity = -1.0f, Opacity = -1.0f;
            std::chrono::steady_clock::time_point Retry{};
        };
        static std::array<CachedFill, 5> Materials{};
        // Match the game's alliance stencil palette: friendly green, NPC
        // white, fleeing yellow, hostile red, and element blue.
        const int ColorIndex = Stencil == 0xF9 ? 0 : Stencil == 0xFA ? 1 :
            Stencil == 0xFB ? 2 : Stencil == 0xFE ? 4 : 3;
        auto& Cache = Materials[ColorIndex];
        int32_t Serial = -1;
        if (!TryGetLiveObjectSerial(Cache.Material, Cache.Index, &Serial) || Serial != Cache.Serial) {
            Cache.Material = nullptr;
            const auto Now = std::chrono::steady_clock::now();
            if (Now < Cache.Retry) return nullptr;
            Cache.Retry = Now + std::chrono::seconds(5);
            auto* Object = SDK::UObject::FindObjectFast<>("StructurePreviewMat");
            if (!Object) Object = SDK::UVictoryCore::BPLoadObject(
                SDK::FString(L"/Game/PrimalEarth/Structures/StructurePreviewMat.StructurePreviewMat"));
            if (!Object || !Object->IsA(SDK::UMaterial::StaticClass())) return nullptr;
            auto* Parent = static_cast<SDK::UMaterial*>(Object);
            if (Parent->MaterialDomain != SDK::EMaterialDomain::MD_Surface ||
                (!Parent->bUsedAsSpecialEngineMaterial && !Parent->bUsedWithSkeletalMesh)) return nullptr;
            // StructurePreviewMat is a cooked translucent material with the
            // skeletal usage permutation. Its ColorParam name is also exposed
            // by PrimalStructure::PreviewMaterialColorParamName on the CDO.
            // Never manufacture missing shader permutations by changing flags.
            Cache.Material = SDK::UKismetMaterialLibrary::CreateDynamicMaterialInstance(
                s_resourceCameraHighlightState.CameraManager, Parent, {}, SDK::EMIDCreationFlags::None);
            if (!Cache.Material) return nullptr;
            Cache.Index = Cache.Material->Index;
            TryGetLiveObjectSerial(Cache.Material, Cache.Index, &Cache.Serial);
            Cache.Intensity = Cache.Opacity = -1.0f;
        }
        const float Intensity = std::clamp(g_Config::ModelHighlightIntensity, 1.0f, 8.0f);
        const float Opacity = std::clamp(g_Config::ModelHighlightOpacity, 0.25f, 1.0f);
        if (Cache.Intensity != Intensity || Cache.Opacity != Opacity) {
            SDK::FLinearColor Color{};
            // Limit exposure so tonemapping does not wash a bright red to pink.
            const float Brightness = std::min(Intensity, 2.0f);
            Color.R = (ColorIndex == 1 || ColorIndex == 2 || ColorIndex == 3) ? Brightness : 0.0f;
            Color.G = (ColorIndex == 0 || ColorIndex == 1 || ColorIndex == 2) ? Brightness : 0.0f;
            Color.B = (ColorIndex == 1 || ColorIndex == 4) ? Brightness : 0.0f;
            Color.A = Opacity;
            Cache.Material->SetVectorParameterValue(SDK::BasicFilesImplUtils::StringToName(L"ColorParam"), Color);
            Cache.Intensity = Intensity;
            Cache.Opacity = Opacity;
        }
        return Cache.Material;
    }

    void ApplySolidHighlight(SDK::UPrimitiveComponent* Component, uint8_t Stencil, ModelHighlightState& State) {
        if (!Component->IsA(SDK::UMeshComponent::StaticClass())) return;
        auto* Material = GetSolidHighlightMaterial(Stencil);
        if (!Material) return;
        auto* Mesh = static_cast<SDK::UMeshComponent*>(Component);
        if (!State.AppliedOverlay || Mesh->OverlayMaterial != State.AppliedOverlay) {
            State.OriginalOverlay = Mesh->OverlayMaterial;
            State.OriginalOverlayIndex = State.OriginalOverlay ? State.OriginalOverlay->Index : -1;
            TryGetLiveObjectSerial(State.OriginalOverlay, State.OriginalOverlayIndex, &State.OriginalOverlaySerial);
        }
        if (!State.AppliedOverlayDistance || Mesh->OverlayMaterialMaxDrawDistance != 0.0f) {
            State.OriginalOverlayDistance = Mesh->OverlayMaterialMaxDrawDistance;
            State.AppliedOverlayDistance = true;
        }
        if (Mesh->OverlayMaterial != Material) Mesh->SetOverlayMaterial(Material);
        if (Mesh->OverlayMaterialMaxDrawDistance != 0.0f) Mesh->SetOverlayMaterialMaxDrawDistance(0.0f);
        State.AppliedOverlay = Material;
    }

    void ApplyCombinedModelHighlights() {
        static std::unordered_map<SDK::UPrimitiveComponent*, DesiredModelHighlight>
            DesiredHighlights;
        DesiredHighlights.clear();
        DesiredHighlights.reserve(
            s_resourceDesiredHighlights.size() + s_actorDesiredHighlights.size());
        for (const auto& [Component, Highlight] : s_resourceDesiredHighlights) {
            DesiredHighlights[Component] = Highlight;
        }
        for (const auto& [Component, Highlight] : s_actorDesiredHighlights) {
            DesiredHighlights[Component] = Highlight;
        }

        for (auto Iterator = s_modelHighlightStates.begin();
            Iterator != s_modelHighlightStates.end();) {
            const auto DesiredIterator = DesiredHighlights.find(Iterator->first);
            const bool SameObject = DesiredIterator != DesiredHighlights.end() &&
                DesiredIterator->second.ObjectIndex == Iterator->second.ObjectIndex &&
                DesiredIterator->second.SerialNumber == Iterator->second.SerialNumber;
            if (!SameObject || !IsLiveResourcePrimitive(Iterator->first,
                Iterator->second.ObjectIndex, Iterator->second.SerialNumber)) {
                RestoreModelHighlight(Iterator->first, Iterator->second);
                Iterator = s_modelHighlightStates.erase(Iterator);
            }
            else {
                ++Iterator;
            }
        }

        for (const auto& [Component, DesiredHighlight] : DesiredHighlights) {
            if (!IsLiveResourcePrimitive(Component, DesiredHighlight.ObjectIndex,
                DesiredHighlight.SerialNumber)) continue;

            auto [Iterator, Inserted] = s_modelHighlightStates.try_emplace(Component);
            ModelHighlightState& State = Iterator->second;
            if (Inserted) {
                State.ObjectIndex = DesiredHighlight.ObjectIndex;
                State.SerialNumber = DesiredHighlight.SerialNumber;
                State.OriginalRenderCustomDepth = Component->bRenderCustomDepth != 0;
                State.OriginalStencilValue = Component->CustomDepthStencilValue;
                State.OriginalStencilWriteMask = Component->CustomDepthStencilWriteMask;
                State.LastAppliedRenderCustomDepth = State.OriginalRenderCustomDepth;
                State.LastAppliedStencilValue = State.OriginalStencilValue;
                State.LastAppliedStencilWriteMask = State.OriginalStencilWriteMask;
            }
            else {
                // If the game or another effect changed a field after our last
                // write, adopt it as the new baseline instead of overwriting it
                // when this highlight is later released.
                if ((Component->bRenderCustomDepth != 0) !=
                    State.LastAppliedRenderCustomDepth) {
                    State.OriginalRenderCustomDepth = Component->bRenderCustomDepth != 0;
                }
                if (Component->CustomDepthStencilValue !=
                    State.LastAppliedStencilValue) {
                    State.OriginalStencilValue = Component->CustomDepthStencilValue;
                }
                if (Component->CustomDepthStencilWriteMask !=
                    State.LastAppliedStencilWriteMask) {
                    State.OriginalStencilWriteMask =
                        Component->CustomDepthStencilWriteMask;
                }
            }

            if (Component->CustomDepthStencilValue != DesiredHighlight.StencilValue) {
                Component->SetCustomDepthStencilValue(DesiredHighlight.StencilValue);
            }
            if (Component->CustomDepthStencilWriteMask !=
                SDK::ERendererStencilMask::ERSM_255) {
                Component->SetCustomDepthStencilWriteMask(
                    SDK::ERendererStencilMask::ERSM_255);
            }
            if (!Component->bRenderCustomDepth) {
                Component->SetRenderCustomDepth(true);
            }
            if (DesiredHighlight.SolidFill || Component->IsA(SDK::USkeletalMeshComponent::StaticClass())) {
                ApplySolidHighlight(Component, DesiredHighlight.StencilValue, State);
            }
            else if (State.AppliedOverlay) {
                RestoreSolidHighlight(Component, State);
                State.AppliedOverlay = nullptr;
                State.AppliedOverlayDistance = false;
            }
            State.LastAppliedRenderCustomDepth = true;
            State.LastAppliedStencilValue = DesiredHighlight.StencilValue;
            State.LastAppliedStencilWriteMask = SDK::ERendererStencilMask::ERSM_255;
        }

        s_resourceDiagnostics.HighlightedComponents =
            static_cast<int>(s_modelHighlightStates.size());
        s_resourceDiagnostics.SolidFillComponents = 0;
        for (const auto& [Component, State] : s_modelHighlightStates)
            if (State.AppliedOverlay) ++s_resourceDiagnostics.SolidFillComponents;
    }

    void RestoreSharedModelHighlightPipelineIfUnused() {
        if (HasActiveModelHighlights()) return;
        RestoreResourceCameraHighlight();
        RestoreResourceRenderPipeline();
    }

    void RestoreAllResourceHighlights() {
        if (s_resourceDesiredHighlights.empty()) {
            RestoreSharedModelHighlightPipelineIfUnused();
            s_resourceDiagnostics.StencilValue = 0;
            return;
        }
        s_resourceDesiredHighlights.clear();
        ApplyCombinedModelHighlights();
        RestoreSharedModelHighlightPipelineIfUnused();
        if (s_resourceDesiredHighlights.empty()) {
            s_resourceDiagnostics.StencilValue = 0;
        }
    }

    void RestoreAllActorHighlights() {
        if (s_actorDesiredHighlights.empty()) {
            RestoreSharedModelHighlightPipelineIfUnused();
            return;
        }
        s_actorDesiredHighlights.clear();
        ApplyCombinedModelHighlights();
        RestoreSharedModelHighlightPipelineIfUnused();
    }

    void ApplyResourceHighlights(const std::vector<ResourceMarker>& Markers) {
        s_resourceDesiredHighlights.clear();

        constexpr uint8_t ElementStencil =
            EncodeFullHealthStencil(SDK::EStencilAlliance::Element);
        if (g_Config::bResourceModelHighlight) {
            for (const ResourceMarker& Marker : Markers) {
                for (const HighlightObjectRef& ComponentRef : Marker.HighlightComponents) {
                    if (ComponentRef.Component) {
                        s_resourceDesiredHighlights[ComponentRef.Component] = {
                            ComponentRef.ObjectIndex,
                            ComponentRef.SerialNumber,
                            ElementStencil
                        };
                    }
                }
            }
        }

        s_resourceDiagnostics.StencilValue = s_resourceDesiredHighlights.empty()
            ? 0 : ElementStencil;
        ApplyCombinedModelHighlights();
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
            !g_Config::bResourcePearl && !g_Config::bResourceElementShard) {
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
                                HighlightObjectRef ComponentRef = CaptureHighlightObjectRef(
                                    static_cast<SDK::UPrimitiveComponent*>(
                                        static_cast<SDK::UMeshComponent*>(MeshComponent)));
                                if (ComponentRef.Component) {
                                    Marker.HighlightComponents.push_back(ComponentRef);
                                }
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
                const bool NeedSelectedShardSample =
                    g_Config::bResourceElementShard &&
                    ComponentType != ResourceType::ElementShard;
                if ((!IsResourceTypeNeeded(ComponentType) || NeedSelectedShardSample) &&
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
                    HighlightObjectRef ComponentRef = CaptureHighlightObjectRef(Component);
                    if (ComponentRef.Component) {
                        Marker.HighlightComponents.push_back(ComponentRef);
                    }
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

    std::vector<ResourceTargetCandidate> FindResourceTargetCandidates(
        const SDK::FVector& Origin, float MaxDistanceMeters, size_t Limit,
        const std::vector<SDK::FVector>& ExcludedLocations, float ExclusionRadiusMeters,
        const SDK::FVector* RequiredForward) {
        if (!Limit || !std::isfinite(MaxDistanceMeters) || !std::isfinite(ExclusionRadiusMeters) ||
            !IsFiniteResourceVector(Origin) || s_resourceMarkers.empty()) {
            return {};
        }

        const double MaximumUU = std::clamp(static_cast<double>(MaxDistanceMeters), 1.0, 1000.0) * 100.0;
        const double MaximumSquared = MaximumUU * MaximumUU;
        const double ExclusionUU = std::max(0.0, static_cast<double>(ExclusionRadiusMeters)) * 100.0;
        const double ExclusionSquared = ExclusionUU * ExclusionUU;
        std::vector<ResourceTargetCandidate> Result;
        Limit = std::min<size_t>(Limit, 16);

        for (const ResourceMarker& Marker : s_resourceMarkers) {
            if (!IsFiniteResourceVector(Marker.Location) ||
                !IsResourceTypeEnabled(Marker.Type)) continue;
            bool Excluded = false;
            if (ExclusionSquared > 0.0) {
                for (const SDK::FVector& ExcludedLocation : ExcludedLocations) {
                    if (IsFiniteResourceVector(ExcludedLocation) &&
                        ResourceDistanceSquared(Marker.Location, ExcludedLocation) <= ExclusionSquared) {
                        Excluded = true;
                        break;
                    }
                }
            }
            if (Excluded) continue;
            const double DistanceSquared = ResourceDistanceSquared(Marker.Location, Origin);
            if (!std::isfinite(DistanceSquared) || DistanceSquared > MaximumSquared) continue;
            if (RequiredForward) {
                if (!IsFiniteResourceVector(*RequiredForward) || DistanceSquared <= 1) continue;
                const auto Delta = Marker.Location - Origin;
                const double Dot = (Delta.X * RequiredForward->X + Delta.Y * RequiredForward->Y +
                    Delta.Z * RequiredForward->Z) / std::sqrt(DistanceSquared);
                if (Dot < 0.10) continue;
            }
            Result.push_back({Marker.Location, static_cast<float>(std::sqrt(DistanceSquared) * 0.01),
                GetResourceTypeLabel(Marker.Type)});
        }
        const auto Nearer = [](const ResourceTargetCandidate& A, const ResourceTargetCandidate& B) {
            if (A.DistanceMeters != B.DistanceMeters) return A.DistanceMeters < B.DistanceMeters;
            if (A.Location.X != B.Location.X) return A.Location.X < B.Location.X;
            if (A.Location.Y != B.Location.Y) return A.Location.Y < B.Location.Y;
            return A.Location.Z < B.Location.Z;
        };
        std::partial_sort(Result.begin(), Result.begin() + std::min(Limit, Result.size()), Result.end(), Nearer);
        if (Result.size() > Limit) Result.resize(Limit);
        return Result;
    }

    bool FindNearestResourceTarget(const SDK::FVector& Origin, float MaxDistanceMeters,
        SDK::FVector* OutLocation, float* OutDistanceMeters, std::string* OutTypeName,
        const std::vector<SDK::FVector>* ExcludedLocations, float ExclusionRadiusMeters) {
        if (!OutLocation) return false;
        const auto Candidates = FindResourceTargetCandidates(Origin, MaxDistanceMeters, 1,
            ExcludedLocations ? *ExcludedLocations : std::vector<SDK::FVector>{}, ExclusionRadiusMeters);
        if (Candidates.empty()) return false;
        const auto& Best = Candidates.front();
        *OutLocation = Best.Location;
        if (OutDistanceMeters) *OutDistanceMeters = Best.DistanceMeters;
        if (OutTypeName) *OutTypeName = Best.TypeName;
        return true;
    }

    bool FindResourceTargetNear(const SDK::FVector& ExpectedLocation, float ToleranceMeters,
        SDK::FVector* OutLocation, std::string* OutTypeName) {
        if (!OutLocation || !IsFiniteResourceVector(ExpectedLocation) || s_resourceMarkers.empty()) {
            return false;
        }

        const double ToleranceUU = std::clamp(static_cast<double>(ToleranceMeters), 0.1, 10.0) * 100.0;
        const double ToleranceSquared = ToleranceUU * ToleranceUU;
        const ResourceMarker* Best = nullptr;
        double BestDistanceSquared = ToleranceSquared;
        for (const ResourceMarker& Marker : s_resourceMarkers) {
            if (!IsFiniteResourceVector(Marker.Location) ||
                !IsResourceTypeEnabled(Marker.Type)) continue;
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
        RestoreAllActorHighlights();
    }

    bool PrepareModelHighlightCameraUpdate(
        SDK::AShooterPlayerCameraManager* CameraManager) {
        if (!HasActiveModelHighlights() ||
            !IsLiveResourceCamera(CameraManager,
                s_resourceCameraHighlightState.ObjectIndex,
                s_resourceCameraHighlightState.SerialNumber) ||
            CameraManager != s_resourceCameraHighlightState.CameraManager) {
            return false;
        }

        ApplyOwnedCameraBool(CameraManager->bHasCustomDepthStencil,
            s_resourceCameraHighlightState.HasCustomDepthStencilOverride, true);
        ApplyOwnedCameraBool(CameraManager->bCompleteCustomDepthStencilOverride,
            s_resourceCameraHighlightState.CompleteStencilOverride, true);
        ApplyOwnedCameraBool(CameraManager->bCustomDepthStencilIgnoreHealth,
            s_resourceCameraHighlightState.IgnoreHealthOverride, true);
        RestoreResourceOutlineBlendable(CameraManager);
        ApplyCombinedModelHighlights();
        s_resourceCameraHighlightState.BlendablePreparedForCameraUpdate =
            EnsureResourceOutlineBlendable(CameraManager, false);
        return s_resourceCameraHighlightState.BlendablePreparedForCameraUpdate;
    }

    void FinishModelHighlightCameraUpdate(
        SDK::AShooterPlayerCameraManager* CameraManager) {
        if (!HasActiveModelHighlights() ||
            CameraManager != s_resourceCameraHighlightState.CameraManager ||
            !IsLiveResourceCamera(CameraManager,
                s_resourceCameraHighlightState.ObjectIndex,
                s_resourceCameraHighlightState.SerialNumber)) {
            s_resourceCameraHighlightState.BlendablePreparedForCameraUpdate = false;
            return;
        }

        if (s_resourceCameraHighlightState.BlendablePreparedForCameraUpdate) {
            // UpdateCamera rebuilds these values every frame. The alliance ESP
            // material normally makes bHas true; retain a fallback for maps
            // where the material metadata is streamed late. CompleteOverride
            // prevents ASA from replacing our chosen 0xF9/0xFC stencil values.
            ApplyOwnedCameraBool(CameraManager->bHasCustomDepthStencil,
                s_resourceCameraHighlightState.HasCustomDepthStencilOverride, true);
            ApplyOwnedCameraBool(CameraManager->bCompleteCustomDepthStencilOverride,
                s_resourceCameraHighlightState.CompleteStencilOverride, true);
            ApplyOwnedCameraBool(CameraManager->bCustomDepthStencilIgnoreHealth,
                s_resourceCameraHighlightState.IgnoreHealthOverride, true);
        }

        // Native camera processing may have rewritten both CustomDepth and the
        // stencil. Restore the desired values in the same pre-render frame.
        ApplyCombinedModelHighlights();
        // UpdateCamera has already copied/consumed PPMaterials into this frame's
        // camera cache. Release only the exact temporary array mutation now.
        RestoreResourceOutlineBlendable(CameraManager);
        ApplyFinalCameraEffect(CameraManager, s_resourceCameraHighlightState.OutlineMaterial,
            s_resourceCameraHighlightState.FinalCameraSlot);
        s_resourceCameraHighlightState.BlendablePreparedForCameraUpdate = false;
        s_resourceDiagnostics.CameraStencilEnabled =
            CameraManager->bHasCustomDepthStencil;
        if (s_resourceDiagnostics.PreRenderWrites < 999999) {
            ++s_resourceDiagnostics.PreRenderWrites;
        }
    }

    void MaintainResourceModelHighlightState() {
        if (!HasActiveModelHighlights()) return;

        if (IsLiveResourceCamera(s_resourceCameraHighlightState.CameraManager,
            s_resourceCameraHighlightState.ObjectIndex,
            s_resourceCameraHighlightState.SerialNumber)) {
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

    }

    void DrawResourceESP(SDK::UCanvas* Canvas) {
        // Resource ESP is independent of the player/dino ESP master switch.
        const bool DrawResourceVisuals = g_Config::bResourceESPEnabled && !g_Config::bCombatESP;
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
            if (!g_ESP::ProjectToCanvas(Canvas, LocalPC, Marker.Location, &ScreenPosition)) continue;
            const float ScreenX = static_cast<float>(ScreenPosition.X);
            const float ScreenY = static_cast<float>(ScreenPosition.Y);
            if (ScreenX < 0.0f || ScreenX >= Canvas->ClipX ||
                ScreenY < 0.0f || ScreenY >= Canvas->ClipY) continue;

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

#include "FeatureExpansionESP.inl"

    #include "IndependentOverlay.inl"

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

        const bool CharacterVisuals = (g_Config::bESPPlayers || g_Config::bESPCreatures) &&
            (g_Config::bModelHighlightEnabled || g_Config::bShowCreatureAggro ||
             g_Config::bDrawBox || g_Config::bDrawHealthBar ||
             g_Config::bDrawName || g_Config::bDrawDistance || g_Config::bDrawTorpor ||
             g_Config::bDrawRagdoll || g_Config::bEnableOOF || g_Config::bMinimalESP || g_Config::bRadar ||
             g_Config::bDrawArmorQuality || g_Config::bDrawHeldWeapon);
        if (!g_Config::bOverlayESP && !CharacterVisuals && !g_Config::bCombatESP && !g_Config::bDrawTurretAmmo && !g_Config::bDrawStructures && !g_Config::bDrawDroppedItems && !g_Config::bDrawWater) {
            s_overlayRigs.clear();
            RestoreAllActorHighlights();
            s_entries.clear();
            waterCandidates.clear();
            return;
        }

        SDK::UWorld* World = _DD::World;
        if (!World || !World->GameState) {
            RestoreAllActorHighlights();
            return;
        }

        SDK::APlayerController* LocalPC = _DD::LocalSPC;
        if (!LocalPC || !LocalPC->Pawn) {
            RestoreAllActorHighlights();
            for (auto& kv : s_entries) {
                kv.second.targetAlpha = 0.0f;
                kv.second.aliveThisFrame = false;
            }
            return;
        }

        SDK::AShooterPlayerState* LocalPS = _DD::LocalSPS;
        if (!LocalPS) {
            RestoreAllActorHighlights();
            for (auto& kv : s_entries) {
                kv.second.targetAlpha = 0.0f;
                kv.second.aliveThisFrame = false;
            }
            return;
        }

        Shadow::ShadowIO& IO = Shadow::GetIO();

        const float screenW = Canvas->ClipX;
        const float screenH = Canvas->ClipY;
        const float deltaTime = IO.DeltaTime;

        SDK::APrimalCharacter* LocalChar = _DD::LocalSC;
        if (!LocalChar && LocalPC->Pawn && LocalPC->Pawn->Class &&
            LocalPC->Pawn->IsA(SDK::APrimalCharacter::StaticClass())) {
            LocalChar = static_cast<SDK::APrimalCharacter*>(LocalPC->Pawn);
        }
        TryAttachSpyglassInventory(LocalChar);
        if (!g_Config::bStructureShowLootContents) {
            std::lock_guard<std::mutex> Guard(s_spyglassLootMutex);
            s_spyglassLoot.clear();
            s_spyglassPending.clear();
        }

        // ASA streams actors across multiple ULevels. Rebuilding and classifying
        // the complete world list every render frame is expensive, so retain a
        // typed registry for 200 ms. Every pointer is still validated through
        // GObjects before it is dereferenced, making the short cache GC-safe.
		const auto ActorRefreshNow = std::chrono::steady_clock::now();
		if (s_cachedESPWorld != World || ActorRefreshNow >= s_nextESPActorRefresh) {
			s_cachedESPWorld = World;
			s_nextESPActorRefresh = ActorRefreshNow + std::chrono::milliseconds(200);
			s_cachedESPActors.clear();

			const int32_t PersistentActorCount = World->PersistentLevel
				? World->PersistentLevel->Actors.Num() : 0;
			if (PersistentActorCount > 0 && PersistentActorCount <= 200000) {
				s_cachedESPActors.reserve(static_cast<size_t>(PersistentActorCount));
			}

			auto AppendLevelActors = [&](SDK::ULevel* Level) {
				if (!Level) return;
				const int32_t Count = Level->Actors.Num();
				if (Count < 0 || Count > 200000) return;
				for (int32_t ActorIndex = 0; ActorIndex < Count; ++ActorIndex) {
					SDK::AActor* Actor = Level->Actors[ActorIndex];
					if (!Actor || !Actor->Class || Actor->Index < 0) continue;
					const ActorType Type = ClassifyESPActor(Actor);
					if (Type == ActorType::Unknown) continue;
					s_cachedESPActors.push_back({ Actor, Actor->Index, Type });
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
		}

        waterCandidates.clear();

        const char* rawEntityFilter = g_Config::entitySearchBuf;
        const bool  hasEntityFilter = g_Config::bEnableFilter && rawEntityFilter[0] != '\0';
        const char* rawStructFilter = g_Config::structureSearchBuf;
        const bool  hasStructureFilter = g_Config::bEnableStructureFilter && rawStructFilter[0] != '\0';
        const g_ESP::Structures::Filter StructureTypes{g_Config::bStructureTypeFilter,
            {g_Config::bStructureAutoTurret, g_Config::bStructureHeavyTurret, g_Config::bStructureTekTurret,
             g_Config::bStructureGenerator, g_Config::bStructureTekGenerator, g_Config::bStructureWindTurbine,
             g_Config::bStructureSupplyDrop, g_Config::bStructureLootCrate,
             g_Config::bStructureBuriedTreasure, g_Config::bStructureOther}};

        for (auto& kv : s_entries)
            kv.second.aliveThisFrame = false;

        s_actorDesiredHighlights.clear();
        constexpr uint8_t FriendlyDinoStencil =
            EncodeFullHealthStencil(SDK::EStencilAlliance::Friendly);
        constexpr uint8_t HostileDinoStencil =
            EncodeFullHealthStencil(SDK::EStencilAlliance::Hostile);
        constexpr uint8_t CalmDinoStencil =
            EncodeFullHealthStencil(SDK::EStencilAlliance::NPC);
        constexpr uint8_t FleeingDinoStencil =
            EncodeFullHealthStencil(SDK::EStencilAlliance::NPCFleeing);

        DrawIndependentOverlay(Canvas, LocalPC, LocalChar);
        if (!CharacterVisuals && !g_Config::bCombatESP && !g_Config::bDrawTurretAmmo &&
            !g_Config::bDrawStructures && !g_Config::bDrawDroppedItems && !g_Config::bDrawWater) {
            RestoreAllActorHighlights();
            s_entries.clear();
            waterCandidates.clear();
            return;
        }

        for (const CachedESPActorRef& ActorRef : s_cachedESPActors) {
			if (ActorRef.ObjectIndex < 0 ||
				ActorRef.ObjectIndex >= SDK::UObject::GObjects->Num()) continue;
			SDK::UObject* LiveObject =
				SDK::UObject::GObjects->GetByIndex(ActorRef.ObjectIndex);
			if (LiveObject != ActorRef.Actor || !LiveObject || !LiveObject->Class ||
				LiveObject->Index != ActorRef.ObjectIndex ||
				(LiveObject->Flags & SDK::EObjectFlags::BeginDestroyed) ||
				(LiveObject->Flags & SDK::EObjectFlags::FinishDestroyed)) {
				continue;
			}
			SDK::AActor* TargetActor = ActorRef.Actor;
            if (!TargetActor || TargetActor == LocalPC->Pawn || TargetActor == _DD::LocalSC) continue;

            const ActorType CurrentActorType = ActorRef.Type;
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
            entry.cachedRect.valid = false;

            if (TargetActor->bHidden) {
                entry.targetAlpha = 0.0f;
                entry.aliveThisFrame = false;
                continue;
            }

            const SDK::FVector actorLoc = TargetActor->K2_GetActorLocation();
            entry.lastWorldLoc = actorLoc;
            entry.indicatorWorldLoc = actorLoc;
            entry.aliveThisFrame = true;
			entry.isOOF = false;

            // ============================================================
            // Branch: PrimalCharacter (包含了 ShooterCharacter, PrimalDinoCharacter 以及其它派生)
            // ============================================================
            if (entry.actorType == ActorType::ShooterCharacter || entry.actorType == ActorType::PrimalDinoCharacter || entry.actorType == ActorType::PrimalCharacter) {
                SDK::APrimalCharacter* TargetChar = static_cast<SDK::APrimalCharacter*>(TargetActor);
                SDK::APlayerState* TargetPS = TargetChar->PlayerState;

                const float dist = (LocalPC && LocalPC->Pawn && TargetActor) ? LocalPC->Pawn->GetDistanceTo(TargetActor) * 0.01f : 0.0f;
                entry.distanceMeters = dist;
                const bool isDead = TargetChar->IsDead();
                entry.characterAlive = !isDead;
                entry.selectedTarget = g_Aimbot::IsSelectedTarget(TargetActor);

                // 将生物类型判定作为一个布尔值，用于区分全局ESP应用规则
                // Non-player APrimalCharacter subclasses are creature/NPC
                // fallbacks and should obey the dino display settings.
                const bool isDino = (entry.actorType != ActorType::ShooterCharacter);
                if (!g_Config::ESPCharacterEnabled(isDino) || (isDino && g_Config::bCombatESP)) {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    continue;
                }


                if (isDead) {
                    g_ESP::RelationType relation = g_ESP::GetRelation(TargetChar, LocalChar);

                    const bool bShowRagdoll = g_Config::bDrawRagdoll;
                    const float* RagdollCol = relation == g_ESP::RelationType::Team ? g_Config::RagdollColorTeam : g_Config::RagdollColor;
                    const float* DistCol = relation == g_ESP::RelationType::Team ? g_Config::DistanceColorTeam : g_Config::DistanceColor;

                    if (!bShowRagdoll) {
                        entry.targetAlpha = 0.0f;
                        entry.aliveThisFrame = false;
                        continue;
                    }

                    SDK::FVector VisualOrigin{};
                    SDK::FVector VisualExtent{};
                    const bool HasVisualBounds = isDino && ResolveCharacterVisualBounds(
                        TargetChar, entry, actorLoc, ActorRefreshNow,
                        &VisualOrigin, &VisualExtent);
                    g_ESP::BoxRect rect = g_ESP::GetBox(
                        Canvas, TargetActor, 0.5f, true,
                        HasVisualBounds ? &VisualOrigin : nullptr,
                        HasVisualBounds ? &VisualExtent : nullptr);

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

                    entry.shouldDrawBox = g_Config::bDrawBox;
                    entry.shouldDrawName = g_Config::bDrawName;
                    entry.shouldDrawDistance = g_Config::bDrawDistance;

                    entry.shouldDrawHealthBar = false;
                    entry.shouldDrawTorpor = false;
                    entry.flags.clear();
                    entry.bars.clear();

                    entry.labelScale = GetDinoLabelScale(dist);

                    std::string deadLabel;
                    if (entry.shouldDrawDistance && isDino) {
                        deadLabel = "[" + std::to_string((int)dist) + "m]";
                    }
                    if (entry.shouldDrawName) {
                        if (!deadLabel.empty()) deadLabel += "  ";
                        deadLabel += TargetPS
                            ? TargetPS->GetPlayerName().ToString()
                            : TargetChar->GetDescriptiveName().ToString();
                    }
                    if (entry.shouldDrawDistance && !isDino) {
                        if (!deadLabel.empty()) deadLabel += "  ";
                        deadLabel += "[" + std::to_string((int)dist) + "m]";
                    }
                    if (!deadLabel.empty()) {
                        entry.flags.push_back({ std::move(deadLabel),
                            entry.shouldDrawDistance ? entry.distanceColor : entry.boxColor,
                            isDino ? g_ESP::FlagPos::Top : g_ESP::FlagPos::Bottom });
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
                entry.relation = relation;
                const bool Friendly = relation == g_ESP::RelationType::Team;

                if (isDino && g_Config::bShowCreatureAggro && LocalChar &&
                    TargetChar->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                    if (ActorRefreshNow >= entry.nextAggroRefresh) {
                        auto* Dino = static_cast<SDK::APrimalDinoCharacter*>(TargetChar);
                        entry.localAggro = 0;
                        SDK::AActor* AttackTarget = nullptr;
                        SDK::FVector AttackLocation{};
                        bool HasTarget = false;
                        Dino->BPGetReplicatedAttackTargets(&AttackTarget, &AttackLocation, &HasTarget);
                        if (HasTarget && AttackTarget == LocalChar) {
                            entry.localAggro = 1;
                        } else if (HasTarget && AttackTarget &&
                                   LocalChar->IsA(SDK::AShooterCharacter::StaticClass())) {
                            auto* Mount = static_cast<SDK::AShooterCharacter*>(LocalChar)->MountedDino.Get();
                            if (Mount && AttackTarget == Mount) entry.localAggro = 2;
                        }
                        const bool Hostile = !Friendly &&
                            (entry.localAggro != 0 || Dino->BPIsTamed() ||
                             Dino->IsHostileOrAggressiveTo(LocalChar));
                        entry.creatureAggroStencil = Hostile ? HostileDinoStencil :
                            Dino->IsFleeing() ? FleeingDinoStencil : CalmDinoStencil;
                        entry.nextAggroRefresh = ActorRefreshNow + std::chrono::milliseconds(250);
                    }
                } else {
                    entry.localAggro = 0;
                    entry.creatureAggroStencil = 0;
                    entry.nextAggroRefresh = {};
                }

                if ((g_Config::bModelHighlightEnabled || (isDino && g_Config::bShowCreatureAggro)) &&
                    std::isfinite(dist) &&
                    dist <= std::clamp(g_Config::ModelHighlightMaxDistance,
                        25.0f, 1000.0f)) {
                    const uint8_t HighlightStencil = isDino && g_Config::bShowCreatureAggro &&
                        entry.creatureAggroStencil ? entry.creatureAggroStencil :
                        Friendly ? FriendlyDinoStencil : HostileDinoStencil;
                    const auto HighlightMesh = [&](SDK::USkeletalMeshComponent* Mesh) {
                        const HighlightObjectRef MeshRef = CaptureHighlightObjectRef(Mesh);
                        if (MeshRef.Component) s_actorDesiredHighlights[MeshRef.Component] = {
                            MeshRef.ObjectIndex, MeshRef.SerialNumber,
                            HighlightStencil, true};
                    };
                    HighlightMesh(TargetChar->Mesh);
                    // Walk existing attachments: the SDK cannot free an engine-
                    // allocated GetComponentsByClass result. Clothing belongs to
                    // the survivor; attached weapons owned by other actors do not.
                    if (!isDino && TargetChar->Mesh) {
                        std::vector<SDK::USceneComponent*> Pending{TargetChar->Mesh};
                        std::unordered_set<SDK::USceneComponent*> SeenComponents;
                        for (size_t I = 0; I < Pending.size() && I < 128; ++I) {
                            auto* Component = Pending[I];
                            if (!Component || !Component->Class || !SeenComponents.insert(Component).second) continue;
                            if (Component->Outer == TargetChar && Component->IsA(SDK::USkeletalMeshComponent::StaticClass()))
                                HighlightMesh(static_cast<SDK::USkeletalMeshComponent*>(Component));
                            const auto& Children = Component->AttachChildren;
                            if (Children.Num() > 0 && Children.Num() <= 128 && Children.IsValid())
                                for (auto* Child : Children) {
                                    if (Pending.size() >= 128) break;
                                    if (Child) Pending.push_back(Child);
                                }
                        }
                    }
                }

                if (!isDino) CollectEquipment(static_cast<SDK::AShooterCharacter*>(TargetChar), entry, ActorRefreshNow);
                const bool bDrawBox = g_Config::bDrawBox || g_Config::bMinimalESP, bDrawHealthBar = g_Config::bDrawHealthBar;
                const bool bDrawName = g_Config::bDrawName, bDrawDistance = g_Config::bDrawDistance;
                const bool bDrawTorpor = g_Config::bDrawTorpor;
                const float* BoxColor = Friendly ? g_Config::BoxColorTeam : g_Config::BoxColor;
                const float* NameColor = Friendly ? g_Config::NameColorTeam : g_Config::NameColor;
                const float* DistanceColor = Friendly ? g_Config::DistanceColorTeam : g_Config::DistanceColor;
                const float* TorporColor = Friendly ? g_Config::TorporColorTeam : g_Config::TorporColor;

                SDK::FVector VisualOrigin{};
                SDK::FVector VisualExtent{};
                g_ESP::BoxRect rect{};
                const bool NeedsVisualRect = bDrawBox || bDrawHealthBar || bDrawName ||
                    bDrawDistance || bDrawTorpor ||
                    (isDino && g_Config::bShowCreatureAggro) ||
                    g_Config::bEnableOOF || (!isDino && (g_Config::bDrawArmorQuality || g_Config::bDrawHeldWeapon));
                if (NeedsVisualRect) {
                    const bool HasVisualBounds = isDino && ResolveCharacterVisualBounds(
                        TargetChar, entry, actorLoc, ActorRefreshNow,
                        &VisualOrigin, &VisualExtent);
                    if (HasVisualBounds) {
                        entry.indicatorWorldLoc = VisualOrigin;
                    }
                    rect = g_ESP::GetBox(
                        Canvas, TargetActor, 0.5f, true,
                        HasVisualBounds ? &VisualOrigin : nullptr,
                        HasVisualBounds ? &VisualExtent : nullptr);
                }

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

                const float TargetLabelScale = GetDinoLabelScale(dist);
                const float ScaleBlend = std::clamp(deltaTime * 10.0f, 0.0f, 1.0f);
                entry.labelScale += (TargetLabelScale - entry.labelScale) * ScaleBlend;

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

                if (isDino && g_Config::bShowCreatureAggro && entry.localAggro)
                    entry.flags.push_back({LanguageManager::Get().TranslateCopy(
                        entry.localAggro == 1 ? "正在攻击你" : "正在攻击坐骑"),
                        Shadow::Color{1.0f, 0.32f, 0.18f, 1.0f}, g_ESP::FlagPos::Bottom});

                std::string primaryLabel;
                if (bDrawDistance && isDino) {
                    primaryLabel = "[" + std::to_string((int)dist) + "m]";
                }
                if (bDrawName) {
                    if (!primaryLabel.empty()) primaryLabel += "  ";
                    primaryLabel += entry.name;
                }
                if (bDrawDistance && !isDino) {
                    if (!primaryLabel.empty()) primaryLabel += "  ";
                    primaryLabel += "[" + std::to_string((int)dist) + "m]";
                }
                if (!primaryLabel.empty()) {
                    entry.flags.push_back({ std::move(primaryLabel),
                        bDrawDistance ? entry.distanceColor : entry.nameColor,
                        isDino ? g_ESP::FlagPos::Top : g_ESP::FlagPos::Bottom });
                }

                if (bDrawHealthBar) {
                    const float healthPct = (entry.cachedMaxHP > 0.0f) ? (entry.cachedHP / entry.cachedMaxHP) : 0.0f;
                    float* colMax = Friendly ? g_Config::TeamHealthColor1 : g_Config::HealthBarColor1;
                    float* colMin = Friendly ? g_Config::TeamHealthColor2 : g_Config::HealthBarColor2;

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

                // Use the model centre with an inner safe area. A tail/wing
                // touching one edge no longer makes the direction arrow flash
                // on and off while the target itself remains off screen.
                const float RectCenterX = rect.valid
                    ? (rect.topLeft.x + rect.bottomRight.x) * 0.5f : -1.0f;
                const float RectCenterY = rect.valid
                    ? (rect.topLeft.y + rect.bottomRight.y) * 0.5f : -1.0f;
                constexpr float ScreenEdgeHysteresis = 18.0f;
                const bool onScreen = rect.valid &&
                    RectCenterX >= ScreenEdgeHysteresis &&
                    RectCenterX <= screenW - ScreenEdgeHysteresis &&
                    RectCenterY >= ScreenEdgeHysteresis &&
                    RectCenterY <= screenH - ScreenEdgeHysteresis;
                if (onScreen) {
                    entry.targetAlpha = 1.0f;
                    entry.isOOF = false;
                }
                else if (g_Config::bEnableOOF &&
                    relation == g_ESP::RelationType::Enemy &&
                    std::isfinite(dist) && dist <= std::clamp(
                        g_Config::OOFMaxDistance, 25.0f, 2000.0f)) {
                    entry.targetAlpha = 1.0f;
                    entry.aliveThisFrame = true;
                    entry.isOOF = true;
                }
                else {
                    entry.targetAlpha = 0.0f;
                    entry.aliveThisFrame = false;
                    entry.isOOF = false;
                }
            }

            // ============================================================
            // Branch: WaterVolume
            // ============================================================
            else if (entry.actorType == ActorType::PhysicsVolume && g_Config::bDrawWater && !g_Config::bCombatESP) {
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
            else if (entry.actorType == ActorType::DroppedItem && g_Config::bDrawDroppedItems && !g_Config::bCombatESP) {
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
                const bool bIsProjected = LocalPC && g_ESP::ProjectToCanvas(Canvas, LocalPC, actorLoc, &screenPos);
                const bool bOnScreen = bIsProjected && (screenPos.X > 0 && screenPos.X < screenW && screenPos.Y > 0 && screenPos.Y < screenH);

                // JIT icons are already selected by the game for this item.
                // The deprecated field is a cheap fallback while JIT loads.
                if (bOnScreen && (entry.iconSourceItem != Item ||
                    ActorRefreshNow >= entry.nextItemIconRefresh)) {
                    entry.iconSourceItem = Item;
                    entry.nextItemIconRefresh = ActorRefreshNow + std::chrono::milliseconds(500);
                    entry.itemIcon = nullptr;
                    entry.itemIconIndex = entry.itemIconSerial = -1;
                    const auto Icon = CaptureItemTexture(Item);
                    entry.itemIcon = Icon.texture;
                    entry.itemIconIndex = Icon.index;
                    entry.itemIconSerial = Icon.serial;
                    entry.itemQualityColor = ResolveItemQuality(Item, SDK::UPrimalGameData::BPGetGameData()).color;
                }

                if (bIsProjected) {
                    const float HalfSize = entry.itemIcon ? g_Config::ESPIconPixels * 0.5f : 5.0f;
                    entry.cachedRect.topLeft = Shadow::Vec2{ (float)(screenPos.X - HalfSize), (float)(screenPos.Y - HalfSize) };
                    entry.cachedRect.bottomRight = Shadow::Vec2{ (float)(screenPos.X + HalfSize), (float)(screenPos.Y + HalfSize) };
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
                auto* Structure = static_cast<SDK::APrimalStructure*>(TargetActor);
                entry.targetAlpha = 0.0f;
                entry.aliveThisFrame = false;
                entry.isItem = false;
                entry.characterAlive = false;
                entry.selectedTarget = false;
                entry.shouldDrawTorpor = false;
                entry.flags.clear();
                entry.bars.clear();
                entry.owner.clear();
                const bool IsTurret = Structure->IsA(SDK::APrimalStructureTurret::StaticClass());
                entry.ammo.clear();
                if (!g_Config::bStructureShowLootContents || g_Config::bCombatESP) {
                    entry.lootContents.clear();
                    entry.lootMoreCount = 0;
                    entry.nextLootRefresh = {};
                }
                if ((g_Config::bCombatESP && !IsTurret) ||
                    (!g_Config::bDrawStructures && !(IsTurret && (g_Config::bDrawTurretAmmo || g_Config::bCombatESP))) || Structure->bIsPreviewStructure ||
                    !std::isfinite(Structure->Health) || Structure->Health <= 0.0f) continue;

                const float Distance = LocalPC->Pawn->GetDistanceTo(TargetActor) * 0.01f;
                if (!std::isfinite(Distance) || !std::isfinite(g_Config::StructureMaxDistance) ||
                    Distance > std::clamp(g_Config::StructureMaxDistance, 1.0f, 10000.0f)) continue;
                entry.distanceMeters = Distance;
                std::string Name = Structure->GetDescriptiveName().ToString();
                if (Name.empty() || Name == "None") Name = "Structure";
                if (hasStructureFilter && !g_Util::IsStructureMatchMulti(Name, rawStructFilter)) continue;
                if (StructureTypes.Enabled || g_Config::bStructureShowLootContents) {
                    if (!entry.structureTypeKnown) {
                        entry.structureType = ClassifyStructure(Structure, Name);
                        entry.structureTypeKnown = true;
                    }
                }
                if (!StructureTypes.Allows(entry.structureType)) continue;

                bool Friendly = LocalChar && LocalChar->TargetingTeam != 0 &&
                    LocalChar->TargetingTeam == Structure->TargetingTeam;
                if (!Friendly && LocalChar && (LocalChar->TargetingTeam == 0 || Structure->TargetingTeam == 0)) {
                    const auto Tribe = LocalChar->TribeName.ToString();
                    Friendly = !Tribe.empty() && Tribe != "None" && Tribe == Structure->OwnerName.ToString();
                }
                entry.relation = Friendly ? g_ESP::RelationType::Team : g_ESP::RelationType::Enemy;
                if (IsTurret && (g_Config::bDrawTurretAmmo || g_Config::bCombatESP)) {
                    const int Ammo = static_cast<SDK::APrimalStructureTurret*>(Structure)->NumBullets;
                    entry.ammo = LanguageManager::Get().TranslateCopy("弹量：") + (Ammo >= 0 ? std::to_string(Ammo) : "?");
                }
                if (g_Config::bStructureShowLootContents && !g_Config::bCombatESP &&
                    ActorRefreshNow >= entry.nextLootRefresh &&
                    (Structure->IsA(SDK::APrimalStructureItemContainer::StaticClass()) ||
                     entry.structureType == g_ESP::Structures::Type::SupplyDrop ||
                     entry.structureType == g_ESP::Structures::Type::LootCrate ||
                     entry.structureType == g_ESP::Structures::Type::BuriedTreasure)) {
                    entry.nextLootRefresh = ActorRefreshNow + std::chrono::milliseconds(500);
                    entry.lootContents.clear();
                    entry.lootMoreCount = 0;
                    auto* Inventory = Structure->IsA(SDK::APrimalStructureItemContainer::StaticClass())
                        ? static_cast<SDK::APrimalStructureItemContainer*>(Structure)->MyInventoryComponent : nullptr;
                    auto* GameData = SDK::UPrimalGameData::BPGetGameData();
                    int32_t Serial = -1;
                    if (Inventory && TryGetLiveObjectSerial(Inventory, Inventory->Index, &Serial) &&
                        !(Inventory->Flags & SDK::EObjectFlags::BeginDestroyed) &&
                        !(Inventory->Flags & SDK::EObjectFlags::FinishDestroyed)) {
                        const int32_t Count = Inventory->InventoryItems.Num();
                        if (Count > 0 && Count <= 10000 && Inventory->InventoryItems.IsValid()) {
                            int Shown = 0, Inspected = 0;
                            for (auto* Item : Inventory->InventoryItems) {
                                if (++Inspected > 64) break;
                                if (!Item || !TryGetLiveObjectSerial(Item, Item->Index, &Serial) ||
                                    (Item->Flags & SDK::EObjectFlags::BeginDestroyed) ||
                                    (Item->Flags & SDK::EObjectFlags::FinishDestroyed) ||
                                    Item->ItemQuantity <= 0) continue;
                                std::string ItemName = Item->CustomItemName.IsValid() ? Item->CustomItemName.ToString() : "";
                                if (ItemName.empty()) ItemName = Item->DescriptiveNameBase.IsValid()
                                    ? Item->DescriptiveNameBase.ToString()
                                    : (Item->Class ? Item->Class->GetName() : "");
                                if (ItemName.empty()) continue;
                                CachedLootItem Loot{};
                                Loot.info.Name = std::move(ItemName);
                                Loot.info.Quantity = Item->ItemQuantity;
                                Loot.info.Kind = Item->bIsBlueprint ? g_ESP::LootItemKind::Blueprint : g_ESP::LootItemKind::Item;
                                Loot.info.QualityColor = ResolveItemQuality(Item, GameData).color;
                                Loot.icon = CaptureItemTexture(Item);
                                entry.lootContents.push_back(std::move(Loot));
                                if (++Shown == MaximumLootIcons) break;
                            }
                            if (Shown) entry.lootMoreCount = std::max(0, Count - Shown);
                        }
                    }
                }
                if (g_Config::bStructureShowLootContents && entry.lootContents.empty() &&
                    (Structure->IsA(SDK::APrimalStructureItemContainer::StaticClass()) ||
                     entry.structureType == g_ESP::Structures::Type::SupplyDrop ||
                     entry.structureType == g_ESP::Structures::Type::LootCrate ||
                     entry.structureType == g_ESP::Structures::Type::BuriedTreasure)) {
                    std::lock_guard<std::mutex> Guard(s_spyglassLootMutex);
                    auto Cached = s_spyglassLoot.find(key);
                    if (Cached != s_spyglassLoot.end()) {
                        if (Cached->second.ObjectIndex == Structure->Index &&
                            ActorRefreshNow - Cached->second.Received < std::chrono::seconds(8)) {
                            entry.lootContents = Cached->second.Items;
                            entry.lootMoreCount = Cached->second.MoreCount;
                        }
                        else s_spyglassLoot.erase(Cached);
                    }
                }
                const bool Highlight = !g_Config::bCombatESP && g_Config::bModelHighlightEnabled &&
                    Distance <= std::clamp(g_Config::ModelHighlightMaxDistance, 25.0f, 1000.0f);
                const bool ShowInfo = !entry.ammo.empty() || !entry.lootContents.empty() || g_Config::bDrawBox || g_Config::bMinimalESP || g_Config::bDrawName ||
                    g_Config::bDrawDistance || g_Config::bDrawHealthBar;
                if (!Highlight && !ShowInfo) continue;

                // Use rendered mesh bounds, excluding collision/range spheres.
                // Queue highlights before projection so occluded/offscreen meshes
                // use the same native material path as players and creatures.
                static std::vector<HighlightObjectRef> StructureMeshes;
                CollectStructureMeshes(Structure, StructureMeshes);
                SDK::FVector Minimum{}, Maximum{};
                bool HasBounds = false;
                for (const auto& Ref : StructureMeshes) {
                    if (Highlight) s_actorDesiredHighlights[Ref.Component] = {
                        Ref.ObjectIndex, Ref.SerialNumber, Friendly ? FriendlyDinoStencil : HostileDinoStencil, true};
                    if (!ShowInfo) continue;
                    SDK::FVector Origin{}, Extent{};
                    float Radius = 0;
                    SDK::UKismetSystemLibrary::GetComponentBounds(Ref.Component, &Origin, &Extent, &Radius);
                    if (!IsFiniteResourceVector(Origin) || !IsFiniteResourceVector(Extent) ||
                        !std::isfinite(Radius) || Radius <= 0 || Extent.X <= 0 || Extent.Y <= 0 || Extent.Z <= 0 ||
                        Extent.X > 250000 || Extent.Y > 250000 || Extent.Z > 250000) continue;
                    const auto Low = Origin - Extent, High = Origin + Extent;
                    if (!HasBounds) { Minimum = Low; Maximum = High; HasBounds = true; }
                    else {
                        Minimum = {std::min(Minimum.X, Low.X), std::min(Minimum.Y, Low.Y), std::min(Minimum.Z, Low.Z)};
                        Maximum = {std::max(Maximum.X, High.X), std::max(Maximum.Y, High.Y), std::max(Maximum.Z, High.Z)};
                    }
                }
                entry.aliveThisFrame = Highlight;
                if (!ShowInfo) continue;
                const SDK::FVector Origin = (Minimum + Maximum) * 0.5;
                const SDK::FVector Extent = (Maximum - Minimum) * 0.5;
                entry.cachedRect = g_ESP::GetBox(Canvas, TargetActor, 1.0f, false,
                    HasBounds ? &Origin : nullptr, HasBounds ? &Extent : nullptr);
                if (!entry.cachedRect.valid) continue;
                const auto& Rect = entry.cachedRect;
                if (Rect.bottomRight.x <= 0 || Rect.bottomRight.y <= 0 ||
                    Rect.topLeft.x >= screenW || Rect.topLeft.y >= screenH) continue;

                const float* BoxColor = Friendly ? g_Config::BoxColorTeam : g_Config::BoxColor;
                const float* NameColor = Friendly ? g_Config::NameColorTeam : g_Config::NameColor;
                const float* DistanceColor = Friendly ? g_Config::DistanceColorTeam : g_Config::DistanceColor;
                entry.boxColor = {BoxColor[0], BoxColor[1], BoxColor[2], BoxColor[3]};
                entry.nameColor = {NameColor[0], NameColor[1], NameColor[2], NameColor[3]};
                entry.distanceColor = {DistanceColor[0], DistanceColor[1], DistanceColor[2], DistanceColor[3]};
                entry.shouldDrawBox = !g_Config::bCombatESP && (g_Config::bDrawBox || g_Config::bMinimalESP);
                entry.shouldDrawName = !g_Config::bCombatESP && g_Config::bDrawName;
                entry.shouldDrawDistance = !g_Config::bCombatESP && g_Config::bDrawDistance;
                entry.shouldDrawHealthBar = !g_Config::bCombatESP && g_Config::bDrawHealthBar;
                entry.name = entry.shouldDrawName ? Name : "";
                if (entry.shouldDrawName && g_Config::bStructureShowOwner) {
                    entry.owner = Structure->OwnerName.ToString();
                    if (entry.owner == "None") entry.owner.clear();
                }
                entry.cachedHP = Structure->Health;
                entry.cachedMaxHP = std::isfinite(Structure->MaxHealth) ? Structure->MaxHealth : 0;
                if (entry.shouldDrawHealthBar) {
                    const auto Color = GetHealthColorLinear(entry.cachedMaxHP > 0 ? entry.cachedHP / entry.cachedMaxHP : 0,
                        Friendly ? g_Config::TeamHealthColor1 : g_Config::HealthBarColor1,
                        Friendly ? g_Config::TeamHealthColor2 : g_Config::HealthBarColor2);
                    entry.bars.push_back({entry.cachedHP, entry.cachedMaxHP, Color,
                        g_ESP::BarPos::Left, g_ESP::BarOrientation::Vertical});
                }
                entry.labelScale += (GetDinoLabelScale(Distance) - entry.labelScale) * std::clamp(deltaTime * 10.0f, 0.0f, 1.0f);
                entry.aliveThisFrame = true;
                entry.targetAlpha = 1.0f;
            }
            else {
                entry.targetAlpha = 0.0f;
                entry.aliveThisFrame = false;
            }
        } // end actor loop

        ApplyCombinedModelHighlights();
        ApplyResourceCameraHighlight(LocalPC);
        ApplyResourceRenderPipeline(World);

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
                if (LocalPC && g_ESP::ProjectToCanvas(Canvas, LocalPC, wc.surfaceLoc, &currentScreenPos)) {
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
        static std::vector<ESPEntry*> OffscreenEntries;
        RenderEntries.clear();
        OffscreenEntries.clear();

        for (auto& kv : s_entries) {
            ESPEntry& entry = kv.second;

            const float fadeTime = (entry.targetAlpha > entry.alpha) ? FADE_IN_TIME : FADE_OUT_TIME;
            entry.alpha = g_Util::ApproachAlpha(entry.alpha, entry.targetAlpha, deltaTime, fadeTime);

            if (entry.alpha <= 0.001f && entry.targetAlpha <= 0.001f && !entry.aliveThisFrame) {
                s_toErase.push_back(kv.first);
                continue;
            }

            if (entry.alpha > 0.001f) {
				if (entry.isOOF && entry.aliveThisFrame) {
					OffscreenEntries.push_back(&entry);
				}
				else {
					RenderEntries.push_back(&entry);
				}
            }
        }

		std::sort(OffscreenEntries.begin(), OffscreenEntries.end(),
			[](const ESPEntry* Left, const ESPEntry* Right) {
				if (Left->distanceMeters != Right->distanceMeters) {
					return Left->distanceMeters < Right->distanceMeters;
				}
				return Left->actorKey < Right->actorKey;
			});
		if (g_Config::bEnableOOF && !OffscreenEntries.empty()) {
			SDK::FVector CameraLocation{};
			SDK::FRotator CameraRotation{};
			LocalPC->GetPlayerViewPoint(&CameraLocation, &CameraRotation);
			const double IndicatorTime = std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			static std::vector<Shadow::Vec2> OccupiedIndicatorTips;
			OccupiedIndicatorTips.clear();
			constexpr size_t MaximumVisibleThreats = 16;
			size_t DrawnThreats = 0;
			for (ESPEntry* Threat : OffscreenEntries) {
				if (DrawnThreats >= MaximumVisibleThreats) break;
				if (DrawOffscreenThreatIndicator(*Threat, CameraLocation,
					CameraRotation, screenW, screenH, IndicatorTime,
					OccupiedIndicatorTips)) {
					++DrawnThreats;
				}
			}
		}

        // Give readable labels to the visually nearest/largest targets first.
        // Farther labels that collide are suppressed by FlagManager instead of
        // being shifted away from the creature they describe.
        std::sort(RenderEntries.begin(), RenderEntries.end(), [](const ESPEntry* Left,
            const ESPEntry* Right) {
                if (Left->selectedTarget != Right->selectedTarget) return Left->selectedTarget;
                if (Left->aliveThisFrame != Right->aliveThisFrame) {
                    return Left->aliveThisFrame > Right->aliveThisFrame;
                }
                const auto IsCharacterEntry = [](const ESPEntry* Entry) {
                    return Entry->actorType == ActorType::ShooterCharacter ||
                        Entry->actorType == ActorType::PrimalDinoCharacter ||
                        Entry->actorType == ActorType::PrimalCharacter;
                    };
                if (IsCharacterEntry(Left) != IsCharacterEntry(Right)) {
                    return IsCharacterEntry(Left) > IsCharacterEntry(Right);
                }
                if (Left->distanceMeters != Right->distanceMeters) {
                    return Left->distanceMeters < Right->distanceMeters;
                }
                return Left->actorKey < Right->actorKey;
            });

        for (ESPEntry* Entry : RenderEntries) {
            ESPEntry& entry = *Entry;
            if (entry.alpha > 0.001f && entry.aliveThisFrame) {
                if (entry.actorType == ActorType::DroppedItem && entry.itemIcon &&
                    entry.cachedRect.valid) {
                    int32_t Serial = -1;
                    if (TryGetLiveObjectSerial(entry.itemIcon, entry.itemIconIndex, &Serial) &&
                        Serial == entry.itemIconSerial &&
                        !(entry.itemIcon->Flags & SDK::EObjectFlags::BeginDestroyed) &&
                        !(entry.itemIcon->Flags & SDK::EObjectFlags::FinishDestroyed)) {
                        const SDK::FVector2D IconPosition{entry.cachedRect.topLeft.x, entry.cachedRect.topLeft.y};
                        const SDK::FVector2D IconSize{g_Config::ESPIconPixels, g_Config::ESPIconPixels};
                        const SDK::FVector2D Zero{0.0, 0.0}, One{1.0, 1.0};
                        if (Canvas->DefaultTexture && entry.itemQualityColor.a > 0.0f) {
                            const auto& C = entry.itemQualityColor;
                            Canvas->K2_DrawTexture(Canvas->DefaultTexture, IconPosition, IconSize,
                                Zero, One, {C.r, C.g, C.b, entry.alpha * 0.8f},
                                SDK::EBlendMode::BLEND_Translucent, 0.0f, Zero);
                        }
                        const double Inset = entry.itemQualityColor.a > 0.0f ? 2.0 : 0.0;
                        Canvas->K2_DrawTexture(entry.itemIcon,
                            {IconPosition.X + Inset, IconPosition.Y + Inset},
                            {IconSize.X - 2.0 * Inset, IconSize.Y - 2.0 * Inset},
                            Zero, One, {1.0f, 1.0f, 1.0f, entry.alpha},
                            SDK::EBlendMode::BLEND_Translucent, 0.0f, Zero);
                    }
                }
                if (!entry.isItem && entry.shouldDrawBox && entry.cachedRect.valid) {
                    Shadow::Color boxCol = entry.boxColor;

                    g_ESP::DrawBox(Canvas, entry.cachedRect, boxCol, entry.alpha);
                }

                if (entry.actorType == ActorType::PrimalStructure ||
                    (entry.characterAlive && (entry.actorType == ActorType::ShooterCharacter ||
                    entry.actorType == ActorType::PrimalDinoCharacter || entry.actorType == ActorType::PrimalCharacter))) {
                    g_ESP::EntityInfo Info;
                    if (entry.actorType == ActorType::ShooterCharacter) {
                        if (g_Config::bDrawArmorQuality) Info.Armor = entry.armor;
                        if (g_Config::bDrawHeldWeapon) Info.Weapon = entry.weapon;
                        if (g_Config::bDrawArmorQuality)
                            for (size_t Slot = 0; Slot < Info.ArmorIcons.size(); ++Slot) {
                                Info.ArmorIcons[Slot] = LiveItemTexture(entry.armorIcons[Slot]);
                                Info.ArmorQualityColors[Slot] = entry.armorQualityColors[Slot];
                            }
                        if (g_Config::bDrawHeldWeapon) {
                            Info.WeaponIcon = LiveItemTexture(entry.weaponIcon);
                            Info.WeaponQualityColor = entry.weaponQualityColor;
                        }
                        Info.ArmorColor = ExpansionColor(g_Config::ArmorQualityColor);
                        Info.WeaponColor = ExpansionColor(g_Config::HeldWeaponColor);
                    }
                    Info.Ammo = entry.ammo;
                    for (const auto& Cached : entry.lootContents) {
                        auto Item = Cached.info;
                        Item.Icon = LiveItemTexture(Cached.icon);
                        Info.LootItems.push_back(std::move(Item));
                    }
                    Info.LootMoreCount = entry.lootMoreCount;
                    Info.AmmoColor = ExpansionColor(g_Config::TurretAmmoColor);
                    Info.Name = entry.shouldDrawName ? entry.name : "";
                    if (entry.actorType == ActorType::PrimalStructure) {
                        Info.Detail = entry.owner.empty() ? "" : "归属：" + entry.owner;
                        const auto* Color = g_Config::StructureOwnerColor;
                        Info.DetailColor = {Color[0], Color[1], Color[2], Color[3]};
                    }
                    Info.Distance = entry.distanceMeters;
                    Info.Health = entry.cachedHP; Info.MaxHealth = entry.cachedMaxHP;
                    Info.Torpor = entry.cachedTorpor; Info.MaxTorpor = entry.cachedMaxTorpor;
                    Info.Scale = entry.labelScale; Info.Alpha = entry.alpha;
                    Info.ShowDistance = entry.shouldDrawDistance; Info.ShowHealth = entry.shouldDrawHealthBar;
                    Info.ShowTorpor = entry.shouldDrawTorpor;
                    Info.NameColor = entry.nameColor; Info.DistanceColor = entry.distanceColor;
                    for (const auto& Bar : entry.bars) {
                        if (Bar.pos == g_ESP::BarPos::Left) Info.HealthColor = Bar.color;
                        else Info.TorporColor = Bar.color;
                    }
                    g_ESP::DrawEntityInfo(Canvas, entry.cachedRect, Info, entry.infoSlot);
                    continue;
                }
                g_ESP::BarManager bm;
                bm.Reset();
                for (const auto& bar : entry.bars)
                    bm.AddBar(Canvas, entry.cachedRect, bar.currentValue, bar.maxValue, bar.color, bar.pos, bar.orientation, entry.alpha);

                g_ESP::FlagManager fm;
                fm.Reset();
                for (const auto& f : entry.flags)
                    fm.AddFlag(Canvas, entry.cachedRect, f.text, f.color, f.pos,
                        entry.alpha, &bm, entry.labelScale);
            }
        }

        for (uintptr_t k : s_toErase)
            s_entries.erase(k);
        DrawExpansionRadar(Canvas, LocalPC, LocalChar);
    }
} // namespace g_DrawESP
