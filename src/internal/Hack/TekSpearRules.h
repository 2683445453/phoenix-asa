#pragma once
#include "../Config/InterfaceRules.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include <string_view>
#include <string>
#include <iterator>
#include "../Legit/Aimbot/AimSelection.h"

namespace g_TekSpear {
    inline constexpr float MaxParryLeadSeconds = 0.30f;
    inline constexpr float MaxMeleeRangeMeters = 25.0f;
    inline constexpr int MaxLockedTargets = 5;
    inline int TargetLimit(int Value) { return std::clamp(Value, 1, MaxLockedTargets); }

    // Boss 类名识别。IsBossDino() 的 vtable 槽位在 BP 恐龙 CDO 上实际返回
    // float，按 bool 包装读取得到垃圾值，因此改用类名特征判定：
    // - 类名以 Dragon_ 开头（Dragon_Character_BP_C）。Dragontopia 的
    //   SnowDragon_Character_BP_C 以 Snow 开头，不会被误判；
    // - 或包含已知 Boss 标记（蜘蛛 / 大猩猩 / 蝎狮 / 监察者 / 君王泰坦 /
    //   渡渡霸王龙 / 泰坦 kaiju / 深海莫德尔）。
    inline bool IsBossClassName(std::string_view ClassName) {
        std::string Normal;
        Normal.reserve(ClassName.size());
        for (unsigned char C : ClassName) {
            if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
            Normal += static_cast<char>(C);
        }
        if (Normal.starts_with("dragon_")) return true;
        for (const auto Token : {"spiderl_character", "gorilla_character", "manticore_character",
            "overseer_character", "kingtitan_character", "dodorex_character",
            "kaiju_character", "moeder_character"})
            if (Normal.find(Token) != std::string::npos) return true;
        return false;
    }
    // 招架目标类型筛选（玩家 / 生物 / Boss）。Boss 只按「野生且类名特征判定为
    // Boss」认定：驯养生物即使带额外等级也归为生物，避免把普通驯养龙误判成
    // Boss 而被 Boss 开关过滤掉。
    inline bool SpearTypeAllowed(bool IsDino, bool IsBossDino, bool IsTamed,
        bool AimPlayers, bool AimCreatures, bool AimBosses) {
        if (!IsDino) return AimPlayers;
        if (IsBossDino && !IsTamed) return AimBosses;
        return AimCreatures;
    }
    struct TargetRank {
        uint64_t Key = 0;
        double DistanceSquared = 0;
        int PreviousOrder = MaxLockedTargets;
    };
    inline bool CloserTarget(const TargetRank& A, const TargetRank& B) {
        if (A.DistanceSquared != B.DistanceSquared) return A.DistanceSquared < B.DistanceSquared;
        if (A.PreviousOrder != B.PreviousOrder) return A.PreviousOrder < B.PreviousOrder;
        return A.Key < B.Key;
    }
    enum class Defense { None, Block, Deflect };
    enum class Key { Shift, RightMouse };

    struct Vec3 {
        double X = 0, Y = 0, Z = 0;
        Vec3 operator+(Vec3 B) const { return {X + B.X, Y + B.Y, Z + B.Z}; }
        Vec3 operator-(Vec3 B) const { return {X - B.X, Y - B.Y, Z - B.Z}; }
        Vec3 operator*(double S) const { return {X * S, Y * S, Z * S}; }
    };
    inline double Dot(Vec3 A, Vec3 B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
    inline bool Finite(Vec3 A) { return std::isfinite(A.X) && std::isfinite(A.Y) && std::isfinite(A.Z); }
    inline double Setting(double Value, double Default, double Min, double Max) {
        return std::isfinite(Value) ? std::clamp(Value, Min, Max) : Default;
    }
    inline bool Facing(Vec3 Forward, Vec3 Offset, double MinDot) {
        const double LengthSquared = Dot(Offset, Offset) * Dot(Forward, Forward);
        return Finite(Forward) && Finite(Offset) && LengthSquared > 0.0001 &&
            Dot(Forward, Offset) >= std::sqrt(LengthSquared) * MinDot;
    }

    inline double AngleDelta(double Angle) { return std::remainder(Angle, 360.0); }
    struct ViewRotation { double Pitch = 0, Yaw = 0, Roll = 0; };
    inline bool Finite(ViewRotation R) {
        return std::isfinite(R.Pitch) && std::isfinite(R.Yaw) && std::isfinite(R.Roll);
    }
    struct ReturnView {
        bool Active = false;
        ViewRotation Original{}, Applied{};
        void TrackUserInput(ViewRotation Current) {
            if (!Active || !Finite(Current)) return;
            Original.Pitch = std::clamp(Original.Pitch + AngleDelta(Current.Pitch - Applied.Pitch), -89.0, 89.0);
            Original.Yaw = AngleDelta(Original.Yaw + AngleDelta(Current.Yaw - Applied.Yaw));
            Original.Roll = AngleDelta(Original.Roll + AngleDelta(Current.Roll - Applied.Roll));
        }
        ViewRotation Face(ViewRotation Current, Vec3 Offset) {
            if (!Finite(Current) || !Finite(Offset) || Dot(Offset, Offset) < .001) return Current;
            if (!Active) { Original = {AngleDelta(Current.Pitch), AngleDelta(Current.Yaw), AngleDelta(Current.Roll)}; Active = true; }
            else TrackUserInput(Current);
            constexpr double Degrees = 180.0 / 3.14159265358979323846;
            Applied = {std::clamp(std::atan2(Offset.Z, std::hypot(Offset.X, Offset.Y)) * Degrees, -89.0, 89.0),
                std::atan2(Offset.Y, Offset.X) * Degrees, Current.Roll};
            return Applied;
        }
        ViewRotation Restore(ViewRotation Current) {
            if (!Active) return Current;
            TrackUserInput(Current);
            const auto Result = Original;
            *this = {};
            return Result;
        }
    };

    struct PendingParries {
        struct Strike { uint64_t Key; double HitAt, DistanceSquared; };
        std::vector<Strike> Entries;
        void Add(uint64_t Key, double HitAt, double DistanceSquared) {
            if (!Key || !std::isfinite(HitAt) || !std::isfinite(DistanceSquared)) return;
            for (auto& Entry : Entries) if (Entry.Key == Key) {
                if (HitAt < Entry.HitAt) Entry = {Key, HitAt, DistanceSquared};
                return;
            }
            if (Entries.size() < MaxLockedTargets) Entries.push_back({Key, HitAt, DistanceSquared});
        }
        template<class Eligible> void Prune(double Now, Eligible Valid) {
            std::erase_if(Entries, [&](const auto& E) { return Now > E.HitAt + .025 || !Valid(E.Key); });
        }
        uint64_t Take() {
            if (Entries.empty()) return 0;
            const auto It = std::min_element(Entries.begin(), Entries.end(), [](const auto& A, const auto& B) {
                if (A.HitAt != B.HitAt) return A.HitAt < B.HitAt;
                if (A.DistanceSquared != B.DistanceSquared) return A.DistanceSquared < B.DistanceSquared;
                return A.Key < B.Key;
            });
            const auto Key = It->Key;
            Entries.erase(It);
            return Key;
        }
    };

    inline bool MovingVelocity(Vec3 Velocity) {
        const double SpeedSquared = Dot(Velocity, Velocity);
        return Finite(Velocity) && SpeedSquared >= 1 && SpeedSquared <= 4.0e12;
    }
    struct ProjectileMotion {
        Vec3 Position{}, Measured{};
        double At = -1, MovedAt = -1;

        Vec3 Observe(Vec3 Current, double Now) {
            if (!Finite(Current) || !std::isfinite(Now)) return {};
            const double Dt = Now - At;
            if (At >= 0 && Dt >= .002 && Dt <= .25) {
                const Vec3 Velocity = (Current - Position) * (1.0 / Dt);
                if (MovingVelocity(Velocity)) { Measured = Velocity; MovedAt = Now; }
            }
            else if (At >= 0 && (Dt < 0 || Dt > .25)) { Measured = {}; MovedAt = -1; }
            if (At < 0 || Dt < 0 || Dt >= .002) { Position = Current; At = Now; }
            return MovedAt >= 0 && Now - MovedAt <= .15 ? Measured : Vec3{};
        }
    };
    inline Vec3 ProjectileVelocity(Vec3 Physics, Vec3 Observed, Vec3 Movement, Vec3 Replicated, Vec3 Last) {
        for (const auto Value : {Physics, Observed, Movement, Replicated, Last})
            if (MovingVelocity(Value)) return Value;
        return {};
    }
    inline bool FriendlyProjectile(bool HasInstigator, bool InstigatorFriendly,
        bool HasDamageCauser, bool DamageCauserFriendly, bool OwnerFriendly, bool TeamFriendly) {
        // The firing pawn is authoritative. A locally owned replication proxy
        // must not override a known enemy instigator and hide its projectile.
        if (HasInstigator) return InstigatorFriendly;
        if (HasDamageCauser) return DamageCauserFriendly;
        return OwnerFriendly || TeamFriendly;
    }
    inline bool ActiveProjectile(bool Exploded, bool Impacted, bool ContinuesAfterImpact) {
        return !Exploded && (!Impacted || ContinuesAfterImpact);
    }
    inline double ProjectileThreatRadius(double SphereRadius, double TraceRadius, double ClientRadius,
        double CollidingBoundsRadius, double BlastRadius, bool ExplodesOnImpact) {
        return std::max({Setting(SphereRadius, 0, 0, 2000), Setting(TraceRadius, 0, 0, 2000),
            Setting(ClientRadius, 0, 0, 2000), Setting(CollidingBoundsRadius, 0, 0, 2000),
            ExplodesOnImpact ? Setting(BlastRadius, 0, 0, 5000) : 0.0, 10.0});
    }

    inline bool InTargetRange(Vec3 Point, Vec3 Player, double Range) {
        const auto Offset = Point - Player;
        return Finite(Offset) && std::isfinite(Range) && Range > 0 && Dot(Offset, Offset) <= Range * Range;
    }

    // Acquisition deliberately has no weapon, melee reach or visibility gate.
    // Those decide whether to parry the retained target, never whom to select.
    struct TargetSelection {
        g_Aimbot::AimCircle Circle;
        double Range = 30000;
        g_Aimbot::AimScreenRanking Ranking;

        bool Consider(uint64_t Key, Vec3 Origin, Vec3 Point, Vec3 Camera, Vec3 Player, Vec3 Forward, double X, double Y) {
            const auto Offset = Origin - Player;
            // Circle coordinates establish eligibility only. Units are ranked
            // by their actual distance to the player, never camera/reticle distance.
            return InTargetRange(Origin, Player, Range) && InTargetRange(Point, Player, Range) &&
                g_Aimbot::InFrontOfAimView(Point, Camera, Player, Forward) &&
                std::isfinite(Circle.Score(X, Y)) && Ranking.Consider(Key, Dot(Offset, Offset));
        }
    };

    inline g_Aimbot::AimLabelLayout TargetLabelLayout(double X, double Y, double Radius, double Width, double Height) {
        auto Layout = g_Aimbot::AimLabelLayout::WithinCircle(X, Y, Radius, Width, Height);
        // Magic-bullet information occupies the lower half of the same circle.
        Layout.Y = 2 * Y - Layout.Y - Height * Layout.Scale;
        return Layout;
    }

    enum class SpearField { Unknown, Blocking, Deflecting, Pullback, Charging, Releasing, Dodging };
    inline SpearField StateField(std::string_view Name) {
        std::string Normal;
        for (unsigned char C : Name) {
            if (C >= 'A' && C <= 'Z') C += 'a' - 'A';
            if ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9')) Normal += static_cast<char>(C);
        }
        if (Normal == "isblocking") return SpearField::Blocking;
        if (Normal == "isdeflecting") return SpearField::Deflecting;
        if (Normal == "pullback" || Normal == "isidlegroundmeleepullback") return SpearField::Pullback;
        if (Normal == "ismidairstabcharging") return SpearField::Charging;
        if (Normal == "ismidairstabreleasing") return SpearField::Releasing;
        if (Normal == "isdoding" || Normal == "isdodging") return SpearField::Dodging;
        return SpearField::Unknown;
    }

    enum class SpearPause { None, ManualRight, AirCharging, AirReleasing, Dodging };
    struct SpearState {
        bool Blocking = false, Deflecting = false;
        bool GroundAnimationGroup = false, Charging = false, Releasing = false, Dodging = false;

        void Set(SpearField Field, bool Value) {
            switch (Field) {
            case SpearField::Blocking: Blocking = Value; break;
            case SpearField::Deflecting: Deflecting = Value; break;
            case SpearField::Pullback: GroundAnimationGroup = Value; break;
            case SpearField::Charging: Charging = Value; break;
            case SpearField::Releasing: Releasing = Value; break;
            case SpearField::Dodging: Dodging = Value; break;
            default: break;
            }
        }

        SpearPause PauseReason(bool PhysicalRight) const {
            if (PhysicalRight) return SpearPause::ManualRight;
            // The live blueprint calls this output "is idle/ground melee/pullback".
            // It is an animation group, NOT proof of an active pullback/action.
            // Blocking and deflecting can also be our own automatic input.
            if (Charging) return SpearPause::AirCharging;
            if (Releasing) return SpearPause::AirReleasing;
            if (Dodging) return SpearPause::Dodging;
            return SpearPause::None;
        }
    };

    inline const char* PauseText(SpearPause Reason) {
        switch (Reason) {
        case SpearPause::ManualRight: return "检测到鼠标右键按下，松开后恢复自动招架";
        case SpearPause::AirCharging: return "长矛空中刺击蓄力中，结束后恢复自动防御";
        case SpearPause::AirReleasing: return "长矛空中刺击释放中，结束后恢复自动防御";
        case SpearPause::Dodging: return "长矛闪避动作中，结束后恢复自动防御";
        default: return "";
        }
    }

    struct StrikeSample {
        uint64_t Source = 0, Montage = 0;
        bool Active = false, DamageActive = false;
        double AttackStamp = 0, DamageStamp = 0, Position = -1, Rate = 0;
        std::vector<double> HitTimes;
    };

    inline double NotifyTime(int LinkMethod, double Value, double SegmentStart,
        double SegmentLength, double TriggerOffset) {
        if (!std::isfinite(Value) || !std::isfinite(SegmentStart) ||
            !std::isfinite(SegmentLength) || !std::isfinite(TriggerOffset))
            return std::numeric_limits<double>::infinity();
        if (LinkMethod == 0) return Value + TriggerOffset;
        if (LinkMethod == 1) return SegmentStart + Value + TriggerOffset;
        if (LinkMethod == 2 && SegmentLength > 0) return SegmentStart + SegmentLength * Value + TriggerOffset;
        return std::numeric_limits<double>::infinity();
    }

    // Observe even when the target is occluded or looking away. Otherwise an
    // old, already active swing would look like a new strike on re-entry.
    struct StrikeTracker {
        StrikeSample Previous;
        size_t NextHit = 0;
        bool Initialized = false;
        double DueIn = 0;

        bool Observe(const StrikeSample& Sample, double Lead, bool AllowDamageEdge = true) {
            const bool SameSource = Initialized && Sample.Source && Sample.Source == Previous.Source;
            const bool Rewind = Sample.Montage && Sample.Montage == Previous.Montage &&
                Sample.Position + 0.08 < Previous.Position;
            const bool NewAnimation = !SameSource || !Previous.Active ||
                Sample.Montage != Previous.Montage || Rewind ||
                (Sample.AttackStamp > Previous.AttackStamp + 0.0001 && Sample.Position <= Previous.Position);
            if (NewAnimation) NextHit = 0;
            bool Due = false;
            DueIn = 0;
            if (Sample.Active && Sample.Montage && !Sample.HitTimes.empty()) {
                if (std::isfinite(Sample.Position) && Sample.Position >= 0 &&
                    std::isfinite(Sample.Rate) && Sample.Rate > 0.0001) {
                    Lead = Setting(Lead, 0.04, 0, MaxParryLeadSeconds);
                    while (NextHit < Sample.HitTimes.size()) {
                        const double Until = (Sample.HitTimes[NextHit] - Sample.Position) / Sample.Rate;
                        if (std::isfinite(Until) && Until > Lead) break;
                        // Do not replay a hit when acquiring a target mid-swing,
                        // or queue a stale hit after a long frame/network jump.
                        if (std::isfinite(Until) && Until >= -0.025 && (SameSource || Until >= 0)) {
                            if (!Due) DueIn = Until;
                            Due = true;
                        }
                        ++NextHit;
                    }
                }
            }
            else if (AllowDamageEdge && SameSource && Sample.DamageActive) {
                // Native damage-window edge is the fallback for weapons whose
                // animation does not expose a recognized melee hit notify.
                Due = !Previous.DamageActive || (std::isfinite(Sample.DamageStamp) &&
                    Sample.DamageStamp > Previous.DamageStamp + 0.0001);
            }
            Previous = Sample;
            Initialized = true;
            return Due;
        }
    };

    struct SingleTarget {
        uint64_t Key = 0;
        // Selection is recomputed each update; only one target is active.
        bool Select(uint64_t Candidate) {
            const bool Changed = Key != Candidate;
            Key = Candidate;
            return Changed;
        }
    };

    struct StrikeHistory {
        struct Entry { uint64_t Key = 0, Seen = 0, Frame = 0; StrikeTracker Tracker; };
        std::vector<Entry> Entries;
        uint64_t Frame = 0, Serial = 0;
        double DueIn = 0;

        void BeginFrame() { ++Frame; }
        void Deactivate() { ++Frame; }
        bool Observe(uint64_t Key, const StrikeSample& Sample, double Lead) {
            if (!Key) { Deactivate(); return false; }
            auto Found = std::find_if(Entries.begin(), Entries.end(), [&](const auto& E) { return E.Key == Key; });
            if (Found == Entries.end()) {
                if (Entries.size() < 64) {
                    Entries.push_back({Key});
                    Found = std::prev(Entries.end());
                }
                else {
                    Found = std::min_element(Entries.begin(), Entries.end(), [](const auto& A, const auto& B) { return A.Seen < B.Seen; });
                    *Found = Entry{Key};
                }
            }
            Found->Seen = ++Serial;
            // Keep consumed montage markers across automatic target switches.
            // A native damage window already active on re-entry is only rebased.
            const bool Due = Found->Tracker.Observe(Sample, Lead, Found->Frame != 0 && Found->Frame + 1 == Frame);
            Found->Frame = Frame;
            DueIn = Found->Tracker.DueIn;
            return Due;
        }
    };

    // Earliest segment intersection with a vertical capsule centered at zero.
    // Continuous sweeps prevent fast projectiles from tunneling between samples.
    inline double CapsuleEntry(Vec3 P, Vec3 D, double Radius, double AxisHalfHeight) {
        double Best = std::numeric_limits<double>::infinity();
        const Vec3 Nearest{0, 0, std::clamp(P.Z, -AxisHalfHeight, AxisHalfHeight)};
        if (Dot(P - Nearest, P - Nearest) <= Radius * Radius) return 0;
        auto Root = [](double A, double B, double C) {
            const double Disc = B * B - 4 * A * C;
            return A > 1e-12 && Disc >= 0 ? (-B - std::sqrt(Disc)) / (2 * A) :
                std::numeric_limits<double>::infinity();
        };
        const double Cylinder = Root(D.X * D.X + D.Y * D.Y,
            2 * (P.X * D.X + P.Y * D.Y), P.X * P.X + P.Y * P.Y - Radius * Radius);
        if (Cylinder >= 0 && Cylinder <= 1 &&
            std::abs(P.Z + Cylinder * D.Z) <= AxisHalfHeight) Best = Cylinder;
        for (double Z : {-AxisHalfHeight, AxisHalfHeight}) {
            const Vec3 Offset = P - Vec3{0, 0, Z};
            const double T = Root(Dot(D, D), 2 * Dot(Offset, D), Dot(Offset, Offset) - Radius * Radius);
            if (T >= 0 && T <= 1) Best = std::min(Best, T);
        }
        return Best;
    }
    inline double ProjectileArrival(Vec3 Position, Vec3 RelativeVelocity, Vec3 Acceleration,
        double Radius, double AxisHalfHeight, double Horizon) {
        const double None = std::numeric_limits<double>::infinity();
        if (!Finite(Position) || !Finite(RelativeVelocity) || !Finite(Acceleration) ||
            !std::isfinite(Radius) || !std::isfinite(AxisHalfHeight) || !std::isfinite(Horizon) ||
            Radius <= 0 || AxisHalfHeight < 0 || Horizon <= 0 || Horizon > 2) return None;
        if (CapsuleEntry(Position, {}, Radius, AxisHalfHeight) == 0) return 0;
        if (Dot(Position, RelativeVelocity) >= 0 && Dot(Acceleration, Acceleration) < 1) return None;
        const int Steps = std::clamp(static_cast<int>(std::ceil(Horizon / 0.02)), 1, 100);
        const double Dt = Horizon / Steps;
        Vec3 Previous = Position;
        // A curved path deviates from each segment by at most |a| dt^2 / 8.
        const double CurveMargin = std::sqrt(Dot(Acceleration, Acceleration)) * Dt * Dt / 8;
        for (int I = 1; I <= Steps; ++I) {
            const double T = I * Dt;
            const Vec3 Next = Position + RelativeVelocity * T + Acceleration * (0.5 * T * T);
            const double Entry = CapsuleEntry(Previous, Next - Previous, Radius + CurveMargin, AxisHalfHeight);
            if (std::isfinite(Entry)) return (I - 1 + Entry) * Dt;
            Previous = Next;
        }
        return None;
    }

    // Native input never alters OS key state, so physical keys can be respected.
    // Enter the held block first, then add Shift for the deflect spin. Upgrading
    // an existing block preserves the held right button to avoid a defense gap.
    struct InputState {
        Defense Mode = Defense::None;
        bool OwnRight = false, OwnShift = false;
        double ReadyAt = 0, PressedAt = 0, BlockEndsAt = 0;

        template<class Emit> bool Release(bool PhysicalRight, bool PhysicalShift, Emit Send) {
            if (OwnRight && !PhysicalRight && !Send(Key::RightMouse, false)) return false;
            OwnRight = false;
            if (OwnShift && !PhysicalShift && !Send(Key::Shift, false)) return false;
            OwnShift = false;
            Mode = Defense::None;
            PressedAt = 0;
            return true;
        }
        template<class Emit> bool ReleaseForTargetChange(double Now, bool PhysicalRight, bool PhysicalShift, Emit Send) {
            const bool WasHolding = OwnRight || OwnShift;
            if (!Release(PhysicalRight, PhysicalShift, Send)) return false;
            *this = {};
            if (WasHolding) ReadyAt = Now + 0.016;
            return true;
        }
        template<class Emit> bool Update(Defense Desired, double Now,
            bool PhysicalRight, bool PhysicalShift, Emit Send,
            bool NewStrike = false, double TapSeconds = 0.08) {
            // A melee block always expires. Repeated observations of the same
            // attack can neither extend it nor re-press the right button.
            if (Mode == Defense::Block && OwnRight && (Now >= BlockEndsAt || NewStrike)) {
                if (!Release(PhysicalRight, PhysicalShift, Send)) return false;
                if (NewStrike && Desired == Defense::Block && !PhysicalRight && !PhysicalShift) {
                    Mode = Defense::Block;
                    ReadyAt = Now + 0.016;
                }
                return true;
            }
            if (Desired == Defense::Block && Mode != Defense::Block && !NewStrike) Desired = Defense::None;
            if (PhysicalRight || (Desired == Defense::Block && PhysicalShift)) Desired = Defense::None;
            if (Desired != Mode || Desired == Defense::None) {
                const bool WasHolding = OwnRight || OwnShift;
                const bool UpgradeBlock = Mode == Defense::Block && Desired == Defense::Deflect && OwnRight;
                if (!UpgradeBlock) {
                    if (!Release(PhysicalRight, PhysicalShift, Send)) return false;
                    if (Desired == Defense::None) return true;
                }
                Mode = Desired;
                ReadyAt = std::max(ReadyAt, Now + (Desired == Defense::Block && !WasHolding ? 0 : 0.016));
                if (UpgradeBlock) PressedAt = Now;
            }
            if (!OwnRight && Now >= ReadyAt) {
                if (!Send(Key::RightMouse, true)) return false;
                OwnRight = true;
                PressedAt = Now;
                BlockEndsAt = Now + Setting(TapSeconds, 0.08, 0.03, PhoenixInterface::MaxSpearTapSeconds);
                ReadyAt = Now + 0.016;
                return true;
            }
            if (Mode == Defense::Deflect && OwnRight && Now >= ReadyAt && !OwnShift && !PhysicalShift) {
                if (!Send(Key::Shift, true)) return false;
                OwnShift = true;
            }
            return true;
        }
    };
}
