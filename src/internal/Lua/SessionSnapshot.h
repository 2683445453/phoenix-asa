#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
namespace PhoenixSession {
    struct Snapshot {
        bool Valid=false, InputReady=false;
        uint64_t Session=0, Updated=0;
        double X=0,Y=0,Z=0;
        float Health=-1,Oxygen=-1,Weight=-1;
    };
    inline std::atomic<uint64_t> RequestedUntil{0};
    inline std::mutex Mutex;
    inline Snapshot Value;
    inline void Publish(const Snapshot& S) { std::scoped_lock Lock(Mutex); Value=S; }
    inline Snapshot Read(uint64_t Now) {
        RequestedUntil.store(Now+3000);
        std::scoped_lock Lock(Mutex);
        Snapshot S=Value;
        if (Now-S.Updated>1000) S.Valid=S.InputReady=false;
        return S;
    }
}
