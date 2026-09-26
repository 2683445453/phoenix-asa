#pragma once
#include <algorithm>
#include <cmath>

namespace PhoenixStartup {
    struct Presentation {
        float Elapsed = 0, Alpha = 0;
        bool Finished = false, Manual = false;
        static constexpr float FadeIn = .45f, Hold = 2.8f, FadeOut = .45f;
        void Replay() { Elapsed = Alpha = 0; Finished = false; Manual = true; }
        void Dismiss() { Finished = true; Manual = false; Alpha = 0; }
        void Tick(bool Ready, bool Enabled, bool Visible, float Delta) {
            Alpha = 0;
            // Injection can finish while the loader has focus or the game is
            // minimized. Count only frames the player can actually see.
            if (!Ready || !Visible || Finished || (!Enabled && !Manual)) return;
            Elapsed += std::clamp(std::isfinite(Delta) ? Delta : 0.f, 0.f, .25f);
            if (Elapsed >= FadeIn + Hold + FadeOut) { Dismiss(); return; }
            const float T = std::min(Elapsed / FadeIn, (FadeIn + Hold + FadeOut - Elapsed) / FadeOut);
            const float X = std::clamp(T, 0.f, 1.f);
            Alpha = X * X * (3.f - 2.f * X);
        }
    };
    inline Presentation Current;
}
