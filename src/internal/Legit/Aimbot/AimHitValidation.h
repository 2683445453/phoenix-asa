#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace g_Aimbot {
    enum class AimTraceState { Hit, Blocked, Unavailable };

    template<typename Point> struct PreparedAimSelection {
        Point Value{};
        double Score = std::numeric_limits<double>::infinity();
        explicit operator bool() const { return std::isfinite(Score); }
    };

    template<typename Prepare, typename Read, typename Score, typename Trace>
    auto SelectPreparedAimPoint(Prepare Refresh, Read ReadPoints, Score ScorePoint, Trace Check) {
        using Point = typename decltype(ReadPoints())::value_type;
        using Selection = PreparedAimSelection<Point>;
        // Even a successful old trace may belong to the previous animation pose.
        // Synchronize before reading OR filtering points, including at shot time.
        if (!Refresh()) return Selection{};
        std::vector<Selection> Candidates;
        for (auto PointValue : ReadPoints()) {
            const double Value = ScorePoint(PointValue);
            if (std::isfinite(Value)) Candidates.push_back({PointValue, Value});
        }
        std::stable_sort(Candidates.begin(), Candidates.end(),
            [](const Selection& A, const Selection& B) { return A.Score < B.Score; });
        for (const auto& Candidate : Candidates)
            if (Check(Candidate.Value) == AimTraceState::Hit) return Candidate;
        return Selection{};
    }

    template<typename Prepare, typename Read, typename Score>
    auto SelectScreenAimPoint(Prepare Refresh, Read ReadPoints, Score ScorePoint) {
        using Point = typename decltype(ReadPoints())::value_type;
        PreparedAimSelection<Point> Best{};
        // Acquisition describes what is selected on screen, not proof of a shot.
        // Refresh the pose before reading it, but never let a missing collision
        // refresh API or a weapon trace silently choose a different character.
        Refresh();
        for (const auto& PointValue : ReadPoints()) {
            const double Value = ScorePoint(PointValue);
            if (std::isfinite(Value) && Value >= 0 && Value < Best.Score) Best = {PointValue, Value};
        }
        return Best;
    }

    // Engine-produced weak references require a positive serial. In particular,
    // a zero-filled no-hit result is not object registry slot zero.
    template<typename WeakRef, typename Resolver, typename TypeCheck>
    auto ResolveAimHitReference(bool BlockingHit, const WeakRef& Ref,
        Resolver Resolve, TypeCheck IsExpectedType) -> decltype(Resolve(Ref)) {
        if (!BlockingHit || Ref.ObjectIndex < 0 || Ref.ObjectSerialNumber <= 0) return nullptr;
        auto* Object = Resolve(Ref); // Resolver also validates registry serial/lifetime.
        return Object && IsExpectedType(Object) ? Object : nullptr;
    }

    inline bool AimHitMatchesTarget(const void* HitActor, const void* HitComponent,
        const void* Target, const void* Mesh, const void* Capsule) {
        // Character points come from these components. An instance handle may
        // reference a manager instead of the actor, so allow component identity.
        return Target && HitComponent &&
            (HitActor == Target || HitComponent == Mesh || HitComponent == Capsule);
    }
}
