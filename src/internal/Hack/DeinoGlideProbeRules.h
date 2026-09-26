#pragma once
#include <cstdint>
namespace PhoenixGlideProbe {
struct Window {
    bool Started=false;
    uint64_t Start=0, Next=0;
    void Begin(uint64_t Now) { Started=true; Start=Next=Now; }
    bool Expired(uint64_t Now) const { return Started && Now>=Start && Now-Start>=120000; }
    bool Take(uint64_t Now) {
        if (!Started || Now<Next || Expired(Now)) return false;
        Next=Now+50; // sample at most 20 Hz; never replay missed frames
        return true;
    }
};
}
