#include "TekSpear.h"
#include "TekSpearRules.h"
#include "../Config/Configs.h"
#include "../Util/Util.h"
#include "../ESP/ESP.h"
#include "../Legit/Aimbot/Aimbot.h"
#include "../../external/CppSDK/SDK/ShooterGame_parameters.hpp"
#include "../../external/Minimal-D3D12-Hook-ImGui/Main/mdx12_api.h"
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <format>

namespace g_TekSpear {
    namespace {
        InputState Inputs;
        SDK::TWeakObjectPtr<SDK::AShooterPlayerController> InputOwner{};
        SDK::TWeakObjectPtr<SDK::AShooterWeapon> HeldWeapon{};
        SDK::TWeakObjectPtr<SDK::UWorld> HeldWorld{};
        SDK::TWeakObjectPtr<SDK::APrimalCharacter> MeleeTarget{};
        struct LockedTarget {
            SDK::TWeakObjectPtr<SDK::APrimalCharacter> Actor{};
            uint64_t Key = 0;
            SDK::FVector Point{};
            std::string Name;
        };
        std::vector<LockedTarget> LockedTargets;
        PendingParries Pending;
        ReturnView Turn;
        StrikeHistory Strikes;
        std::string TargetName;
        std::string SelectionStatus, OverlayHint;
        std::string ProjectileStatus = "飞行物检测未开启", ProjectileHint;
        std::unordered_map<uint64_t, ProjectileMotion> ProjectileMotions;
        SDK::TWeakObjectPtr<SDK::AShooterCharacter> LocalOwner{};
        ULONGLONG SelectionAt = 0;
        std::atomic<DWORD> SelectionThread{0};
        bool DefenseReady = false;
        struct MontageMarkers { SDK::TWeakObjectPtr<SDK::UAnimMontage> Montage{}; std::vector<double> Times; };
        std::unordered_map<uint64_t, MontageMarkers> MarkerCache;
        std::atomic<bool> ResetRequested{false};
        std::atomic<bool> RetargetRequested{false};
        std::mutex StatusMutex;
        std::string Status = "泰克矛自动防御未开启";
        double LastDeflectAt = -1, RetryAt = 0, LastConfirmedAt = 0;
        bool ParryConfirmed = false;
        SDK::FKey RightKey{}, ShiftKey{};

        void SetStatus(std::string Text) {
            std::scoped_lock Lock(StatusMutex);
            Status = std::move(Text);
        }
        void SetProjectileStatus(std::string Detail, std::string Hint) {
            std::scoped_lock Lock(StatusMutex);
            ProjectileStatus = std::move(Detail);
            ProjectileHint = std::move(Hint);
        }
        bool Usable(const SDK::UObject* Object) {
            return Object && Object->Class && !Object->IsDefaultObject() &&
                !(Object->Flags & SDK::EObjectFlags::BeginDestroyed) &&
                !(Object->Flags & SDK::EObjectFlags::FinishDestroyed) &&
                !(Object->Flags & SDK::EObjectFlags::MirroredGarbage);
        }
        SDK::FUObjectItem* Entry(int Index) {
            auto* Registry = SDK::UObject::GObjects.GetTypedPtr();
            if (!Registry || Index < 0 || Index >= Registry->NumElements) return nullptr;
            const int Chunk = Index / SDK::TUObjectArray::ElementsPerChunk;
            auto** Chunks = Registry->GetDecrytedObjPtr();
            if (!Chunks || Chunk >= Registry->NumChunks || !Chunks[Chunk]) return nullptr;
            return &Chunks[Chunk][Index % SDK::TUObjectArray::ElementsPerChunk];
        }
        template<class T> SDK::TWeakObjectPtr<T> Weak(T* Object) {
            SDK::TWeakObjectPtr<T> Ref{};
            Ref.ObjectIndex = -1;
            auto* Item = Object ? Entry(Object->Index) : nullptr;
            if (Item && Item->Object == Object) {
                Ref.ObjectIndex = Object->Index;
                std::memcpy(&Ref.ObjectSerialNumber, reinterpret_cast<const char*>(Item) + 0x10, sizeof(int));
            }
            return Ref;
        }
        template<class T> T* Live(const SDK::TWeakObjectPtr<T>& Ref) {
            if (Ref.ObjectSerialNumber <= 0) return nullptr;
            auto* Item = Entry(Ref.ObjectIndex);
            int Serial = 0;
            if (!Item) return nullptr;
            std::memcpy(&Serial, reinterpret_cast<const char*>(Item) + 0x10, sizeof(int));
            if (Serial != Ref.ObjectSerialNumber || !Usable(Item->Object)) return nullptr;
            return static_cast<T*>(Item->Object);
        }
        template<class T> uint64_t ObjectKey(T* Object) {
            const auto Ref = Weak(Object);
            if (Ref.ObjectIndex < 0 || Ref.ObjectSerialNumber <= 0) return 0;
            return (static_cast<uint64_t>(Ref.ObjectSerialNumber) << 32) |
                (static_cast<uint32_t>(Ref.ObjectIndex) + 1u);
        }
        void ClearTarget() {
            MeleeTarget = Weak<SDK::APrimalCharacter>(nullptr);
            LockedTargets.clear();
            Pending = {};
            Strikes = {};
            TargetName.clear();
            SelectionStatus.clear();
            MarkerCache.clear();
        }
        SDK::UFunction* Function(SDK::UObject* Object, const char* Name) {
            for (auto* Type = Usable(Object) ? Object->Class : nullptr; Type;
                Type = static_cast<SDK::UClass*>(Type->SuperStruct)) {
                for (auto* Field = Type->Children; Field; Field = Field->Next)
                    if (Field->HasTypeFlag(SDK::EClassCastFlags::Function) && Field->GetName() == Name)
                        return static_cast<SDK::UFunction*>(Field);
            }
            return nullptr;
        }
        Vec3 V(SDK::FVector Value) { return {Value.X, Value.Y, Value.Z}; }
        bool PhysicalRight() { return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0; }
        bool PhysicalShift() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }
        ViewRotation View(SDK::FRotator R) { return {R.Pitch, R.Yaw, R.Roll}; }
        SDK::FRotator Rotator(ViewRotation R) { return {R.Pitch, R.Yaw, R.Roll}; }
        void RestoreFacing() {
            if (!Turn.Active) return;
            auto* PC = Live(InputOwner);
            // Do not apply a previous pawn's saved view to a new possession/map.
            if (PC && PC->Pawn == Live(LocalOwner) && Live(HeldWorld) == SDK::UWorld::GetWorld())
                PC->SetControlRotation(Rotator(Turn.Restore(View(PC->GetControlRotation()))));
            Turn = {};
        }
        void FaceAttacker(SDK::AShooterPlayerController* PC, SDK::AShooterCharacter* Human) {
            auto* Enemy = Live(MeleeTarget);
            if (!g_Config::bTekSpearAutoFace || !Enemy) { RestoreFacing(); return; }
            const auto Destination = Usable(Enemy->CapsuleComponent) ? Enemy->CapsuleComponent->K2_GetComponentLocation() : Enemy->K2_GetActorLocation();
            const auto Offset = V(Destination) - V(Human->CapsuleComponent->K2_GetComponentLocation());
            PC->SetControlRotation(Rotator(Turn.Face(View(PC->GetControlRotation()), Offset)));
        }
        LockedTarget* Locked(uint64_t Key) {
            const auto It = std::find_if(LockedTargets.begin(), LockedTargets.end(), [&](const auto& T) { return T.Key == Key; });
            return It == LockedTargets.end() ? nullptr : &*It;
        }
        void ActivateTarget(const LockedTarget* Target) {
            MeleeTarget = Target ? Target->Actor : Weak<SDK::APrimalCharacter>(nullptr);
            TargetName = Target ? Target->Name : "";
        }

        // Validate reflected parameter layout before dispatching the native input
        // entry. This also avoids persisting a UFunction pointer across map loads.
        bool Send(Key Which, bool Down) {
            auto* PC = Live(InputOwner);
            if (!PC) return false;
            auto* Func = Function(PC, "SimulateInputKey");
            if (!Func) return false;
            bool HasKey = false, HasPressed = false;
            int Count = 0;
            for (auto* Field = Func->ChildProperties; Field; Field = Field->Next) {
                auto* Property = static_cast<SDK::FProperty*>(Field);
                if (!(static_cast<SDK::EPropertyFlags>(Property->PropertyFlags) & SDK::EPropertyFlags::Parm)) continue;
                ++Count;
                if (Property->Name.ToString() == "Key" && Property->Offset == 0 &&
                    Property->ElementSize == sizeof(SDK::FKey)) HasKey = true;
                if (Property->Name.ToString() == "bPressed" && Property->Offset == sizeof(SDK::FKey) &&
                    Property->ElementSize == sizeof(bool)) HasPressed = true;
            }
            if (Count != 2 || !HasKey || !HasPressed) return false;
            SDK::Params::BasePlayerController_SimulateInputKey Params{};
            Params.Key = Which == Key::Shift ? ShiftKey : RightKey;
            Params.bPressed = Down;
            PC->ProcessEvent(Func, &Params);
            return true;
        }
        bool ReadSpearState(SDK::AShooterWeapon* Weapon, SpearState& State) {
            // Only a weapon implementing the specific Tek Spear interface is
            // eligible; no display-name substring or guessed weapon-state enum.
            auto* Func = Function(Weapon, "Interface_TekSpear_GetState");
            if (!Func) return false;
            struct BoolResult { SpearField Kind; int Offset; int Mask; };
            std::vector<BoolResult> Bools;
            int Size = 0, Count = 0;
            for (auto* Field = Func->ChildProperties; Field; Field = Field->Next) {
                auto* Property = static_cast<SDK::FProperty*>(Field);
                const auto Flags = static_cast<SDK::EPropertyFlags>(Property->PropertyFlags);
                if (!(Flags & SDK::EPropertyFlags::Parm)) continue;
                if (!(Flags & SDK::EPropertyFlags::OutParm) || Property->Offset < 0 ||
                    Property->ElementSize <= 0 || Property->Offset + Property->ElementSize > 256) return false;
                ++Count;
                Size = std::max(Size, Property->Offset + Property->ElementSize);
                if (!Property->ClassPrivate || Property->ClassPrivate->Name.ToString() != "BoolProperty") continue;
                const auto Kind = StateField(Property->Name.ToString());
                if (Kind != SpearField::Unknown) {
                    if (std::any_of(Bools.begin(), Bools.end(), [&](const auto& B) { return B.Kind == Kind; })) return false;
                    const auto* Bool = static_cast<SDK::FBoolProperty*>(Property);
                    const int Offset = Property->Offset + Bool->ByteOffset;
                    const int Mask = Bool->FieldMask ? Bool->FieldMask : Bool->ByteMask;
                    if (!Mask || Offset < Property->Offset || Offset >= Property->Offset + Property->ElementSize) return false;
                    Bools.push_back({Kind, Offset, Mask});
                }
            }
            if (Count != 7 || Bools.size() != 6 || Size <= 0) return false;
            std::vector<unsigned char> Params(Size, 0);
            Weapon->ProcessEvent(Func, Params.data());
            for (const auto& Bool : Bools) {
                const bool Value = (Params[Bool.Offset] & Bool.Mask) != 0;
                State.Set(Bool.Kind, Value);
            }
            return true;
        }
        bool Friendly(SDK::AActor* Actor, SDK::AShooterCharacter* Human) {
            if (!Usable(Actor)) return false;
            if (Actor == Human || Actor == Human->CurrentWeapon) return true;
            // AI neutrality is not tribe membership. Match magic-bullet
            // eligibility for wild creatures, including passive wild dinos.
            if (Actor->IsA(SDK::APrimalDinoCharacter::StaticClass()) &&
                !static_cast<SDK::APrimalDinoCharacter*>(Actor)->BPIsTamed()) return false;
            if (Actor->IsA(SDK::APrimalCharacter::StaticClass()))
                return g_ESP::GetRelation(static_cast<SDK::APrimalCharacter*>(Actor), Human) == g_ESP::RelationType::Team;
            return Actor->TargetingTeam != 0 && (Actor->TargetingTeam == Human->TargetingTeam ||
                Human->IsAlliedWithOtherTeam(Actor->TargetingTeam));
        }
        bool IsMeleeNotify(const SDK::FAnimNotifyEvent& Event) {
            auto Matches = [](const SDK::FName& Name) {
                std::string Text = Name.ToString();
                Text.erase(std::remove_if(Text.begin(), Text.end(), [](unsigned char C) {
                    return !std::isalnum(C);
                }), Text.end());
                std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) {
                    return static_cast<char>(std::tolower(C));
                });
                // Do not mistake an attack sound, VFX or end notify for a hit.
                for (const char* Hit : {"melee", "meleeswing", "meleedamage", "meleehit",
                    "startmeleeswing", "beginmeleeswing", "startmeleetrace", "enablemeleedamage"})
                    if (Text == Hit) return true;
                return false;
            };
            if (Matches(Event.NotifyName)) return true;
            if (Usable(Event.Notify) && Event.Notify->IsA(SDK::UAnimNotify_CustomEvent::StaticClass()) &&
                Matches(static_cast<SDK::UAnimNotify_CustomEvent*>(Event.Notify)->EventName)) return true;
            return Usable(Event.NotifyStateClass) &&
                Event.NotifyStateClass->IsA(SDK::UAnimNotifyState_Custom::StaticClass()) &&
                Matches(static_cast<SDK::UAnimNotifyState_Custom*>(Event.NotifyStateClass)->EventName);
        }
        double HitTime(const SDK::FAnimNotifyEvent& Event) {
            return NotifyTime(static_cast<int>(Event.LinkMethod), Event.LinkValue,
                Event.SegmentBeginTime, Event.SegmentLength, Event.TriggerTimeOffset);
        }
        const std::vector<double>& HitMarkers(SDK::UAnimMontage* Montage) {
            const auto Key = ObjectKey(Montage);
            if (const auto It = MarkerCache.find(Key); It != MarkerCache.end() && Live(It->second.Montage) == Montage) return It->second.Times;
            if (MarkerCache.size() >= 64) MarkerCache.clear();
            auto& Cached = MarkerCache[Key];
            Cached.Montage = Weak(Montage);
            auto& MarkerTimes = Cached.Times;
            MarkerTimes.clear();
            auto Add = [&](double Time) {
                if (std::isfinite(Time) && Time >= 0 && Time <= Montage->SequenceLength)
                    MarkerTimes.push_back(Time);
            };
            if (Montage->Notifies.Num() >= 0 && Montage->Notifies.Num() <= 4096)
                for (const auto& Event : Montage->Notifies) if (IsMeleeNotify(Event)) Add(HitTime(Event));
            if (Montage->SlotAnimTracks.Num() >= 0 && Montage->SlotAnimTracks.Num() <= 64) {
                for (const auto& Track : Montage->SlotAnimTracks) {
                    if (Track.AnimTrack.AnimSegments.Num() < 0 || Track.AnimTrack.AnimSegments.Num() > 256) continue;
                    for (const auto& Segment : Track.AnimTrack.AnimSegments) {
                        auto* Sequence = Segment.AnimReference;
                        if (!Usable(Sequence) || Sequence->Notifies.Num() < 0 || Sequence->Notifies.Num() > 4096) continue;
                        const double Rate = Segment.AnimPlayRate * Sequence->RateScale;
                        const double Length = Segment.AnimEndTime - Segment.AnimStartTime;
                        if (!std::isfinite(Rate) || Rate <= 0.0001 || !std::isfinite(Length) || Length <= 0 ||
                            Segment.LoopingCount < 1 || Segment.LoopingCount > 64) continue;
                        for (const auto& Event : Sequence->Notifies) {
                            if (!IsMeleeNotify(Event)) continue;
                            const double Time = HitTime(Event);
                            if (!std::isfinite(Time) || Time < Segment.AnimStartTime || Time > Segment.AnimEndTime) continue;
                            for (int Loop = 0; Loop < Segment.LoopingCount; ++Loop)
                                Add(Segment.StartPos + (Loop * Length + Time - Segment.AnimStartTime) / Rate);
                        }
                    }
                }
            }
            std::sort(MarkerTimes.begin(), MarkerTimes.end());
            MarkerTimes.erase(std::unique(MarkerTimes.begin(), MarkerTimes.end(), [](double A, double B) {
                return std::abs(A - B) < 0.005;
            }), MarkerTimes.end());
            return MarkerTimes;
        }
        StrikeSample ReadAttack(SDK::APrimalCharacter* Enemy, double& Reach) {
            StrikeSample Sample;
            Reach = 0;
            auto* Anim = Usable(Enemy->Mesh) ? Enemy->Mesh->GetAnimInstance() : nullptr;
            auto* Montage = Usable(Anim) ? Anim->GetCurrentActiveMontage() : nullptr;
            if (Enemy->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                auto* Dino = static_cast<SDK::APrimalDinoCharacter*>(Enemy);
                Sample.Source = ObjectKey(Dino);
                Sample.Active = Dino->IsCurrentlyPlayingAttackAnimation();
                const int Index = Dino->AttackIndexOfPlayedAnimation;
                if (!Dino->AttackInfos.IsValidIndex(Index)) return Sample;
                const auto& Attack = Dino->AttackInfos[Index];
                if (Attack.MeleeDamageAmount <= 0) { Sample.Active = false; return Sample; }
                Reach = Setting(Attack.AttackRange + Attack.MeleeSwingRadius, 300, 50, 2000);
                Sample.AttackStamp = Attack.LastAttackTime;
            }
            else if (Enemy->IsA(SDK::AShooterCharacter::StaticClass())) {
                auto* Weapon = static_cast<SDK::AShooterCharacter*>(Enemy)->CurrentWeapon;
                if (!Usable(Weapon)) return Sample;
                Sample.Source = ObjectKey(Weapon);
                Sample.DamageActive = Weapon->bDoMeleeSwing;
                Sample.DamageStamp = Weapon->EndDoMeleeSwingTime;
                Sample.AttackStamp = Weapon->NextAllowedMeleeTime;
                Sample.Active = Sample.DamageActive || Weapon->IsInMeleeAttack();
                Reach = Setting(Weapon->TheMeleeSwingRadius, 250, 100, 1200);
            }
            else return Sample;
            if (Usable(Montage) && Usable(Anim) && Anim->Montage_IsPlaying(Montage, 0.0f)) {
                Sample.Montage = ObjectKey(Montage);
                Sample.Position = Anim->Montage_GetPosition(Montage);
                Sample.Rate = Anim->Montage_GetEffectivePlayRate(Montage);
                Sample.HitTimes = HitMarkers(Montage);
                if (!Sample.HitTimes.empty()) Sample.Active = true;
            }
            return Sample;
        }
        SDK::AShooterCharacter* LocalHuman(SDK::APlayerController* PC) {
            auto AsHuman = [](SDK::UObject* Object) -> SDK::AShooterCharacter* {
                return Usable(Object) && Object->IsA(SDK::AShooterCharacter::StaticClass()) &&
                    !Object->IsA(SDK::APrimalDinoCharacter::StaticClass()) ? static_cast<SDK::AShooterCharacter*>(Object) : nullptr;
            };
            if (!Usable(PC)) return nullptr;
            if (auto* Human = AsHuman(PC->Pawn)) return Human;
            if (auto* Human = AsHuman(PC->Character)) return Human;
            if (Usable(PC->Pawn) && PC->Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass()))
                return AsHuman(Live(static_cast<SDK::APrimalDinoCharacter*>(PC->Pawn)->Rider));
            return nullptr;
        }
        std::vector<SDK::ULevel*> WorldLevels(SDK::UWorld* World) {
            std::vector<SDK::ULevel*> Levels;
            if (Usable(World->PersistentLevel)) Levels.push_back(World->PersistentLevel);
            if (World->Levels.Num() >= 0 && World->Levels.Num() <= 4096)
                for (auto* Level : World->Levels)
                    if (Usable(Level) && std::find(Levels.begin(), Levels.end(), Level) == Levels.end()) Levels.push_back(Level);
            return Levels;
        }
        bool BossChain(const SDK::UClass* Class) {
            // IsBossDino() 的 vtable 槽位在 BP 恐龙 CDO 上返回 float，按 bool
            // 包装读取得到垃圾值。改用类名特征判定（与 TekSpearRules 同源）。
            for (auto* Type = Class; Type; Type = static_cast<SDK::UClass*>(Type->SuperStruct))
                if (IsBossClassName(Type->GetName())) return true;
            return false;
        }
        bool TargetEligible(SDK::APrimalCharacter* Enemy, SDK::AShooterCharacter* Human, double Range) {
            // 候选过滤与自动瞄准（Aimbot::GetBestTarget）保持同一套规则：
            // 有效性 → 隐藏/存活/网格 → 距离 → 类型开关 → 部落排除。
            // 不再额外要求 RootComponent 或 !bIsSleeping，这两项曾把单机下
            // 的全部角色拒之门外（通过筛选恒为 0）。
            if (!Usable(Enemy) || Enemy == Human || Enemy->bHidden || Enemy->IsDead() ||
                !Usable(Enemy->Mesh) ||
                !InTargetRange(V(Enemy->K2_GetActorLocation()), V(Human->K2_GetActorLocation()), Range)) return false;
            bool IsDino = false, IsBoss = false, IsTamed = false;
            if (Enemy->IsA(SDK::APrimalDinoCharacter::StaticClass())) {
                auto* Dino = static_cast<SDK::APrimalDinoCharacter*>(Enemy);
                IsDino = true;
                IsBoss = BossChain(Enemy->Class);
                IsTamed = Dino->BPIsTamed();
            }
            else if (!Enemy->IsA(SDK::AShooterCharacter::StaticClass())) return false;
            // 招架目标类型筛选：玩家 / 生物 / Boss（与自动瞄准同款开关语义）。
            if (!SpearTypeAllowed(IsDino, IsBoss, IsTamed,
                g_Config::bSpearAimPlayers, g_Config::bSpearAimCreatures, g_Config::bSpearAimBosses)) return false;
            // 被动野生生物可被战斗 AI 视为“友好”；只有归属角色参与部落排除（与自动瞄准一致）。
            return (IsDino && !IsTamed) || g_ESP::GetRelation(Enemy, Human) != g_ESP::RelationType::Team;
        }
        void UpdateTarget(SDK::UWorld* World, SDK::AShooterPlayerController* PC, SDK::AShooterCharacter* Human) {
            SelectionThread.store(GetCurrentThreadId(), std::memory_order_release);
            SelectionAt = GetTickCount64();
            if (!g_Config::bTekSpearAutoBlock) { ClearTarget(); return; }
            const double Range = Setting(g_Config::TekSpearTargetRangeMeters, 300, 5, 1000) * 100;
            const int Limit = TargetLimit(g_Config::TekSpearTargetCount);
            const auto ActiveKey = ObjectKey(Live(MeleeTarget));
            // A temporary defensive turn must not move the acquisition circle
            // and replace the original group. Still remove dead/invalid targets.
            if (Turn.Active) {
                std::erase_if(LockedTargets, [&](const auto& T) { return !TargetEligible(Live(T.Actor), Human, Range); });
                if (LockedTargets.size() > Limit) LockedTargets.resize(Limit);
                ActivateTarget(Locked(ActiveKey) ? Locked(ActiveKey) : LockedTargets.empty() ? nullptr : &LockedTargets.front());
                return;
            }
            SDK::FVector Camera{};
            SDK::FRotator Rotation{};
            PC->GetPlayerViewPoint(&Camera, &Rotation);
            const auto Forward = SDK::UKismetMathLibrary::GetForwardVector(Rotation);
            const auto Player = Human->K2_GetActorLocation();
            int32_t Width = 0, Height = 0;
            PC->GetViewportSize(&Width, &Height);
            const auto Circle = g_Aimbot::AimCircle::FromViewport(Width, Height, g_Config::AimbotCircleRadiusPercent);
            auto Points = [&](SDK::APrimalCharacter* Enemy) {
                auto Result = Usable(Enemy->Mesh) ? g_Aimbot::GetDefenseLockPoints(Enemy, Camera) : std::vector<SDK::FVector>{};
                if (Result.empty()) Result.push_back(Usable(Enemy->CapsuleComponent) ?
                    Enemy->CapsuleComponent->K2_GetComponentLocation() : Enemy->K2_GetActorLocation());
                return Result;
            };
            if (Circle.Radius <= 0) { ClearTarget(); SelectionStatus = "未锁定：等待游戏视口"; return; }
            struct Nearby { SDK::APrimalCharacter* Enemy; TargetRank Rank; };
            std::vector<Nearby> NearbyUnits;
            std::vector<LockedTarget> Candidates;
            std::unordered_set<SDK::AActor*> Seen;
            int Characters = 0, Eligible = 0, InRange = 0, InCircle = 0, TypeFiltered = 0;
            for (auto* Level : WorldLevels(World)) {
                if (Level->Actors.Num() < 0 || Level->Actors.Num() > 200000) continue;
                for (auto* Actor : Level->Actors) {
                    if (!Usable(Actor) || Actor == Human || Actor == PC->Pawn ||
                        !Actor->IsA(SDK::APrimalCharacter::StaticClass()) || !Seen.insert(Actor).second) continue;
                    ++Characters;
                    auto* Enemy = static_cast<SDK::APrimalCharacter*>(Actor);
                    if (!TargetEligible(Enemy, Human, 1.0e12)) { ++TypeFiltered; continue; }
                    ++Eligible;
                    if (!InTargetRange(V(Enemy->K2_GetActorLocation()), V(Player), Range)) continue;
                    ++InRange;
                    const auto Origin = Enemy->K2_GetActorLocation();
                    const auto Offset = V(Origin) - V(Player);
                    const auto Key = ObjectKey(Enemy);
                    int PreviousOrder = MaxLockedTargets;
                    for (size_t I = 0; I < LockedTargets.size(); ++I) if (LockedTargets[I].Key == Key) PreviousOrder = static_cast<int>(I);
                    if (Key) NearbyUnits.push_back({Enemy, {Key, Dot(Offset, Offset), PreviousOrder}});
                }
            }
            std::sort(NearbyUnits.begin(), NearbyUnits.end(), [&](const auto& A, const auto& B) {
                return CloserTarget(A.Rank, B.Rank);
            });
            // Stop refreshing poses after the configured nearest group is found.
            for (const auto& Unit : NearbyUnits) {
                g_Aimbot::AimScreenRanking PointRanking;
                SDK::FVector BestPoint{};
                for (const auto& Point : Points(Unit.Enemy)) {
                    SDK::FVector2D Screen{};
                    if (!InTargetRange(V(Point), V(Player), Range) ||
                        !g_Aimbot::InFrontOfAimView(Point, Camera, Player, Forward) ||
                        !PC->ProjectWorldLocationToScreen(Point, &Screen, false)) continue;
                    if (PointRanking.Consider(Unit.Rank.Key, Circle.Score(Screen.X, Screen.Y))) {
                        BestPoint = Point;
                    }
                }
                if (PointRanking.Target) {
                    std::string Name;
                    if (auto* Old = Locked(Unit.Rank.Key)) Name = Old->Name;
                    if (Name.empty() && Usable(Unit.Enemy->PlayerState) && !Unit.Enemy->IsA(SDK::APrimalDinoCharacter::StaticClass()))
                        Name = Unit.Enemy->PlayerState->GetPlayerName().ToString();
                    if (Name.empty()) Name = Unit.Enemy->GetDescriptiveName().ToString();
                    for (auto& C : Name) if (static_cast<unsigned char>(C) < 0x20) C = ' ';
                    if (Name.empty()) Name = "未命名目标";
                    Candidates.push_back({Weak(Unit.Enemy), Unit.Rank.Key, BestPoint, std::move(Name)});
                    ++InCircle;
                    if (Candidates.size() >= Limit) break;
                }
            }
            LockedTargets = std::move(Candidates);
            auto* Retained = Inputs.Mode == Defense::Block ? Locked(ActiveKey) : nullptr;
            ActivateTarget(Retained ? Retained : LockedTargets.empty() ? nullptr : &LockedTargets.front());
            SelectionStatus = !LockedTargets.empty() ? std::format("已锁定 {} / {} 个最近目标", LockedTargets.size(), Limit) : std::format("未锁定：角色 {} / 通过筛选 {} / 距离内 {} / 圆内头身 {}",
                Characters, Eligible, InRange, InCircle);
        }
        double ScanProjectiles(SDK::UWorld* World, SDK::AShooterPlayerController* PC, SDK::AShooterCharacter* Human, double Now) {
            double Arrival = std::numeric_limits<double>::infinity();
            if (!g_Config::bTekSpearAutoDeflect) {
                ProjectileMotions.clear();
                SetProjectileStatus("飞行物检测未开启", "");
                return Arrival;
            }
            const Vec3 Center = V(Human->CapsuleComponent->K2_GetComponentLocation());
            const Vec3 Velocity = V(Human->GetVelocity(false));
            const double Radius = Setting(Human->CapsuleComponent->GetScaledCapsuleRadius(), 35, 10, 250);
            const double Axis = std::max(0.0, Human->CapsuleComponent->GetScaledCapsuleHalfHeight() - Radius);
            const double ProjectileRange = Setting(g_Config::TekSpearProjectileRangeMeters, 60, 5, 150) * 100;
            const double Horizon = Setting(g_Config::TekSpearLookAheadSeconds, 0.35, 0.1, 1);
            std::erase_if(ProjectileMotions, [&](const auto& Entry) { return Now - Entry.second.At > 2 || Now < Entry.second.At; });
            if (ProjectileMotions.size() > 2048) ProjectileMotions.clear();
            int Found = 0, InRange = 0, Hostile = 0, Active = 0, Moving = 0, Incoming = 0, Blocked = 0;
            std::string Kind = "飞行物";
            std::unordered_set<SDK::AActor*> Seen;
            for (auto* Level : WorldLevels(World)) {
                if (Level->Actors.Num() < 0 || Level->Actors.Num() > 200000) continue;
                for (auto* Actor : Level->Actors) {
                    if (!Usable(Actor) || Actor == Human || !Usable(Actor->RootComponent) ||
                        !Actor->IsA(SDK::AShooterProjectile::StaticClass()) || !Seen.insert(Actor).second) continue;
                    ++Found;
                    const auto Location = Actor->K2_GetActorLocation();
                    const Vec3 Offset = V(Location) - Center;
                    if (!Finite(Offset) || Dot(Offset, Offset) > ProjectileRange * ProjectileRange) continue;
                    ++InRange;
                    auto* Projectile = static_cast<SDK::AShooterProjectile*>(Actor);
                    auto* Source = Live(Projectile->DamageCauser);
                    if (FriendlyProjectile(Usable(Projectile->Instigator), Friendly(Projectile->Instigator, Human),
                        Usable(Source), Friendly(Source, Human), Friendly(Projectile->Owner, Human), Friendly(Projectile, Human))) continue;
                    ++Hostile;
                    auto* Movement = Usable(Projectile->MovementComp) && Projectile->MovementComp->IsA(SDK::UProjectileMovementComponent::StaticClass()) ?
                        static_cast<SDK::UProjectileMovementComponent*>(Projectile->MovementComp) : nullptr;
                    if (!Movement) {
                        auto* Component = Projectile->GetComponentByClass(SDK::UProjectileMovementComponent::StaticClass());
                        if (Usable(Component) && Component->IsA(SDK::UProjectileMovementComponent::StaticClass())) Movement = static_cast<SDK::UProjectileMovementComponent*>(Component);
                    }
                    const bool Continues = (Movement && Movement->bShouldBounce) || Projectile->bUseMultiTraceForBlocking || Projectile->bResetHasImpactedOnMultiTraceForBlocking;
                    if (!ActiveProjectile(Projectile->bExploded, Projectile->bHasImpacted, Continues)) continue;
                    ++Active;
                    const auto Key = ObjectKey(Projectile);
                    if (!Key) continue;
                    const auto Observed = ProjectileMotions[Key].Observe(V(Location), Now);
                    auto* Primitive = Actor->RootComponent->IsA(SDK::UPrimitiveComponent::StaticClass()) ? static_cast<SDK::UPrimitiveComponent*>(Actor->RootComponent) : nullptr;
                    const bool SimulatesPhysics = Primitive && Primitive->IsSimulatingPhysics({});
                    const Vec3 Physics = SimulatesPhysics ? V(Primitive->GetPhysicsLinearVelocity({})) : Vec3{};
                    Vec3 MovementVelocity = Movement ? V(Movement->Velocity) : V(Actor->RootComponent->GetComponentVelocity());
                    const Vec3 Replicated = Projectile->bReplicateMovement ? V(Projectile->ReplicatedMovement.LinearVelocity) : Vec3{};
                    // Impacted, stopped rocks must not inherit an old in-flight LastVelocity.
                    const Vec3 Last = Projectile->bHasImpacted ? Vec3{} : V(Projectile->LastVelocity);
                    const Vec3 Speed = ProjectileVelocity(Physics, Observed, MovementVelocity, Replicated, Last);
                    if (!MovingVelocity(Speed)) continue;
                    ++Moving;
                    double GravityZ = Movement ? Movement->GetGravityZ() : 0;
                    if (SimulatesPhysics) {
                        GravityZ = 0;
                        if (Primitive->IsGravityEnabled()) {
                            auto* Settings = World->GetWorldSettings(false, false);
                            if (Usable(Settings)) GravityZ = Settings->bGlobalGravitySet ? Settings->GlobalGravityZ : Settings->WorldGravityZ;
                        }
                    }
                    // GetGravityZ already includes ProjectileGravityScale.
                    const Vec3 Gravity{0, 0, std::isfinite(GravityZ) ? GravityZ : 0};
                    SDK::FVector BoundsCenter{}, Extent{};
                    Actor->GetActorBounds(true, &BoundsCenter, &Extent, false);
                    const double BoundsRadius = Finite(V(Extent)) ? std::sqrt(Dot(V(Extent), V(Extent))) : 0;
                    const double SphereRadius = Usable(Projectile->CollisionComp) ? Projectile->CollisionComp->GetScaledSphereRadius() : 0;
                    const double ThreatRadius = ProjectileThreatRadius(SphereRadius, Projectile->TraceForBlockingRadius, Projectile->ClientSideCollisionRadius,
                        BoundsRadius, Projectile->WeaponConfig.ExplosionRadius, Projectile->bExplodeOnImpact || Projectile->WeaponConfig.ExplosionDamage > 0);
                    const double Time = ProjectileArrival(Offset, Speed - Velocity, Gravity, Radius + ThreatRadius + 25, Axis, Horizon);
                    if (!std::isfinite(Time)) continue;
                    ++Incoming;
                    if (!PC->LineOfSightTo(Actor, Human->CapsuleComponent->K2_GetComponentLocation(), false)) { ++Blocked; continue; }
                    if (Time >= Arrival) continue;
                    Arrival = Time;
                    Kind = "飞行物";
                    for (auto* Type = Actor->Class; Type; Type = static_cast<SDK::UClass*>(Type->SuperStruct)) {
                        const auto Name = g_Util::ToLower(Type->GetName());
                        if (Name.find("dilo") != std::string::npos || Name.find("poison") != std::string::npos || Name.find("venom") != std::string::npos || Name.find("spit") != std::string::npos) { Kind = "毒液弹"; break; }
                        if (Name.find("rock") != std::string::npos || Name.find("boulder") != std::string::npos || Name.find("gorilla") != std::string::npos) { Kind = "投石"; break; }
                    }
                }
            }
            const auto Hint = std::isfinite(Arrival) ? std::format("{}接近｜预计 {:.2f} 秒", Kind, Arrival) :
                Blocked ? "已检测来袭飞行物，遮挡时暂停偏转" : Moving ? "已检测飞行物，等待进入防御时机" :
                Active ? "已识别飞行物，等待有效移动数据" : InRange ? "范围内暂无有效来袭飞行物" : "检测范围内暂无飞行物";
            SetProjectileStatus(std::format("{}｜已识别 {} / 范围内 {} / 非友方 {} / 移动 {} / 预计接近 {} / 遮挡 {}",
                Hint, Found, InRange, Hostile, Moving, Incoming, Blocked), Hint);
            return Arrival;
        }
        Defense Threat(SDK::AShooterPlayerController* PC, SDK::AShooterCharacter* Human, bool& StrikeDue, double Arrival, double Now) {
            const Vec3 Center = V(Human->CapsuleComponent->K2_GetComponentLocation());
            const Vec3 Forward = V(SDK::UKismetMathLibrary::GetForwardVector(PC->GetControlRotation()));
            constexpr double Radians = 3.14159265358979323846 / 180;
            const double Radius = Setting(Human->CapsuleComponent->GetScaledCapsuleRadius(), 35, 10, 250);
            const double MeleeRange = Setting(g_Config::TekSpearMeleeRangeMeters, 4.5, 1, MaxMeleeRangeMeters) * 100;
            StrikeDue = false;
            OverlayHint = LockedTargets.empty() ? "等待目标" : "已锁定，等待下一击";
            Strikes.BeginFrame();
            std::vector<uint64_t> ReadyTargets;
            for (const auto& Target : LockedTargets) {
                auto* Enemy = Live(Target.Actor);
                if (!Enemy || !g_Config::bTekSpearAutoBlock) continue;
                double Reach;
                const StrikeSample Sample = ReadAttack(Enemy, Reach);
                const bool Due = Strikes.Observe(Target.Key, Sample, g_Config::TekSpearParryLeadSeconds);
                const Vec3 Offset = V(Enemy->K2_GetActorLocation()) - Center;
                const double AllowedRange = std::min(MeleeRange, Reach + Radius + 75);
                const bool Near = Reach > 0 && Finite(Offset) && Dot(Offset, Offset) <= AllowedRange * AllowedRange;
                const bool InFront = g_Config::bTekSpearAutoFace || Facing(Forward, Offset, std::cos(80 * Radians));
                const bool TowardUs = Facing(V(Enemy->GetActorForwardVector()), Offset * -1, std::cos(75 * Radians));
                const bool Visible = Near && InFront && PC->LineOfSightTo(Enemy, Human->CapsuleComponent->K2_GetComponentLocation(), false);
                if (Near && InFront && TowardUs && Visible) {
                    ReadyTargets.push_back(Target.Key);
                    if (Due) Pending.Add(Target.Key, Now + Strikes.DueIn, Dot(Offset, Offset));
                }
                if (Enemy == Live(MeleeTarget))
                    OverlayHint = !InTargetRange(V(Enemy->K2_GetActorLocation()), Center, MeleeRange) ? "已锁定，等待进入招架距离" :
                        !InFront ? "已锁定，请面向目标" : !TowardUs ? "已锁定，目标未朝你攻击" :
                        Reach <= 0 ? "已锁定，等待近战攻击" : !Near ? "已锁定，尚未进入攻击范围" :
                        !Visible ? "已锁定，遮挡时暂停招架" :
                        Sample.Active && Sample.HitTimes.empty() && !Sample.DamageActive ? "等待可识别的打击时机" : "已锁定，等待下一击";
            }
            Pending.Prune(Now, [&](uint64_t Key) { return std::find(ReadyTargets.begin(), ReadyTargets.end(), Key) != ReadyTargets.end(); });
            // Finish a bounded pulse before serving the next attacker. Keep all
            // targets' observations alive, and discard hits that became too late.
            if (Inputs.Mode == Defense::Block) return Defense::Block;
            if (!PhysicalShift() && Now >= Inputs.ReadyAt) {
                if (const auto Key = Pending.Take()) {
                    ActivateTarget(Locked(Key));
                    StrikeDue = Live(MeleeTarget) != nullptr;
                }
            }
            return StrikeDue ? Defense::Block : std::isfinite(Arrival) ? Defense::Deflect : Defense::None;
        }
        bool ReleaseControls(bool ClearHistory = true) {
            DefenseReady = false;
            RestoreFacing();
            if (Live(InputOwner)) {
                const bool Released = ClearHistory ? Inputs.Release(PhysicalRight(), PhysicalShift(), Send) :
                    Inputs.ReleaseForTargetChange(GetTickCount64() / 1000.0, PhysicalRight(), PhysicalShift(), Send);
                if (!Released) return false;
            }
            else Inputs = {};
            if (ClearHistory) Inputs = {};
            if (ClearHistory) Strikes = {}; // UI/manual pauses rebase observed damage windows.
            if (ClearHistory) Pending = {};
            LastDeflectAt = -1;
            RetryAt = LastConfirmedAt = 0;
            ParryConfirmed = false;
            return true;
        }
    }

    std::string GetStatus() { std::scoped_lock Lock(StatusMutex); return Status; }
    std::string GetProjectileStatus() { std::scoped_lock Lock(StatusMutex); return ProjectileStatus; }
    void RequestReset() { ResetRequested.store(true, std::memory_order_release); }
    void RequestRetarget() { RetargetRequested.store(true, std::memory_order_release); }
    bool IsDefending() {
        return Inputs.Mode != Defense::None || (DefenseReady && g_Config::bTekSpearAutoBlock && !LockedTargets.empty());
    }
    bool Reset() {
        if (!ReleaseControls()) return false;
        InputOwner = Weak<SDK::AShooterPlayerController>(nullptr);
        HeldWeapon = Weak<SDK::AShooterWeapon>(nullptr);
        HeldWorld = Weak<SDK::UWorld>(nullptr);
        LocalOwner = Weak<SDK::AShooterCharacter>(nullptr);
        SelectionAt = 0;
        SelectionThread.store(0, std::memory_order_release);
        ClearTarget();
        ProjectileMotions.clear();
        SetProjectileStatus("等待游戏内飞行物检测", "");
        return true;
    }
    void DrawOverlay(SDK::UCanvas* Canvas) {
        if (!Usable(Canvas) || (!g_Config::bTekSpearAutoBlock && !g_Config::bTekSpearAutoDeflect) || g_MDX12::g_MenuState::g_isOpen ||
            (!g_Config::bDrawTekSpearCircle && !g_Config::bDrawTekSpearTargetLine && !g_Config::bDrawTekSpearTargetInfo)) return;
        auto* BasePC = g_Util::GetLocalPC();
        if (!Usable(BasePC) || !BasePC->IsA(SDK::AShooterPlayerController::StaticClass())) return;
        auto* PC = static_cast<SDK::AShooterPlayerController*>(BasePC);
        auto* Human = LocalHuman(PC);
        if (!Human || Human->IsDead() || Human->bIsSleeping || PC->IsViewingInventoryUI() || PC->bShowMouseCursor) return;
        int32_t Width = 0, Height = 0;
        PC->GetViewportSize(&Width, &Height);
        const auto Circle = g_Aimbot::AimCircle::FromViewport(Width, Height, g_Config::AimbotCircleRadiusPercent);
        const auto Space = g_Aimbot::AimCanvasSpace::FromViewport(Width, Height, Canvas->ClipX, Canvas->ClipY);
        auto* Draw = Shadow::GetBackgroundDrawList();
        if (!Draw || Circle.Radius <= 0 || Space.ScaleX <= 0 || Space.ScaleY <= 0) return;
        const auto ToCanvas = [&](double X, double Y) {
            const auto P = Space.ToCanvas(X, Y);
            return Shadow::Vec2{static_cast<float>(P.X), static_cast<float>(P.Y)};
        };
        const auto* Color = g_Config::TekSpearTargetColor;
        const Shadow::Color Tint{static_cast<float>(Setting(Color[0], .15, 0, 1)), static_cast<float>(Setting(Color[1], .9, 0, 1)),
            static_cast<float>(Setting(Color[2], 1, 0, 1)), static_cast<float>(Setting(Color[3], .9, 0, 1))};
        const auto Ring = [&](double X, double Y, double Radius, float Thickness, int Segments) {
            auto Previous = ToCanvas(X + Radius, Y);
            for (int I = 1; I <= Segments; ++I) {
                const double A = I * (6.283185307179586 / Segments);
                const auto Next = ToCanvas(X + std::cos(A) * Radius, Y + std::sin(A) * Radius);
                Draw->AddLine(Previous, Next, Tint, Thickness);
                Previous = Next;
            }
        };
        if (g_Config::bTekSpearAutoBlock && g_Config::bDrawTekSpearCircle)
            Ring(Circle.X, Circle.Y, Circle.Radius, static_cast<float>(Setting(g_Config::AimCircleThickness, 1.5, .5, 5)), 128);
        // Target data is produced by Tick on the game thread. Do not render an
        // old actor after a respawn, world transition or stopped update hook.
        const bool Fresh = SelectionThread.load(std::memory_order_acquire) == GetCurrentThreadId() &&
            SelectionAt && GetTickCount64() - SelectionAt <= 250 && Live(LocalOwner) == Human &&
            Live(HeldWorld) == SDK::UWorld::GetWorld();
        std::vector<const LockedTarget*> DisplayTargets;
        if (Fresh && g_Config::bTekSpearAutoBlock) for (const auto& T : LockedTargets)
            if (TargetEligible(Live(T.Actor), Human, Setting(g_Config::TekSpearTargetRangeMeters, 300, 5, 1000) * 100)) DisplayTargets.push_back(&T);
        const auto ActiveKey = ObjectKey(Live(MeleeTarget));
        for (const auto* T : DisplayTargets) {
            SDK::FVector Camera{};
            SDK::FRotator Rotation{};
            SDK::FVector2D Screen{};
            PC->GetPlayerViewPoint(&Camera, &Rotation);
            const bool OnScreen = g_Aimbot::InFrontOfAimView(T->Point, Camera, Human->K2_GetActorLocation(), SDK::UKismetMathLibrary::GetForwardVector(Rotation)) &&
                PC->ProjectWorldLocationToScreen(T->Point, &Screen, false) && std::isfinite(Screen.X) && std::isfinite(Screen.Y) &&
                Screen.X >= 0 && Screen.X < Width && Screen.Y >= 0 && Screen.Y < Height;
            if (OnScreen && g_Config::bDrawTekSpearTargetLine) {
                const float Thickness = static_cast<float>(Setting(g_Config::AimTargetLineThickness, 1.5, .5, 5)) *
                    (Inputs.Mode == Defense::Block && T->Key == ActiveKey ? 1.5f : 1.0f);
                Draw->AddLine(ToCanvas(Circle.X, Circle.Y), ToCanvas(Screen.X, Screen.Y), Tint, Thickness);
                const double Marker = Setting(g_Config::AimTargetMarkerSize, 5, 0, 20);
                if (Marker > 0) Ring(Screen.X, Screen.Y, Marker, Thickness, 24);
            }
        }
        if (!g_Config::bDrawTekSpearTargetInfo || !Shadow::DefaultFont) return;
        std::vector<std::string> Lines;
        if (!DisplayTargets.empty()) {
            Lines.push_back(std::format("招架目标：{} / {}", DisplayTargets.size(), TargetLimit(g_Config::TekSpearTargetCount)));
            for (size_t I = 0; I < DisplayTargets.size(); ++I) {
                const auto* T = DisplayTargets[I];
                const auto Delta = V(Live(T->Actor)->K2_GetActorLocation()) - V(Human->K2_GetActorLocation());
                Lines.push_back(std::format("{}. {}｜{:.1f} 米{}", I + 1, T->Name, std::sqrt(Dot(Delta, Delta)) * .01,
                    Inputs.Mode == Defense::Block && T->Key == ActiveKey ? "｜正在招架" : ""));
            }
            Lines.push_back(OverlayHint);
        }
        else if (g_Config::bTekSpearAutoBlock) {
            Lines.push_back("招架：未锁定");
            Lines.push_back(Fresh ? SelectionStatus : "等待游戏内目标更新");
            if (Fresh && !OverlayHint.empty() && OverlayHint != "等待目标") Lines.push_back(OverlayHint);
        }
        if (g_Config::bTekSpearAutoDeflect) {
            Lines.push_back(Fresh && !ProjectileHint.empty() ? ProjectileHint : "等待游戏内飞行物检测");
            if (!g_Config::bTekSpearAutoBlock && Fresh && !OverlayHint.empty() && OverlayHint != "等待目标") Lines.push_back(OverlayHint);
        }
        const float FontScale = static_cast<float>(Setting(g_Config::AimTargetInfoScale, 1, .5, 2));
        const float MeasureScale = FontScale * Shadow::GetStyle().FontScaleDpi;
        std::vector<SDK::FVector2D> Sizes;
        double TextWidth = 0, LineHeight = 0;
        for (const auto& Text : Lines) {
            const auto Wide = Shadow::ToWString(Text);
            const auto Size = Canvas->K2_TextSize(Shadow::DefaultFont, SDK::FString(Wide.c_str()), {MeasureScale, MeasureScale});
            Sizes.push_back(Size);
            TextWidth = std::max(TextWidth, Size.X);
            LineHeight = std::max(LineHeight, Size.Y);
        }
        const double Step = LineHeight * 1.15, TextHeight = Step * Lines.size() - LineHeight * .15;
        const auto Center = ToCanvas(Circle.X, Circle.Y);
        const auto Layout = TargetLabelLayout(Center.x, Center.y, Circle.Radius * std::min(Space.ScaleX, Space.ScaleY), TextWidth, TextHeight);
        if (Layout.Scale <= 0) return;
        const Shadow::Color Black{0, 0, 0, 1};
        for (size_t I = 0; I < Lines.size(); ++I)
            Draw->AddText(Shadow::DefaultFont, FontScale * static_cast<float>(Layout.Scale), Black, Black,
                {static_cast<float>(Layout.X + (TextWidth - Sizes[I].X) * Layout.Scale * .5),
                 static_cast<float>(Layout.Y + I * Step * Layout.Scale)}, Tint, Lines[I]);
    }
    void Tick() {
        if (ResetRequested.exchange(false, std::memory_order_acq_rel)) {
            if (!Reset()) ResetRequested.store(true, std::memory_order_release);
            return;
        }
        if (RetargetRequested.exchange(false, std::memory_order_acq_rel)) {
            if (!ReleaseControls()) { RetargetRequested.store(true, std::memory_order_release); return; }
            ClearTarget();
        }
        auto Pause = [](const char* Text, bool KeepTarget = false) {
            if (KeepTarget) ReleaseControls(); else Reset();
            OverlayHint = Text;
            SetStatus(std::string(Text) + (KeepTarget && !TargetName.empty() ? "｜已选：" + TargetName :
                KeepTarget && !SelectionStatus.empty() ? "｜" + SelectionStatus : ""));
        };
        if (!g_Config::bTekSpearAutoBlock && !g_Config::bTekSpearAutoDeflect) {
            Pause("泰克矛自动防御未开启"); return;
        }
        DWORD ForegroundPID = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &ForegroundPID);
        auto* World = SDK::UWorld::GetWorld();
        auto* BasePC = g_Util::GetLocalPC();
        if (!Usable(World) || !Usable(BasePC) || !BasePC->IsA(SDK::AShooterPlayerController::StaticClass())) {
            Pause("等待游戏内存活人物"); return;
        }
        auto* PC = static_cast<SDK::AShooterPlayerController*>(BasePC);
        auto* Human = LocalHuman(PC);
        if (!Usable(Human) || Human->IsDead() || Human->bIsSleeping || !Usable(Human->CapsuleComponent)) {
            Pause("人物死亡或昏迷，自动防御暂停"); return;
        }
        if (Live(InputOwner) != PC || Live(LocalOwner) != Human || Live(HeldWorld) != World) {
            if (!Reset()) { SetStatus("等待释放上次防御输入"); return; }
            InputOwner = Weak(PC);
            LocalOwner = Weak(Human);
            HeldWorld = Weak(World);
            RightKey.KeyName = SDK::UKismetStringLibrary::Conv_StringToName(SDK::FString(L"RightMouseButton"));
            ShiftKey.KeyName = SDK::UKismetStringLibrary::Conv_StringToName(SDK::FString(L"LeftShift"));
        }
        if (ForegroundPID != GetCurrentProcessId() || g_MDX12::g_MenuState::g_isOpen) {
            Pause("等待游戏前台，关闭菜单后自动防御继续", true); return;
        }
        if (PC->IsViewingInventoryUI() || PC->bShowMouseCursor || PC->IsLookInputIgnored() || PC->IsMoveInputIgnored()) {
            Pause("背包或其他界面打开时暂停", true); return;
        }
        if (Usable(PC->MyHUD) && PC->MyHUD->IsA(SDK::AShooterHUD::StaticClass()) &&
            static_cast<SDK::AShooterHUD*>(PC->MyHUD)->ChatWindowHasFocus()) {
            Pause("聊天界面打开，自动防御暂停", true); return;
        }
        const double Now = GetTickCount64() / 1000.0;
        const auto PreviousTarget = ObjectKey(Live(MeleeTarget));
        if (Turn.Active && !g_Config::bTekSpearAutoFace) RestoreFacing();
        UpdateTarget(World, PC, Human);
        // Detection remains visible even if the current weapon cannot defend.
        const double Arrival = ScanProjectiles(World, PC, Human, Now);
        if (Inputs.Mode == Defense::Block && PreviousTarget != ObjectKey(Live(MeleeTarget)) && !ReleaseControls(false)) {
            SetStatus("等待释放上个目标的防御输入"); return;
        }
        if (PC->Pawn != Human) { Pause("已保留锁定，骑乘时暂停自动防御", true); return; }
        auto* Weapon = Human->CurrentWeapon;
        if (Live(HeldWeapon) != Weapon) {
            if (!ReleaseControls()) { SetStatus("等待释放上次防御输入"); return; }
            HeldWeapon = Weak(Weapon);
        }
        if (!Usable(Weapon)) { Pause("请装备泰克长矛以自动招架", true); return; }
        SpearState State;
        if (!ReadSpearState(Weapon, State)) { Pause("长矛状态未匹配，招架暂停", true); return; }
        auto* Item = Weapon->AssociatedPrimalItem;
        if (Usable(Item) && Item->bUseItemDurability && Item->ItemDurability <= Item->MinItemDurability + 0.01f) {
            Pause("泰克长矛耐久不足，自动防御暂停", true); return;
        }
        const auto StatePause = State.PauseReason(PhysicalRight());
        if (StatePause != SpearPause::None) { Pause(PauseText(StatePause), true); return; }
        DefenseReady = true;
        bool StrikeDue = false;
        Defense Desired = Threat(PC, Human, StrikeDue, Arrival, Now);
        if (std::isfinite(Arrival)) LastDeflectAt = Now;
        if (Desired == Defense::None && g_Config::bTekSpearAutoDeflect &&
            LastDeflectAt >= 0 && Now - LastDeflectAt < 0.10) Desired = Defense::Deflect;
        // A failed deflection must not suspend observation of the selected
        // melee target or suppress its next precisely timed parry.
        if (Desired == Defense::Deflect && Now < RetryAt) Desired = Defense::None;
        const bool WasParrying = Inputs.Mode == Defense::Block && Inputs.OwnRight;
        if (WasParrying && State.Blocking) ParryConfirmed = true;
        if (StrikeDue) ParryConfirmed = false;
        if (Desired == Defense::Block && !PhysicalShift()) FaceAttacker(PC, Human);
        else RestoreFacing();
        if (!Inputs.Update(Desired, Now, PhysicalRight(), PhysicalShift(), Send,
            StrikeDue, g_Config::TekSpearParryTapSeconds)) {
            Pause("游戏按键接口不可用，自动防御暂停", true); return;
        }
        if (Inputs.Mode != Defense::Block) RestoreFacing();
        const bool Confirmed = State.Deflecting;
        if (Inputs.OwnRight && Confirmed) LastConfirmedAt = Now;
        // Melee pulses expire independently of animation confirmation. Never
        // hold/retry the same strike while waiting for a server-side state.
        if (Inputs.Mode == Defense::Deflect && Inputs.OwnRight &&
            Now - std::max(Inputs.PressedAt, LastConfirmedAt) > 0.6 && !Confirmed) {
            ReleaseControls();
            RetryAt = Now + 0.5;
            OverlayHint = "未确认挥矛动作，请检查能量和键位";
            SetStatus("未确认防御动作；请检查长矛能量和右键／Shift 键位"); return;
        }
        const std::string TargetLabel = TargetName.empty() ? "" : "｜目标：" + TargetName;
        if (Inputs.Mode == Defense::Deflect) {
            OverlayHint = State.Deflecting ? "挥矛偏转状态已确认" : "飞行物接近，触发挥矛";
            SetStatus(std::string(State.Deflecting ? "飞行物威胁：挥矛偏转状态已确认" : "飞行物接近：正在触发 Shift + 右键") + TargetLabel);
        }
        else if (Inputs.Mode == Defense::Block) {
            OverlayHint = ParryConfirmed ? "本次招架动作已确认" : "本次打击：短按右键招架";
            SetStatus(std::string(ParryConfirmed ? "本次瞬间招架动作已确认，即将松开" : "本次打击：短按右键招架") + TargetLabel);
        }
        else if (WasParrying)
            SetStatus(std::string(ParryConfirmed ? "已松开右键，等待目标下一击" : "已结束本次短按，尚未确认招架动作") + TargetLabel);
        else if (!TargetName.empty()) {
            if (PhysicalShift()) OverlayHint = "手动 Shift 按住，暂停近战招架";
            SetStatus(OverlayHint + TargetLabel);
        }
        else SetStatus(SelectionStatus.empty() ? "等待来袭飞行物" : SelectionStatus);
    }
}
