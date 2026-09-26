#include "../src/internal/Hack/AutoCraftScheduler.h"
#include <cassert>
#include <limits>
#include <iostream>

int main() {
    using namespace std::chrono;
    using Scheduler = g_Hack::AutoCraftScheduler;
    const Scheduler::Clock::time_point Start{};
    Scheduler State;
    // Bounded batches and no repeated RPC burst within one interval.
    assert(State.Plan(0, Start, 1) == 100);
    assert(State.Plan(0, Start + milliseconds(999), 1) == 0);
    assert(State.Plan(98, Start + seconds(1), 1) == 2);
    // Full and even overfull cached queues continue probing at a low rate.
    assert(State.Plan(100, Start + seconds(2), 1) == 1);
    assert(State.Plan(100, Start + seconds(3), 1) == 0);
    assert(State.Plan(101, Start + seconds(4), 1) == 0);
    assert(State.Plan(101, Start + seconds(5), 1) == 1);
    // Fresh free slots resume normal top-ups immediately at the next interval.
    assert(State.Plan(90, Start + seconds(6), 1) == 10);
    int Total = 0;
    for (int Second = 7; Second < 107; ++Second)
        Total += State.Plan(0, Start + seconds(Second), 1);
    assert(Total == 10000); // no lifetime limit at 100 requests
    State = {};
    assert(State.Plan(-1, Start, 0) == 100);
    assert(State.Plan(0, Start + milliseconds(249), 0) == 0);
    assert(State.Plan(0, Start + milliseconds(250), 0) == 100);
    State = {};
    assert(State.Plan(0, Start, std::numeric_limits<float>::quiet_NaN()) == 100);
    assert(State.Plan(0, Start + milliseconds(500), 1) == 0);
    std::cout << "Auto-craft scheduler: all regression checks passed\n";
}
