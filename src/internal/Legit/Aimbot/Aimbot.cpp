#include "../../Config/InterfaceRules.h"
#include "../../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include "../../../external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../../../external/CppSDK/SDK.hpp"
#include "../../../external/CppSDK/SDK/ShooterGame_parameters.hpp"
#include "../../Config/Configs.h"
#include "../../Hack/TekSpear.h"
#include "../../ESP/ESP.h"
#include "../../Util/Util.h"
#include "Aimbot.h"
#include "AimSelection.h"
#include "AimViewArguments.h"
#include "AimTargetRules.h"
#include "AimHitValidation.h"
#include "AimCollisionGeometry.h"
#include "ProjectilePrediction.h"
#include "TekBowRules.h"
#include "../../../external/Shadow-Gui/include/Shadow.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace g_Aimbot {
    namespace {
        SDK::TWeakObjectPtr<SDK::AShooterWeapon> AutoFireWeapon{};
        SDK::TWeakObjectPtr<SDK::AShooterWeapon> AimWeapon{};
        SDK::TWeakObjectPtr<SDK::AShooterCharacter> AimOwner{};
        SDK::TWeakObjectPtr<SDK::APrimalCharacter> LockedTarget{};
        SDK::TWeakObjectPtr<SDK::APrimalCharacter> HeldTarget{};
        TargetHoldState PistolHold{};
        SDK::TWeakObjectPtr<SDK::AShooterWeapon> SpreadWeapon{};
        std::atomic<DWORD> SpreadThread{0};
        ULONGLONG SpreadPreparedAt = 0;
        SDK::FVector AimLocation{};
        SDK::FVector ShotLocation{};
        AimBodyPart LockedPart = AimBodyPart::None;
        AimBodyPart ShotPart = AimBodyPart::None;
        bool ShotDirectionValid = false;
        // Only accessed after the game-thread identity check. Native origin
        // getters can request GetAdjustedAim themselves; nested calls pass through.
        bool AimQueryInProgress = false;
        struct AimQueryScope {
            AimQueryScope() { AimQueryInProgress = true; }
            ~AimQueryScope() { AimQueryInProgress = false; }
        };
        std::string LockedTargetName;
        float LockedTargetDistanceMeters = 0;
        ULONGLONG AimPreparedAt = 0;
        std::mutex StatusMutex;
        std::string Status = "等待游戏内瞄准更新";
        void SetStatus(std::string Text) {
            std::scoped_lock Lock(StatusMutex);
            Status = std::move(Text);
        }
        std::atomic<DWORD> ActiveAimThread{0};
        std::atomic<DWORD> SelectionThread{0};
        std::atomic<uint64_t> RedirectedShots{0};
        std::atomic<uint64_t> RedirectedProjectileEvents{0};
        SDK::UFunction* ProjectileEvents[4]{};
        std::atomic<uint64_t> RedirectedTekBowViews{0};

        template<typename T> SDK::TWeakObjectPtr<T> Weak(T* Object) {
            SDK::TWeakObjectPtr<T> Result{};
            Result.ObjectIndex = -1;
            if (!Object) return Result;
            auto* Registry = SDK::UObject::GObjects.GetTypedPtr();
            const int32_t Index = Object->Index;
            if (!Registry || Index < 0 || Index >= Registry->NumElements) return Result;
            auto** Chunks = Registry->GetDecrytedObjPtr();
            const int32_t Chunk = Index / SDK::TUObjectArray::ElementsPerChunk;
            if (!Chunks || Chunk >= Registry->NumChunks || !Chunks[Chunk]) return Result;
            auto* Entry = &Chunks[Chunk][Index % SDK::TUObjectArray::ElementsPerChunk];
            if (Entry->Object != Object) return Result;
            Result.ObjectIndex = Index;
            std::memcpy(&Result.ObjectSerialNumber,
                reinterpret_cast<const uint8_t*>(Entry) + 0x10, sizeof(int32_t));
            return Result;
        }
        template<typename T> T* Live(const SDK::TWeakObjectPtr<T>& Ref) {
            // Dumper-7's FWeakObjectPtr::Get only resolves an index; verify the
            // serial ourselves before accepting a recycled registry slot.
            auto* Registry = SDK::UObject::GObjects.GetTypedPtr();
            if (!Registry || Ref.ObjectIndex < 0 || Ref.ObjectIndex >= Registry->NumElements) return nullptr;
            auto** Chunks = Registry->GetDecrytedObjPtr();
            const int32_t Chunk = Ref.ObjectIndex / SDK::TUObjectArray::ElementsPerChunk;
            if (!Chunks || Chunk >= Registry->NumChunks || !Chunks[Chunk]) return nullptr;
            auto* Entry = &Chunks[Chunk][Ref.ObjectIndex % SDK::TUObjectArray::ElementsPerChunk];
            int32_t Serial = -1;
            std::memcpy(&Serial, reinterpret_cast<const uint8_t*>(Entry) + 0x10, sizeof(Serial));
            if (Serial != Ref.ObjectSerialNumber) return nullptr;
            auto* Object = static_cast<T*>(Entry->Object);
            if (!Object || !Object->Class ||
                (Object->Flags & SDK::EObjectFlags::BeginDestroyed) ||
                (Object->Flags & SDK::EObjectFlags::FinishDestroyed)) return nullptr;
            return Object;
        }
        template<typename T> uint64_t ObjectKey(T* Object) {
            if (!Object) return 0;
            const auto Ref = Weak(Object);
            if (Ref.ObjectIndex < 0) return 0;
            return (static_cast<uint64_t>(static_cast<uint32_t>(Ref.ObjectSerialNumber)) << 32) |
                (static_cast<uint32_t>(Ref.ObjectIndex) + 1u);
        }
        bool IsTekPistol(SDK::AShooterWeapon* Weapon) {
            for (auto* Class = Weapon ? Weapon->Class : nullptr; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
                if (Class->GetName() == "WeapTekPistol_C") return true;
            return false;
        }
        bool IsShotgun(SDK::AShooterWeapon* Weapon) {
            if (!Weapon || !Weapon->IsA(SDK::AShooterWeapon_Instant::StaticClass()) ||
                static_cast<SDK::AShooterWeapon_Instant*>(Weapon)->NumTracesPerShot <= 1) return false;
            for (auto* Class = Weapon->Class; Class; Class = static_cast<SDK::UClass*>(Class->SuperStruct))
                if (IsShotgunAssetName(Class->GetName())) return true;
            return false;
        }
        SDK::AShooterCharacter* LocalHuman(SDK::APlayerController* PC) {
            if (!PC) return nullptr;
            auto AsHuman = [](SDK::UObject* Object) -> SDK::AShooterCharacter* {
                return Object && Object->Class &&
                    Object->IsA(SDK::AShooterCharacter::StaticClass()) &&
                    !Object->IsA(SDK::APrimalDinoCharacter::StaticClass())
                    ? static_cast<SDK::AShooterCharacter*>(Object) : nullptr;
            };
            if (auto* Human = AsHuman(PC->Pawn)) return Human;
            if (auto* Human = AsHuman(PC->Character)) return Human;
            if (PC->Pawn && PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass()))
                return AsHuman(static_cast<SDK::APrimalDinoCharacter*>(PC->Pawn)->Rider.Get());
            return nullptr;
        }
        void StopFire() {
            if (auto* Weapon = Live(AutoFireWeapon)) Weapon->StopFire();
            AutoFireWeapon = Weak<SDK::AShooterWeapon>(nullptr);
        }
        void ClearDirectionOverride() {
            ShotDirectionValid = false;
            ShotPart = AimBodyPart::None;
            ActiveAimThread.store(0, std::memory_order_release);
            SelectionThread.store(0, std::memory_order_release);
            AimWeapon = Weak<SDK::AShooterWeapon>(nullptr);
            AimOwner = Weak<SDK::AShooterCharacter>(nullptr);
        }
        bool Finite(const SDK::FVector& V) {
            return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
        }
        struct AimBody {
            SDK::FName Bone;
            SDK::FVector Center{};
            AimBodyPart Part = AimBodyPart::None;
        };
        struct AimBodies {
            SDK::TWeakObjectPtr<SDK::UPhysicsAsset> Asset;
            std::vector<AimBody> Parts;
        };
        std::unordered_map<SDK::UPhysicsAsset*, AimBodies> BodyCache;

        struct AimPoint {
            SDK::FVector Position{};
            AimBodyPart Part = AimBodyPart::None;
            SDK::FName Bone{};
            SDK::FVector LocalCenter{};
            bool FromBone = false;
        };
        std::vector<AimPoint> HeadBodyPoints(SDK::APrimalCharacter* Character,
            bool Head = g_Config::bAimHead, bool BodyEnabled = g_Config::bAimBody) {
            auto* Mesh = Character->Mesh;
            auto* Asset = Mesh->PhysicsAssetOverride;
            if (!Asset) {
                auto* SkeletalMesh = Mesh->GetSkeletalMeshAsset();
                Asset = SkeletalMesh ? SkeletalMesh->PhysicsAsset : nullptr;
            }
            std::vector<AimPoint> Points;
            if (Asset) {
                if (BodyCache.size() > 1024) BodyCache.clear();
                auto& Cached = BodyCache[Asset];
                if (Live(Cached.Asset) != Asset) {
                    Cached.Asset = Weak(Asset);
                    Cached.Parts.clear();
                    struct BoneGeometry { SDK::FName Bone; std::vector<SDK::FVector> Points; };
                    std::vector<BoneGeometry> GeometryBodies;
                    if (ValidAimGeometryArray(Asset->SkeletalBodySetups, 512)) {
                        for (auto* Setup : Asset->SkeletalBodySetups) {
                            if (!Setup) continue;
                            auto Points = AimGeometryPoints(Setup->AggGeom,
                                [](const auto& Vector, const auto& Rotation) {
                                    return SDK::UKismetMathLibrary::GreaterGreater_VectorRotator(Vector, Rotation);
                                }, [](const auto& Transform, const auto& Vector) {
                                    return SDK::UKismetMathLibrary::TransformLocation(Transform, Vector);
                                });
                            if (!Points.empty()) GeometryBodies.push_back({Setup->BoneName, std::move(Points)});
                        }
                    }
                    for (const auto& Body : GeometryBodies) {
                        const auto Part = ClassifyAimPhysicsBone(Body.Bone.ToString(),
                            GeometryBodies.size() == 1 && Asset->SkeletalBodySetups.Num() == 1);
                        if (Part == AimBodyPart::None) continue;
                        for (const auto& Center : Body.Points) Cached.Parts.push_back({Body.Bone, Center, Part});
                    }
                    // Retain each eligible collider, including smaller torso
                    // segments. Screen distance, not volume, selects the target.
                }
                for (const auto& Body : Cached.Parts) {
                    if (!AimPartEnabled(Body.Part, Head, BodyEnabled)) continue;
                    if (Mesh->GetBoneIndex(Body.Bone) < 0) continue;
                    SDK::FVector Point{};
                    SDK::FRotator Rotation{};
                    // Use the collision shape's center, not the bone pivot at a joint.
                    Mesh->TransformFromBoneSpace(Body.Bone, Body.Center, {}, &Point, &Rotation);
                    if (Finite(Point) && !Point.IsZero()) Points.push_back({Point, Body.Part, Body.Bone, Body.Center, true});
                }
            }
            if (Points.empty() && BodyEnabled && Character->CapsuleComponent) {
                // Missing/unknown skeleton: use the character's central capsule.
                // Never fall back to the closest physics bone, which can be a tail.
                const auto Center = Character->CapsuleComponent->K2_GetComponentLocation();
                if (Finite(Center)) Points.push_back({Center, AimBodyPart::Body});
            }
            return Points;
        }
        AimTraceState TraceAimPoint(SDK::APlayerController* PC, const SDK::FVector& Start,
            const AimPoint& Point, SDK::APrimalCharacter* Target) {
            SDK::FHitResult Hit{};
            // SDK TArray::Add does not allocate. Supply actual stack capacity.
            SDK::AActor* Ignored[2] = { PC->Pawn, LocalHuman(PC) };
            SDK::TArray<SDK::AActor*> Ignore(Ignored, Ignored[1] ? 2 : 1, 2);
            // ShooterWeapon's native trace uses collision channel 5. Visibility
            // traces can miss weapon blockers and previously accepted empty rays.
            const bool HasHit = SDK::UVictoryCore::VTraceSingleBP_IgnoreActorsArray(PC,
                &Hit, Start, Point.Position, Ignore, Ignored[1],
                SDK::ECollisionChannel::ECC_GameTraceChannel1, 0, {}, false, false, 0.0f);
            // No-hit outputs can contain zero-filled weak references. The SDK's
            // Get() ignores serials and resolves (0,0) as registry object zero.
            // Never inspect their objects or bones before checking the trace.
            if (!Target) return AimTraceState::Blocked;
            if (!HasHit || !Hit.bBlockingHit) return AimTraceState::Unavailable;
            auto* Component = ResolveAimHitReference(true, Hit.Component,
                [](const auto& Ref) { return Live(Ref); },
                [](SDK::UPrimitiveComponent* Object) { return Object->IsA(SDK::UPrimitiveComponent::StaticClass()); });
            if (!Component) return AimTraceState::Unavailable;
            auto* HitActor = ResolveAimHitReference(true, Hit.HitObjectHandle.ReferenceObject,
                [](const auto& Ref) { return Live(Ref); },
                [](SDK::UObject* Object) { return Object->IsA(SDK::AActor::StaticClass()); });
            // Avoid a reflected GetOwner call on trace outputs entirely.
            const bool HitTarget = AimHitMatchesTarget(HitActor, Component, Target,
                Target->Mesh, Target->CapsuleComponent);
            if (!HitTarget) return AimTraceState::Blocked;
            const auto Bone = Hit.BoneName.ToString();
            const bool HasBone = !Bone.empty() && Bone != "None";
            if (!HasBone && Point.Part == AimBodyPart::Head) return AimTraceState::Unavailable;
            // A single-body asset can use a generic/root bone. It is accepted
            // only when it matches the actual geometry used for this point.
            const auto HitPart = Point.FromBone && Hit.BoneName == Point.Bone
                ? Point.Part : ClassifyAimBone(Bone);
            return AcceptAimHit(true, true, Point.Part, HitPart, HasBone,
                g_Config::bAimHead, g_Config::bAimBody) ? AimTraceState::Hit : AimTraceState::Unavailable;
        }
        bool RefreshAimCollision(SDK::APrimalCharacter* Target, const SDK::FVector& Start) {
            auto* World = SDK::UWorld::GetWorld();
            auto* Default = SDK::APrimalCharacter::GetDefaultObj();
            if (!Target || Target->IsDead() || !Target->Mesh) return false;
            const auto End = Target->K2_GetActorLocation();
            if (!World || !Default || !Default->Class || !Finite(Start) || !Finite(End)) return false;
            static SDK::UFunction* Function = nullptr;
            if (!Function) Function = Default->Class->GetFunction("PrimalCharacter", "ForceUpdateAimedCharacters_Single");
            SDK::Params::PrimalCharacter_ForceUpdateAimedCharacters_Single Params{};
            if (!Function || Function->Size != sizeof(Params)) return false;
            // The native instant firing path calls this local, synchronous
            // refresh before its trace (v93.9: FireWeapon +0x708682a). A generic
            // visibility trace alone does not wake throttled character collision.
            Params.World = World;
            Params.StartLoc = Start;
            Params.EndLoc = End;
            // v93.9 native +0x6330385..0x63303aa explicitly refreshes this
            // character when the flag is set, even if the coarse ray missed it
            // or reached a different creature. A ray-only refresh is insufficient
            // for small, submerged or moving targets.
            Params.IgnoreActor = Target;
            Params.bForceUpdateIgnoreActor = true;
            Params.TraceRadius = 0.0f;
            Params.bOnlyRefreshRelevancyValues = false;
            Default->ProcessEvent(Function, &Params);
            return true;
        }
        double WeaponRange(SDK::AShooterWeapon* Weapon) {
            return Weapon && Weapon->IsA(SDK::AShooterWeapon_Instant::StaticClass())
                ? static_cast<double>(Weapon->InstantConfig.WeaponRange) : INFINITY;
        }
        bool ShootingOrigin(SDK::AShooterWeapon* Weapon, SDK::FVector& Origin) {
            if (!Weapon) return false;
            if (Weapon->IsA(SDK::AShooterWeapon_Instant::StaticClass())) {
                SDK::FVector NativeDirection{};
                // Uses the same native muzzle/camera branch as FireWeapon.
                static_cast<SDK::AShooterWeapon_Instant*>(Weapon)->GetFireLocationAndDirection(&Origin, &NativeDirection);
            }
            else if (Weapon->IsA(SDK::AShooterWeapon_Projectile::StaticClass())) {
                SDK::FVector NativeDirection{};
                static_cast<SDK::AShooterWeapon_Projectile*>(Weapon)->BPGetProjectileSpawnTransform(&Origin, &NativeDirection);
            }
            else Origin = Weapon->BPGetShootingCameraLocation();
            return Finite(Origin);
        }
        float Angle(const SDK::FVector& Camera, const SDK::FVector& Location,
            const SDK::FRotator& Rotation) {
            const auto Direction = SDK::UKismetMathLibrary::Normal(Location - Camera, 0.0001f);
            const auto Forward = SDK::UKismetMathLibrary::GetForwardVector(Rotation);
            const double Dot = std::clamp(SDK::UKismetMathLibrary::Dot_VectorVector(Direction, Forward), -1.0, 1.0);
            return static_cast<float>(std::acos(Dot) * 180.0 / 3.141592653589793);
        }
        bool TargetEnabled(SDK::APrimalCharacter* Character) {
            if (Character->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                auto* Dino = static_cast<SDK::APrimalDinoCharacter*>(Character);
                if (Dino->IsBossDino()) return g_Config::bAimBosses;
                return Dino->BPIsTamed() ? g_Config::bAimTamedDinos : g_Config::bAimWildDinos;
            }
            return g_Config::bAimPlayers && Character->IsA(SDK::AShooterCharacter::StaticClass());
        }
        std::string TargetDisplayName(SDK::APrimalCharacter* Character) {
            std::string Name;
            if (Character->PlayerState && !Character->IsA(SDK::APrimalDinoCharacter::StaticClass()))
                Name = Character->PlayerState->GetPlayerName().ToString();
            if (Name.empty()) Name = Character->GetDescriptiveName().ToString();
            for (auto& Ch : Name) if (static_cast<unsigned char>(Ch) < 0x20) Ch = ' ';
            return Name.empty() ? "未命名目标" : Name;
        }
        bool CanRedirectWeapon(SDK::UObject* Weapon) {
            if (ActiveAimThread.load(std::memory_order_acquire) != GetCurrentThreadId() ||
                !g_Config::bAimbotEnabled || g_MDX12::g_MenuState::g_isOpen ||
                GetTickCount64() - AimPreparedAt > 250) return false;
            return !AimQueryInProgress && (g_Config::bAimHead || g_Config::bAimBody) &&
                Weapon && Live(AimWeapon) == Weapon && Live(LockedTarget);
        }
        bool MayOverlapCircle(SDK::APlayerController* PC, SDK::APrimalCharacter* Target, const AimCircle& Circle) {
            SDK::FVector Center{}, Extent{};
            float Radius = 0;
            SDK::UKismetSystemLibrary::GetComponentBounds(Target->Mesh, &Center, &Extent, &Radius);
            if (!Finite(Center) || !Finite(Extent) || !std::isfinite(Radius) || Radius <= 0 ||
                Extent.X < 0 || Extent.Y < 0 || Extent.Z < 0) return true;
            double MinX = INFINITY, MinY = INFINITY, MaxX = -INFINITY, MaxY = -INFINITY;
            for (int Corner = 0; Corner < 8; ++Corner) {
                const auto Position = Center + SDK::FVector{Corner & 1 ? Extent.X : -Extent.X,
                    Corner & 2 ? Extent.Y : -Extent.Y, Corner & 4 ? Extent.Z : -Extent.Z};
                SDK::FVector2D Screen{};
                if (!PC->ProjectWorldLocationToScreen(Position, &Screen, false) ||
                    !std::isfinite(Screen.X) || !std::isfinite(Screen.Y)) return true;
                MinX = std::min(MinX, Screen.X); MinY = std::min(MinY, Screen.Y);
                MaxX = std::max(MaxX, Screen.X); MaxY = std::max(MaxY, Screen.Y);
            }
            return Circle.OverlapsBounds(MinX, MinY, MaxX, MaxY);
        }
        bool RefreshShotPoint(SDK::AShooterWeapon* Weapon, const SDK::FVector& Origin, bool RequireTrace = true) {
            auto* PC = g_Util::GetLocalPC();
            auto* Human = Live(AimOwner);
            auto* Target = Live(LockedTarget);
            if (!PC || !Human || Human != LocalHuman(PC) || Human->CurrentWeapon != Weapon ||
                Human->IsDead() || !Target || Target->IsDead() || !Target->Mesh ||
                !TargetEnabled(Target)) return false;
            const bool Wild = Target->IsA(SDK::APrimalDinoCharacter::StaticClass()) &&
                !static_cast<SDK::APrimalDinoCharacter*>(Target)->BPIsTamed();
            if (!Wild && g_ESP::GetRelation(Target, Human) == g_ESP::RelationType::Team) return false;
            SDK::FVector Camera{};
            SDK::FRotator Rotation{};
            PC->GetPlayerViewPoint(&Camera, &Rotation);
            const auto Forward = SDK::UKismetMathLibrary::GetForwardVector(Rotation);
            const auto Player = Human->K2_GetActorLocation();
            int32_t Width = 0, Height = 0;
            PC->GetViewportSize(&Width, &Height);
            const auto Circle = AimCircle::FromViewport(Width, Height, g_Config::AimbotCircleRadiusPercent);
            const double MaxDistance = std::clamp(g_Config::AimbotMaxDistanceMeters, 10.0f, PhoenixInterface::MaxAimDistance) * 100.0;
            const auto Selected = SelectPreparedAimPoint(
                [&] { return RefreshAimCollision(Target, Origin); },
                [&] { return HeadBodyPoints(Target); },
                [&](const AimPoint& Point) -> double {
                    SDK::FVector2D Screen{};
                    if (!InFrontOfAimView(Point.Position, Camera, Player, Forward) ||
                        !WithinAimRange((Point.Position - Camera).Magnitude(), (Point.Position - Origin).Magnitude(),
                        MaxDistance, WeaponRange(Weapon)) || !PC->ProjectWorldLocationToScreen(Point.Position, &Screen, false)) return INFINITY;
                    return Circle.Score(Screen.X, Screen.Y);
                }, [&](const AimPoint& Point) { return RequireTrace ? TraceAimPoint(PC, Origin, Point, Target) : AimTraceState::Hit; });
            if (!Selected) return false;
            // Keep the screen selection snapshot immutable during firing.
            // A different valid point on this same actor must not move its marker.
            ShotLocation = Selected.Value.Position;
            ShotPart = Selected.Value.Part;
            return true;
        }
#include "ProjectileAim.inl"

        void RejectShotPoint() {
            // Selection is independent of the held gun. A blocked muzzle or a
            // short weapon range must not make selection flicker or switch actors.
            // Do not call StopFire from inside the weapon's firing stack.
            ShotDirectionValid = false;
            SetStatus("目标已选中，当前武器射程或发射路径不满足");
        }
    }

    bool IsSelectedTarget(SDK::AActor* Actor) {
        return SelectionThread.load(std::memory_order_acquire) == GetCurrentThreadId() && Actor && Live(LockedTarget) == Actor;
    }
    void ClearCurrentTarget() {
        ClearDirectionOverride();
        StopFire();
        LockedTarget = Weak<SDK::APrimalCharacter>(nullptr);
        LockedTargetName.clear();
        LockedTargetDistanceMeters = 0;
        LockedPart = AimBodyPart::None;
    }

    void ResetTargeting() {
        ClearCurrentTarget();
        PistolHold = {};
        HeldTarget = Weak<SDK::APrimalCharacter>(nullptr);
    }

    void Reset() {
        ResetTargeting();
        SpreadThread.store(0, std::memory_order_release);
        SpreadWeapon = Weak<SDK::AShooterWeapon>(nullptr);
    }

    std::vector<SDK::FVector> GetDefenseLockPoints(SDK::APrimalCharacter* Target, const SDK::FVector& Camera) {
        std::vector<SDK::FVector> Points;
        if (!Target || !Target->Mesh || Target->IsDead()) return Points;
        AimQueryScope Query;
        // This only refreshes local collision poses. No weapon trace, target
        // selection, direction override or automatic fire is invoked here.
        RefreshAimCollision(Target, Camera);
        for (const auto& Point : HeadBodyPoints(Target, true, true)) Points.push_back(Point.Position);
        return Points;
    }

    TargetInfo GetBestTarget() {
        TargetInfo Best{};
        AimQueryScope Query;
        if (!g_Config::bAimHead && !g_Config::bAimBody) {
            SetStatus("头部与身体均已关闭，暂停锁定");
            return Best;
        }
        auto* World = SDK::UWorld::GetWorld();
        auto* PC = g_Util::GetLocalPC();
        auto* Human = LocalHuman(PC);
        if (!World || !World->PersistentLevel || !PC || !PC->Pawn || !Human) return Best;
        auto* Weapon = Human->CurrentWeapon;
        auto* FixedTarget = PistolHold.Target ? Live(HeldTarget) : nullptr;
        if (PistolHold.Target && !FixedTarget) {
            SetStatus("固定目标已失效，松开左键后重新选取");
            return Best;
        }
        SDK::FVector Camera{};
        SDK::FRotator Rotation{};
        PC->GetPlayerViewPoint(&Camera, &Rotation);
        const auto Forward = SDK::UKismetMathLibrary::GetForwardVector(Rotation);
        const auto Player = Human->K2_GetActorLocation();
        float MaximumDistance = std::clamp(g_Config::AimbotMaxDistanceMeters, 10.0f, PhoenixInterface::MaxAimDistance) * 100.0f;
        int32_t Width = 0, Height = 0;
        PC->GetViewportSize(&Width, &Height);
        const auto Circle = AimCircle::FromViewport(Width, Height, g_Config::AimbotCircleRadiusPercent);
        if (Circle.Radius <= 0) return Best;
        AimScreenRanking Ranking{ObjectKey(Live(LockedTarget))};
        std::vector<SDK::ULevel*> Levels{World->PersistentLevel};
        if (World->Levels.Num() >= 0 && World->Levels.Num() <= 4096) {
            for (auto* Level : World->Levels)
                if (Level && Level != World->PersistentLevel) Levels.push_back(Level);
        }
        int Characters = 0, Eligible = 0, InRange = 0, InFov = 0;
        std::unordered_set<SDK::AActor*> Seen;
        for (auto* Level : Levels) {
            if (Level->Actors.Num() < 0 || Level->Actors.Num() > 200000) continue;
            for (auto* Actor : Level->Actors) {
                if (PistolHold.Target && (Actor != FixedTarget || !PistolHold.Allows(ObjectKey(Actor)))) continue;
                if (!Actor || Actor == PC->Pawn || Actor == Human || !Actor->Class || Actor->bHidden ||
                    (Actor->Flags & SDK::EObjectFlags::BeginDestroyed) ||
                    (Actor->Flags & SDK::EObjectFlags::FinishDestroyed) ||
                    !Actor->IsA(SDK::APrimalCharacter::StaticClass()) || !Seen.insert(Actor).second) continue;
                auto* Character = static_cast<SDK::APrimalCharacter*>(Actor);
                ++Characters;
                if (Character->IsDead() || !TargetEnabled(Character) || !Character->Mesh) continue;
                const bool Wild = Character->IsA(SDK::APrimalDinoCharacter::StaticClass()) &&
                    !static_cast<SDK::APrimalDinoCharacter*>(Character)->BPIsTamed();
                // Passive wild dinos can be considered "friendly" by combat AI.
                // Only owned characters participate in the tribe exclusion.
                if (!Wild && g_ESP::GetRelation(Character, Human) == g_ESP::RelationType::Team) continue;
                ++Eligible;
                const auto Origin = Actor->K2_GetActorLocation();
                if (!Finite(Origin) || (Origin - Camera).Magnitude() > MaximumDistance + 5000.0) continue;
                // Only a broad mesh-bounds check precedes collision preparation.
                // Old bone centers must not reject a creature that moved into the circle.
                if (!MayOverlapCircle(PC, Character, Circle)) continue;
                bool HasRange = false, HasCircle = false;
                const auto Selected = SelectScreenAimPoint(
                    [&] { return RefreshAimCollision(Character, Camera); },
                    [&] { return HeadBodyPoints(Character); },
                    [&](const AimPoint& Candidate) -> double {
                        const auto& Point = Candidate.Position;
                        if (!InFrontOfAimView(Point, Camera, Player, Forward) ||
                            !WithinAimRange((Point - Camera).Magnitude(), (Point - Camera).Magnitude(),
                                MaximumDistance, INFINITY)) return INFINITY;
                        HasRange = true;
                        SDK::FVector2D Screen{};
                        if (!PC->ProjectWorldLocationToScreen(Point, &Screen, false)) return INFINITY;
                        const double Score = Circle.Score(Screen.X, Screen.Y);
                        if (!std::isfinite(Score)) return INFINITY;
                        HasCircle = true;
                        return Score;
                    });
                if (Selected && Ranking.Consider(ObjectKey(Character), Selected.Score)) {
                    const auto& Point = Selected.Value.Position;
                    const float Distance = static_cast<float>((Point - Camera).Magnitude());
                    const float Fov = Angle(Camera, Point, Rotation);
                    Best.Character = Character;
                    Best.BestComponentLocation = Point;
                    Best.Part = Selected.Value.Part;
                    Best.Distance = Distance;
                    Best.ScreenDistance = static_cast<float>(std::sqrt(Selected.Score));
                    Best.Health = Character->GetHealth();
                    Best.bIsValid = true;
                    // Conservative body radius instead of using the entire tail /
                    // wing span as a supposedly guaranteed hit region.
                    const float RadiusAngle = static_cast<float>(std::atan2(20.0, Distance) * 180.0 / 3.141592653589793);
                    const float Spread = Weapon ? std::abs(Weapon->CurrentFiringSpread) * 57.29578f : 0.0f;
                    Best.HitChance = std::clamp(RadiusAngle / std::max(Fov + Spread, 0.001f) * 100.0f, 0.0f, 100.0f);
                    Best.bIsLocked = Best.HitChance >= g_Config::TriggerHitChance;
                }
                InRange += HasRange;
                InFov += HasCircle;
            }
        }
        LockedTargetName = Best.bIsValid ? TargetDisplayName(Best.Character) : "";
        LockedTargetDistanceMeters = Best.bIsValid ? Best.Distance * 0.01f : 0;
        SetStatus(Best.bIsValid
            ? std::format("{} {} | {:.0f} 米 | 距圆心 {:.1f} 像素", PistolHold.Active ? "左键保持目标" : "已选中", LockedTargetName, LockedTargetDistanceMeters, Best.ScreenDistance)
            : PistolHold.Target ? "保持原目标，等待重新可瞄准；松开左键可重新选取"
            : std::format("未锁定：角色 {} / 类型匹配 {} / 前方距离内 {} / 圆内头身 {}",
                Characters, Eligible, InRange, InFov));
        LockedTarget = Weak(Best.Character);
        return Best;
    }

    void OverrideWeaponAim(SDK::AShooterWeapon* Weapon, SDK::FVector& Direction) {
        if (!CanRedirectWeapon(Weapon)) return;
        AimQueryScope Query;
        SDK::FVector Origin{};
        // Re-read animated collision centers and the native origin at this
        // direction request. A 250 ms old point is not a valid moving-target aim.
        if (UsesProjectileFlight(Weapon) && g_Config::bProjectilePrediction) {
            SDK::FVector PredictedDirection = Direction;
            if (ShootingOrigin(Weapon, Origin) && PrepareProjectileShot(Weapon, Origin, PredictedDirection, 0)) {
                Direction = PredictedDirection;
                ShotDirectionValid = true;
                RedirectedShots.fetch_add(1, std::memory_order_relaxed);
            }
            else ShotDirectionValid = false;
            return;
        }
        if (!ShootingOrigin(Weapon, Origin) || !RefreshShotPoint(Weapon, Origin) ||
            !RedirectViewFromOrigin(ShotLocation, Origin, Direction)) {
            RejectShotPoint();
            return;
        }
        ShotDirectionValid = true;
        RedirectedShots.fetch_add(1, std::memory_order_relaxed);
    }

    bool IsProjectileShotEvent(SDK::UFunction* Function) {
        // Cheap pointer filter for the global dispatch hook. Caches are populated at initialization.
        return Function && (Function == ProjectileEvents[0] || Function == ProjectileEvents[1] ||
            Function == ProjectileEvents[2] || Function == ProjectileEvents[3]);
    }
    bool InitializeProjectileEvents() {
        auto* Default = SDK::AShooterWeapon_Projectile::GetDefaultObj();
        if (!Default || !Default->Class) return false;
        const char* Names[] = {"ServerFireProjectile", "ServerFireProjectileEx", "ClientsFireProjectile", "ClientsFireProjectileEx"};
        for (int i = 0; i < 4; ++i) {
            auto* Function = Default->Class->GetFunction("ShooterWeapon_Projectile", Names[i]);
            const size_t Expected = i % 2 ? sizeof(SDK::Params::ShooterWeapon_Projectile_ServerFireProjectileEx) :
                sizeof(SDK::Params::ShooterWeapon_Projectile_ServerFireProjectile);
            if (!Function || Function->Size != Expected) return false;
            // Validate named fields as well as the total size before using SDK layouts.
            int Matches = 0;
            for (auto* Field = Function->ChildProperties; Field; Field = Field->Next) {
                auto* Property = static_cast<SDK::FProperty*>(Field);
                const auto Name = Field->Name.ToString();
                if (!Field->ClassPrivate) continue;
                if ((Name == "Origin" || Name == "ShootDir") && Field->ClassPrivate->Name.ToString() == "StructProperty") {
                    auto* Struct = static_cast<SDK::FStructProperty*>(Field);
                    const bool Origin = Name == "Origin";
                    if (Property->Offset == (Origin ? 0 : sizeof(SDK::FVector)) && Property->ArrayDim == 1 &&
                        Property->ElementSize == sizeof(SDK::FVector) && Struct->Struct &&
                        Struct->Struct->GetName() == (Origin ? "Vector" : "Vector_NetQuantizeNormal")) ++Matches;
                }
                if (i % 2 && Name == "Speed" && Property->Offset == 2 * sizeof(SDK::FVector) &&
                    Property->ElementSize == sizeof(float) && Field->ClassPrivate->Name.ToString() == "FloatProperty") ++Matches;
            }
            if (Matches != (i % 2 ? 3 : 2)) return false;
            ProjectileEvents[i] = Function;
        }
        return true;
    }
    void OverrideProjectileEvent(SDK::UObject* Object, SDK::UFunction* Function, void* Parameters) {
        if (!g_Config::bProjectilePrediction || !Parameters ||
            !IsProjectileShotEvent(Function) || !CanRedirectWeapon(Object)) return;
        auto* Weapon = Live(AimWeapon);
        if (!UsesProjectileFlight(Weapon)) return;
        const bool Extended = Function == ProjectileEvents[1] || Function == ProjectileEvents[3];
        double Speed = 0;
        if (Extended) {
            float NativeSpeed = 0;
            std::memcpy(&NativeSpeed, static_cast<uint8_t*>(Parameters) + 2 * sizeof(SDK::FVector), sizeof(float));
            if (!std::isfinite(NativeSpeed)) return;
            Speed = NativeSpeed;
        }
        SDK::FVector Origin{}, Direction{};
        std::memcpy(&Origin, Parameters, sizeof(Origin));
        std::memcpy(&Direction, static_cast<uint8_t*>(Parameters) + sizeof(Origin), sizeof(Direction));
        if (!Finite(Origin) || !Finite(Direction)) return;
        AimQueryScope Query;
        if (!PrepareProjectileShot(Weapon, Origin, Direction, Speed)) { ShotDirectionValid = false; return; }
        std::memcpy(static_cast<uint8_t*>(Parameters) + sizeof(Origin), &Direction, sizeof(Direction));
        ShotDirectionValid = true;
        RedirectedProjectileEvents.fetch_add(1, std::memory_order_relaxed);
    }

    void OverrideShotgunSpread(SDK::AShooterWeapon_Instant* Weapon, float& Spread) {
        // Native instant weapons apply their random cone after GetAdjustedAim.
        // Redirecting the center alone can still scatter a bullet off a small head.
        if (g_Config::bAimRemoveSpread && CanRedirectWeapon(Weapon) && ShotDirectionValid &&
            AimPartEnabled(ShotPart, g_Config::bAimHead, g_Config::bAimBody)) {
            Spread = 0.0f;
            return;
        }
        if (SpreadThread.load(std::memory_order_acquire) != GetCurrentThreadId() ||
            !g_Config::bShotgunConverge || g_MDX12::g_MenuState::g_isOpen ||
            GetTickCount64() - SpreadPreparedAt > 250 || !Weapon || Live(SpreadWeapon) != Weapon) return;
        Spread = 0.0f;
    }

    void OverrideTekBowInstantView(SDK::UObject* ScriptObject, SDK::UFunction* ScriptNode,
        uint8_t* Locals, const uint8_t* Arguments, SDK::APrimalCharacter* Character) {
        if (!CanRedirectWeapon(ScriptObject) || !Character || Character != Live(AimOwner) ||
            !ScriptNode || !Locals || !Arguments || !ScriptNode->Outer ||
            ScriptNode->GetName() != "BPSelectProjectileToFire" ||
            ScriptNode->Outer->GetName() != "WeapTekBow_C") return;
        auto* Weapon = Live(AimWeapon);
        if (!Weapon || !Weapon->bIsAccessoryActive) return;

        // This blueprint's instant-hit branch reads character view twice, then
        // performs VTraceSingleBP_IgnoreActorsArray and RunOnServer_InstantHit.
        // Adjust its local direction immediately after the native view getter,
        // before the blueprint constructs the trace. Never change camera state
        // or fabricate hit results / extra server requests.
        struct ScriptStorage { const uint8_t* Data; int32_t Num, Max; } Script{};
        static_assert(sizeof(ScriptStorage) == 0x10);
        // Current UStruct::Script, verified against the live blueprint bytecode.
        std::memcpy(&Script, reinterpret_cast<const uint8_t*>(ScriptNode) + 0x60, sizeof(Script));
        const auto Begin = reinterpret_cast<uintptr_t>(Script.Data);
        const auto Position = reinterpret_cast<uintptr_t>(Arguments);
        if (!Script.Data || Script.Num <= 0 || Script.Num > Script.Max || Script.Max > 16 * 1024 * 1024 ||
            Position < Begin || Position - Begin > static_cast<size_t>(Script.Num)) return;
        ViewArgumentRefs Refs{};
        if (!ReadViewArgumentRefs({Script.Data, static_cast<size_t>(Script.Num)}, Position - Begin, Refs)) return;
        auto FindVector = [&](uintptr_t Ref) -> SDK::FProperty* {
            for (auto* Field = ScriptNode->ChildProperties; Field; Field = Field->Next) {
                if (reinterpret_cast<uintptr_t>(Field) != Ref) continue;
                if (!Field->ClassPrivate || Field->ClassPrivate->Name.ToString() != "StructProperty") return nullptr;
                auto* Property = static_cast<SDK::FStructProperty*>(Field);
                if (!Property->Struct || Property->Struct->GetName() != "Vector" ||
                    !ValidViewVectorOffset(Property->Offset, Property->ElementSize, Property->ArrayDim, ScriptNode->Size)) return nullptr;
                return Property;
            }
            return nullptr;
        };
        const auto* Location = FindVector(Refs.Location);
        const auto* Direction = FindVector(Refs.Direction);
        if (!Location || !Direction || std::abs(Location->Offset - Direction->Offset) < sizeof(SDK::FVector)) return;
        SDK::FVector NativeOrigin{}, NativeDirection{};
        std::memcpy(&NativeOrigin, Locals + Location->Offset, sizeof(NativeOrigin));
        AimQueryScope Query;
        if (!RefreshShotPoint(Weapon, NativeOrigin) ||
            !RedirectViewFromOrigin(ShotLocation, NativeOrigin, NativeDirection)) {
            RejectShotPoint();
            return;
        }
        std::memcpy(Locals + Direction->Offset, &NativeDirection, sizeof(NativeDirection));
        ShotDirectionValid = true;
        RedirectedTekBowViews.fetch_add(1, std::memory_order_relaxed);
    }

    std::string GetAimbotStatus() {
        std::scoped_lock Lock(StatusMutex);
        return Status;
    }

    std::string GetMagicBulletStatus() {
        if (!g_Hook::WeaponAimOK) return "武器方向接口未匹配，自动锁定暂停";
        return std::format("枪械方向已就绪：修正 {} 次 | 泰克弓即时命中{}：修正 {} 次 | 投射物发射修正 {} 次",
            RedirectedShots.load(std::memory_order_relaxed),
            g_Hook::TekBowInstantAimOK ? "已就绪" : "未匹配",
            RedirectedTekBowViews.load(std::memory_order_relaxed), RedirectedProjectileEvents.load(std::memory_order_relaxed));
    }

    void Tick() {
        auto* PC = g_Util::GetLocalPC();
        auto* Human = LocalHuman(PC);
        DWORD ForegroundPID = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &ForegroundPID);
        if (!PC || !PC->Pawn || !Human || Human->IsDead() ||
            g_MDX12::g_MenuState::g_isOpen || ForegroundPID != GetCurrentProcessId()) {
            Reset();
            // Keep the last targeting result while the settings menu is open.
            if (!g_MDX12::g_MenuState::g_isOpen) SetStatus("等待启用瞄准、存活人物及游戏前台");
            return;
        }
        if (PC->IsA(SDK::AShooterPlayerController::StaticClass()) &&
            static_cast<SDK::AShooterPlayerController*>(PC)->IsViewingInventoryUI()) {
            Reset();
            SetStatus("背包打开，瞄准暂停");
            return;
        }
        auto* Weapon = Human->CurrentWeapon;
        SpreadThread.store(0, std::memory_order_release);
        SpreadWeapon = Weak<SDK::AShooterWeapon>(nullptr);
        if (g_Config::bShotgunConverge && g_Hook::ShotgunSpreadOK && IsShotgun(Weapon)) {
            SpreadWeapon = Weak(Weapon);
            SpreadPreparedAt = GetTickCount64();
            SpreadThread.store(GetCurrentThreadId(), std::memory_order_release);
        }
        if (!g_Config::bAimbotEnabled && !g_Config::bTriggerbotEnabled) {
            ResetTargeting();
            SetStatus("等待启用自动锁定");
            return;
        }
        const bool AllowTrigger = g_Config::bTriggerbotEnabled && !g_TekSpear::IsDefending();
        if (Live(AutoFireWeapon) != Weapon || !AllowTrigger) StopFire();
        const bool StartedHold = PistolHold.Update(g_Config::bAimbotEnabled &&
            g_Config::bTekPistolHoldTarget && IsTekPistol(Weapon),
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, ObjectKey(Weapon));
        if (StartedHold || !PistolHold.Active) HeldTarget = Weak<SDK::APrimalCharacter>(nullptr);
        if (StartedHold && Live(AimWeapon) == Weapon && Live(LockedTarget)) {
            PistolHold.Capture(ObjectKey(Live(LockedTarget)));
            HeldTarget = LockedTarget;
        }
        ClearDirectionOverride();
        const auto Best = GetBestTarget();
        if (!Best.bIsValid) { ClearCurrentTarget(); return; }
        if (PistolHold.Active && !PistolHold.Target) {
            PistolHold.Capture(ObjectKey(Best.Character));
            HeldTarget = Weak(Best.Character);
        }
        const bool Redirect = g_Config::bAimbotEnabled && (g_Hook::SupportsWeaponAim(Weapon) ||
            (g_Hook::ProjectileShotEventsOK && Weapon && Weapon->IsA(SDK::AShooterWeapon_Projectile::StaticClass())));
        {
            ClearDirectionOverride();
            AimWeapon = Weak(Weapon);
            AimOwner = Weak(Human);
            AimLocation = Best.BestComponentLocation;
            LockedPart = Best.Part;
            AimPreparedAt = GetTickCount64();
            SelectionThread.store(GetCurrentThreadId(), std::memory_order_release);
            if (Redirect) ActiveAimThread.store(GetCurrentThreadId(), std::memory_order_release);
        }
        if (g_Config::bAimbotEnabled && !Redirect)
            SetStatus(std::format("已选中 {} | {:.0f} 米 | 距圆心 {:.1f} 像素；{}", LockedTargetName,
                LockedTargetDistanceMeters, Best.ScreenDistance, Weapon ? "当前武器尚未匹配方向接口" : "等待装备武器"));
        bool TriggerPathReady = false;
        if (AllowTrigger && Weapon && (Redirect || Best.bIsLocked)) {
            AimQueryScope Query;
            SDK::FVector Origin{};
            if (UsesProjectileFlight(Weapon) && g_Config::bProjectilePrediction) {
                SDK::FVector Direction{};
                TriggerPathReady = ShootingOrigin(Weapon, Origin) &&
                    PrepareProjectileShot(Weapon, Origin, Direction, 0);
            }
            else TriggerPathReady = ShootingOrigin(Weapon, Origin) && RefreshShotPoint(Weapon, Origin);
        }
        if (AllowTrigger && TriggerPathReady && Weapon && (Redirect || Best.bIsLocked) &&
            Weapon->GetAmmoReloadState() == SDK::EWeaponAmmoReloadState::Ready &&
            Weapon->GetCurrentAmmo() > 0 && Weapon->CanFire(false)) {
            if (!Live(AutoFireWeapon)) {
                Weapon->StartFire(false);
                AutoFireWeapon = Weak(Weapon);
            }
        }
        else StopFire();
    }
    void DrawOverlay(SDK::UCanvas* Canvas) {
        // This executes between Shadow::NewFrame and Render on the game thread.
        // It remains independent of the global ESP and individual ESP labels.
        if (!Canvas || !g_Config::bAimbotEnabled ||
            (!g_Config::bDrawAimCircle && !g_Config::bDrawAimTargetLine && !g_Config::bDrawAimTargetInfo) ||
            g_MDX12::g_MenuState::g_isOpen) return;
        auto* PC = g_Util::GetLocalPC();
        auto* Human = LocalHuman(PC);
        if (!PC || !Human || Human->IsDead()) return;
        if (PC->IsA(SDK::AShooterPlayerController::StaticClass()) &&
            static_cast<SDK::AShooterPlayerController*>(PC)->IsViewingInventoryUI()) return;
        int32_t Width = 0, Height = 0;
        PC->GetViewportSize(&Width, &Height);
        const auto Circle = AimCircle::FromViewport(Width, Height, g_Config::AimbotCircleRadiusPercent);
        const auto Space = AimCanvasSpace::FromViewport(Width, Height, Canvas->ClipX, Canvas->ClipY);
        if (Circle.Radius <= 0 || Space.ScaleX <= 0 || Space.ScaleY <= 0) return;
        auto* Draw = Shadow::GetBackgroundDrawList();
        if (!Draw) return;
        const auto ToCanvas = [&](double X, double Y) {
            const auto Point = Space.ToCanvas(X, Y);
            return Shadow::Vec2{static_cast<float>(Point.X), static_cast<float>(Point.Y)};
        };
        const auto Safe = [](float Value, float Low, float High, float Default) {
            return std::isfinite(Value) ? std::clamp(Value, Low, High) : Default;
        };
        const auto Color = [&](const float* Values) {
            return Shadow::Color{Safe(Values[0],0,1,1), Safe(Values[1],0,1,1),
                Safe(Values[2],0,1,1), Safe(Values[3],0,1,1)};
        };
        const auto DrawRing = [&](double X, double Y, double Radius, Shadow::Color Tint, float Thickness, int Segments) {
            auto Previous = ToCanvas(X + Radius, Y);
            for (int Index = 1; Index <= Segments; ++Index) {
                const double Radians = Index * (6.283185307179586 / Segments);
                const auto Next = ToCanvas(X + std::cos(Radians) * Radius, Y + std::sin(Radians) * Radius);
                Draw->AddLine(Previous, Next, Tint, Thickness);
                Previous = Next;
            }
        };
        if (g_Config::bDrawAimCircle)
            DrawRing(Circle.X, Circle.Y, Circle.Radius, Color(g_Config::AimCircleColor),
                Safe(g_Config::AimCircleThickness, 0.5f, 5.0f, 1.5f), 128);
        if ((!g_Config::bDrawAimTargetLine && !g_Config::bDrawAimTargetInfo) ||
            SelectionThread.load(std::memory_order_acquire) != GetCurrentThreadId() ||
            GetTickCount64() - AimPreparedAt > 250) return;
        auto* Target = Live(LockedTarget);
        if (!Target || Target->IsDead() || Live(AimWeapon) != Human->CurrentWeapon ||
            !AimPartEnabled(LockedPart, g_Config::bAimHead, g_Config::bAimBody)) return;
        SDK::FVector2D Screen{};
        SDK::FVector Camera{};
        SDK::FRotator Rotation{};
        PC->GetPlayerViewPoint(&Camera, &Rotation);
        if (!InFrontOfAimView(AimLocation, Camera, Human->K2_GetActorLocation(), SDK::UKismetMathLibrary::GetForwardVector(Rotation)) ||
            !PC->ProjectWorldLocationToScreen(AimLocation, &Screen, false) ||
            !std::isfinite(Circle.Score(Screen.X, Screen.Y))) return;
        if (g_Config::bDrawAimTargetLine) {
            const auto Tint = Color(g_Config::AimTargetLineColor);
            const float Thickness = Safe(g_Config::AimTargetLineThickness, 0.5f, 5.0f, 1.5f);
            Draw->AddLine(ToCanvas(Circle.X, Circle.Y), ToCanvas(Screen.X, Screen.Y), Tint, Thickness);
            const float MarkerSize = Safe(g_Config::AimTargetMarkerSize, 0.0f, 20.0f, 5.0f);
            if (MarkerSize > 0.0f) DrawRing(Screen.X, Screen.Y, MarkerSize, Tint, Thickness, 24);
        }
        if (g_Config::bDrawAimTargetInfo && Shadow::DefaultFont && !LockedTargetName.empty()) {
            auto* Font = Shadow::DefaultFont;
            const std::string Distance = std::format("{:.0f} 米", LockedTargetDistanceMeters);
            const float FontScale = Safe(g_Config::AimTargetInfoScale, 0.5f, 2.0f, 1.0f);
            const float MeasureScale = FontScale * Shadow::GetStyle().FontScaleDpi;
            const auto Measure = [&](const std::string& Text) {
                const auto Wide = Shadow::ToWString(Text);
                return Canvas->K2_TextSize(Font, SDK::FString(Wide.c_str()), {MeasureScale, MeasureScale});
            };
            const auto NameSize = Measure(LockedTargetName), DistanceSize = Measure(Distance);
            const double TextWidth = std::max(NameSize.X, DistanceSize.X);
            const double Gap = std::max(NameSize.Y, DistanceSize.Y) * 0.15;
            const double TextHeight = NameSize.Y + Gap + DistanceSize.Y;
            const auto Center = ToCanvas(Circle.X, Circle.Y);
            const double Radius = Circle.Radius * std::min(Space.ScaleX, Space.ScaleY);
            const auto Layout = AimLabelLayout::WithinCircle(Center.x, Center.y, Radius, TextWidth, TextHeight);
            if (Layout.Scale <= 0) return;
            const Shadow::Color White{1, 1, 1, 1}, Black{0, 0, 0, 1};
            const auto AddCentered = [&](const std::string& Text, double TextLineWidth, double Y) {
                Draw->AddText(Font, FontScale * static_cast<float>(Layout.Scale), Black, Black,
                    {static_cast<float>(Layout.X + (TextWidth - TextLineWidth) * Layout.Scale * 0.5),
                     static_cast<float>(Y)}, White, Text);
            };
            AddCentered(LockedTargetName, NameSize.X, Layout.Y);
            AddCentered(Distance, DistanceSize.X, Layout.Y + (NameSize.Y + Gap) * Layout.Scale);
        }
    }

}
