#pragma once
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
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

    class NoteRequestBatch {
        std::vector<int> Notes;
        std::size_t Cursor = 0;
        uint64_t NextAt = 0;
    public:
        void Start(std::vector<int> Values, uint64_t Now) {
            Values.erase(std::remove_if(Values.begin(), Values.end(), [](int I) { return I < 0; }), Values.end());
            std::sort(Values.begin(), Values.end());
            Values.erase(std::unique(Values.begin(), Values.end()), Values.end());
            Notes = std::move(Values); Cursor = 0; NextAt = Now;
        }
        std::optional<int> Next(uint64_t Now) {
            if (!Pending() || Now < NextAt) return std::nullopt;
            NextAt = Now + 250;
            return Notes[Cursor++];
        }
        bool Pending() const { return Cursor < Notes.size(); }
        std::size_t Sent() const { return Cursor; }
        std::size_t Total() const { return Notes.size(); }
        void Cancel() { *this = {}; }
    };
}
