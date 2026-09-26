#include "../src/internal/Hack/HarvestRules.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>

using namespace g_Hack::Harvest;
using namespace std::chrono_literals;
int main() {
    const Time Start{1s};
    // Reproduce a 30 ms actor update continuously reporting an unsafe route.
    // The old now+350 ms assignment never becomes due; bring-forward does.
    auto Due = Start + 1400ms;
    auto OldDue = Due;
    int Replans = 0, OldReplans = 0;
    for (int Ms = 0; Ms <= 3000; Ms += 30) {
        auto Now = Start + std::chrono::milliseconds(Ms);
        if (Now >= Due) { ++Replans; Due = Now + 1400ms; }
        if (Now >= OldDue) { ++OldReplans; OldDue = Now + 1400ms; }
        Due = BringForward(Due, Now + 350ms);
        OldDue = Now + 350ms;
    }
    assert(Replans >= 8 && OldReplans == 0);
    assert(BringForward(Start - 1s, Start + 200ms) == Start - 1s);
    assert(ReplanDelay(true, true, false, true) == 2800ms);
    assert(ReplanDelay(true, true, true, true) == 250ms);
    assert(ReplanDelay(false, true, false, true) == 850ms);
    assert(ReplanDelay(true, false, false, false) == 1400ms);

    // A two-minute productive harvest survives the former 10/45-second caps.
    Time Gain{};
    for (int Seconds = 0; Seconds <= 120; ++Seconds) {
        auto Now = Start + std::chrono::seconds(Seconds);
        if (Seconds % 3 == 0) Gain = Now;
        assert(!IdleExpired(Now, Start, Gain, 10s));
        assert(!IdleExpired(Now, Start, Gain, 14s));
    }
    assert(!IdleExpired(Gain + 10s, Start, Gain, 10s));
    assert(IdleExpired(Gain + 10001ms, Start, Gain, 10s));
    assert(!IdleExpired(Gain + 14s, Start, Gain, 15s));
    assert(IdleExpired(Gain + 15001ms, Start, Gain, 15s));
    assert(!IdleExpired(Start + 120s, {}, {}, 10s));
    // Travel is also allowed to exceed 30 seconds while making real progress.
    assert(!IdleExpired(Start + 60s, Start, Start + 59s, 30s));
    assert(IdleExpired(Start + 60s, Start, Start + 29s, 30s));

    const auto Pick = [](const std::vector<RouteCost>& Routes) {
        RouteCost Best{false, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        int Chosen = -1;
        for (size_t I = 0; I < Routes.size(); ++I)
            if (PreferRoute(Routes[I], Best)) { Best = Routes[I]; Chosen = static_cast<int>(I); }
        return Chosen;
    };
    assert(Pick({{false, 100, 100}, {true, 450, 250}}) == 1); // Nearby blocked shelf.
    assert(Pick({{true, 1400, 200}, {true, 600, 400}}) == 1); // Shorter actual route.
    assert(Pick({{false, 100, 100}, {false, 200, 200}}) == 0); // No client navigation data.
    assert(Pick({{true, 0, 500}, {true, 300, 300}}) == 0); // Already in harvest range.
    assert(Pick({{true, NAN, 100}, {true, -10, 100}}) == -1);
    assert(Pick({{true, 500, 250}, {true, 500, 200}}) == 1);
    // Switching nodes keeps a pending 2.5-second configured attack interval.
    assert(NextTargetAttack(Start + 2500ms, Start + 400ms) == Start + 2500ms);
    assert(NextTargetAttack(Start, Start + 2s) == Start + 2060ms);
    InventoryProgress Inventory;
    assert(!Inventory.Observe({{1, 100}, {2, 200}})); // Initial inventory is a baseline.
    assert(!Inventory.Observe({{2, 200}, {1, 100}})); // Reordering/stack merging.
    assert(!Inventory.Observe({{1, 99}, {2, 200}}));  // Food consumption.
    assert(Inventory.Observe({{1, 98}, {2, 201}}));  // Food loss doesn't mask metal gain.
    assert(!Inventory.Observe({{1, 98}, {2, 50}}));  // Transfer out isn't progress.
    assert(Inventory.Observe({{1, 98}, {2, 50}, {3, 10}})); // First stack of a new resource.
    // Both immediate turns are safe, but one leads into a second wall/cliff.
    assert(PreferDetour({true, 0.6, false}, {false, 0.9, true}));
    assert(!PreferDetour({false, 0.9, true}, {true, 0.6, false}));
    assert(PreferDetour({true, 0.8, false}, {true, 0.5, true}));
    assert(PreferDetour({true, 0.8, true}, {true, 0.8, false}));
    assert(EmptySearchDelay(0)==250ms&&EmptySearchDelay(1)==500ms&&EmptySearchDelay(9)==2000ms);
    assert(MeleeFeedbackDelay(850)==2400ms&&MeleeFeedbackDelay(10000)==20250ms);
    assert(MeleeIdleLimit(850,false)==10000ms&&MeleeIdleLimit(10000,false)==31000ms);
    assert(MeleeApproachRange(30,0,1)==4.5f); // Unknown reach does not authorize a 30m distant swing.
    assert(std::abs(MeleeApproachRange(30,2,1)-2.65f)<.001f);
    assert(MeleeApproachRange(1000,100,1)==PhoenixInterface::MaxHarvestApproachMeters);
    assert(MeleeApproachRange(NAN,NAN,NAN)==4.5f);
    float Range=30;for(int I=0;I<5;++I)Range=RefineMeleeRange(Range,1);
    assert(Range<4.5f&&Range>=1.5f&&RefineMeleeRange(1.5f,1)==1.5f);
    Range=150;for(int I=0;I<4;++I)Range=RefineBeamRange(Range);
    assert(Range<35&&Range>=10&&RefineBeamRange(10)==10);
    const std::vector<double> Points{0,2,5,9};
    assert(PathSuffix(Points,[](double A,double B){return std::abs(A-B);})==std::vector<double>({9,7,4,0}));
    assert(PathSuffix(std::vector<double>{},[](double A,double B){return std::abs(A-B);}).empty());
    std::cout << "Harvest checks passed: blocked replanning, productive/idle timeouts, route selection, missing navigation fallback, target-switch cooldown, inventory progress and detour lookahead\n";
}
