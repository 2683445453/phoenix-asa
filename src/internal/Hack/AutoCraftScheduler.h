#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>

namespace g_Hack {
    // The server may leave the last replicated queue count at 100 after its
    // inventory view closes. Keep a bounded probe alive until updates resume.
    struct AutoCraftScheduler {
        using Clock = std::chrono::steady_clock;
        Clock::time_point NextRequest = Clock::time_point::min();
        Clock::time_point NextFullQueueProbe = Clock::time_point::min();

        int Plan(int Queue, Clock::time_point Now, float IntervalSeconds) {
            if (Now < NextRequest) return 0;
            const float Interval = std::isfinite(IntervalSeconds)
                ? std::clamp(IntervalSeconds, 0.25f, 10.0f) : 1.0f;
            NextRequest = Now + std::chrono::milliseconds(static_cast<int64_t>(Interval * 1000));
            const int Batch = 100 - std::clamp(Queue, 0, 100);
            if (Batch > 0) return Batch;
            if (Now < NextFullQueueProbe) return 0;
            NextFullQueueProbe = Now + std::chrono::seconds(3);
            return 1;
        }
    };
}
