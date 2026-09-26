#pragma once
#include "../Config/InterfaceRules.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace g_Hack::Harvest {
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    // Repeated obstacle reports may bring a deadline forward, never postpone it.
    inline Time BringForward(Time Existing, Time Requested) {
        return std::min(Existing, Requested);
    }
    inline std::chrono::milliseconds ReplanDelay(bool Complete, bool MakingProgress,
        bool Recovering, bool Experimental) {
        if (Recovering) return std::chrono::milliseconds(250);
        if (Complete && MakingProgress) return std::chrono::milliseconds(2800);
        return std::chrono::milliseconds(Experimental ? 850 : 1400);
    }
    inline bool IdleExpired(Time Now, Time Started, Time Progress,
        std::chrono::milliseconds Limit) {
        return Started != Time{} && Now - std::max(Started, Progress) > Limit;
    }
    inline Time NextTargetAttack(Time Previous, Time Now) {
        return std::max(Previous, Now + std::chrono::milliseconds(60));
    }
    inline std::chrono::milliseconds EmptySearchDelay(unsigned EmptySearches) {
        return std::chrono::milliseconds(250 * (1u << std::min(EmptySearches, 3u)));
    }
    inline std::chrono::milliseconds MeleeFeedbackDelay(int IntervalMs) {
        return std::chrono::milliseconds(std::max(2400, IntervalMs * 2 + 250));
    }
    inline std::chrono::milliseconds MeleeIdleLimit(int IntervalMs, bool Aquatic) {
        return std::chrono::milliseconds(std::max(Aquatic ? 15000 : 10000, IntervalMs * 3 + 1000));
    }
    // A reflected melee sweep radius is socket-relative. The body-edge offset
    // is only a conservative approach estimate; inventory gains confirm reach.
    inline float MeleeApproachRange(float Requested, float NativeRadiusMeters, float CapsuleRadiusMeters) {
        Requested = std::isfinite(Requested) ? std::clamp(Requested, 1.5f, PhoenixInterface::MaxHarvestApproachMeters) : 4.5f;
        const float Body = std::isfinite(CapsuleRadiusMeters) ? std::clamp(CapsuleRadiusMeters, .25f, 8.f) : .75f;
        const float Estimated = std::isfinite(NativeRadiusMeters) && NativeRadiusMeters > 0
            ? NativeRadiusMeters + Body * .65f : std::max(4.5f, Body + 1.f);
        return std::min(Requested, std::max(1.5f, Estimated));
    }
    inline float RefineMeleeRange(float Current, float CapsuleRadiusMeters) {
        const float Floor = std::clamp(std::isfinite(CapsuleRadiusMeters) ? CapsuleRadiusMeters * .5f : 1.5f, 1.5f, 4.f);
        return std::min(Current, std::max(Floor, Current * .65f));
    }
    inline float RefineBeamRange(float Current) { return std::min(Current, std::max(10.f, Current * .65f)); }
    template<class Points, class Distance> std::vector<double> PathSuffix(const Points& Path, Distance&& Between) {
        std::vector<double> Result(Path.size(), 0);
        for(size_t I=Path.size();I>1;--I) {
            const double Segment=Between(Path[I-2],Path[I-1]);
            Result[I-2]=std::isfinite(Segment)&&Segment>=0 ? Segment+Result[I-1] : std::numeric_limits<double>::infinity();
        }
        return Result;
    }
    // One shortlist route and at most two committed navigation queries per
    // update. These count bounds remain effective when a native call is slow.
    inline constexpr unsigned CandidateRoutesPerUpdate = 1;
    inline constexpr unsigned NavigationQueriesPerRefresh = 2;
    inline constexpr unsigned GroundSelectionSegments = 3;
    struct InventoryProgress {
        using Snapshot = std::unordered_map<uintptr_t, int64_t>;
        Snapshot Previous;
        bool Valid = false;
        bool Observe(Snapshot Current) {
            bool Gain = false;
            if (Valid) for (const auto& [Type, Quantity] : Current) {
                auto It = Previous.find(Type);
                const auto Before = It == Previous.end() ? 0 : It->second;
                if (Quantity > Before) { Gain = true; break; }
            }
            Previous = std::move(Current);
            Valid = true;
            return Gain;
        }
    };
    struct RouteCost {
        bool ReachesHarvestRange = false;
        double Travel = 0;
        double Direct = 0;
    };
    inline bool PreferRoute(const RouteCost& Candidate, const RouteCost& Best) {
        if (!std::isfinite(Candidate.Travel) || !std::isfinite(Candidate.Direct) ||
            Candidate.Travel < 0 || Candidate.Direct < 0) return false;
        if (Candidate.ReachesHarvestRange != Best.ReachesHarvestRange)
            return Candidate.ReachesHarvestRange;
        if (Candidate.Travel != Best.Travel) return Candidate.Travel < Best.Travel;
        return Candidate.Direct < Best.Direct;
    }
    struct DetourCost { bool ContinuationClear = false; double Alignment = -2; bool PreferredSide = false; };
    inline bool PreferDetour(DetourCost Candidate, DetourCost Best) {
        if (!std::isfinite(Candidate.Alignment)) return false;
        if (Candidate.ContinuationClear != Best.ContinuationClear) return Candidate.ContinuationClear;
        if (std::abs(Candidate.Alignment - Best.Alignment) > 0.01) return Candidate.Alignment > Best.Alignment;
        return Candidate.PreferredSide && !Best.PreferredSide;
    }
}
