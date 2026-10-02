#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace g_Hack {
    inline bool IsObeliskTerminalIdentity(std::string Identity) {
        std::transform(Identity.begin(), Identity.end(), Identity.begin(),
            [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        for (const char* Excluded : {"transmitter", "tekterminal", "supplycrate", "cityterminal", "泰克传送器", "泰克传输器"})
            if (Identity.find(Excluded) != std::string::npos) return false;
        return Identity.find("obelisk") != std::string::npos ||
            Identity.find("tributeterminal") != std::string::npos || Identity.find("方尖碑") != std::string::npos;
    }
    inline float RemoteViewRange(double Distance) {
        return std::isfinite(Distance) && Distance >= 0 && Distance < 1.e12
            ? static_cast<float>(Distance + 5000.0) : 0;
    }
    class RemotePanelSession {
        uint64_t OpenedAt = 0;
        bool Seen = false;
    public:
        void Open(uint64_t Now) { OpenedAt = Now; Seen = false; }
        bool Keep(uint64_t Now, bool IsCurrentUI, bool Visible, bool Closing, bool AnotherUI) {
            if (Closing || AnotherUI) return false;
            if (IsCurrentUI) { Seen = true; return true; }
            if (Visible) return true;
            return !Seen && Now >= OpenedAt && Now - OpenedAt < 2000;
        }
    };

    // One atomic state covers both queued clicks and an active game-thread round.
    // A click on the final batch cannot remain queued for a second round.
    class NoteRequestGate {
        enum class State { Idle, Queued, Running };
        std::atomic<State> Current{State::Idle};
    public:
        bool Request() {
            auto Expected = State::Idle;
            return Current.compare_exchange_strong(Expected, State::Queued);
        }
        bool Begin() {
            auto Expected = State::Queued;
            return Current.compare_exchange_strong(Expected, State::Running);
        }
        void Reset() { Current.store(State::Idle); }
    };

    class NoteRequestBatch {
        std::vector<int> Notes;
        std::size_t Cursor = 0;
        uint64_t NextAt = 0;
    public:
        static constexpr std::size_t MaxPerBatch = 4;
        static constexpr uint64_t IntervalMs = 100;

        void Start(std::vector<int> Values, uint64_t Now) {
            Values.erase(std::remove_if(Values.begin(), Values.end(), [](int I) { return I < 0; }), Values.end());
            std::sort(Values.begin(), Values.end());
            Values.erase(std::unique(Values.begin(), Values.end()), Values.end());
            Notes = std::move(Values); Cursor = 0; NextAt = Now;
        }
        std::vector<int> NextBatch(uint64_t Now) {
            if (!Pending() || Now < NextAt) return {};
            const auto Count = (std::min)(MaxPerBatch, Notes.size() - Cursor);
            std::vector<int> Batch(Notes.begin() + Cursor, Notes.begin() + Cursor + Count);
            Cursor += Count;
            // Schedule from the actual dispatch time: stalls never accumulate bursts.
            NextAt = Now + IntervalMs;
            return Batch;
        }
        bool Pending() const { return Cursor < Notes.size(); }
        std::size_t Sent() const { return Cursor; }
        std::size_t Total() const { return Notes.size(); }
        void Cancel() { *this = {}; }
    };
}
