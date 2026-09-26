#include "../src/internal/Hack/TekSpearRules.h"
#include "../src/internal/Config/ConfigManager.h"
#include "../src/internal/Config/Configs.h"
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

int main() {
    using namespace g_TekSpear;
    const double Inf = std::numeric_limits<double>::infinity();
    const double Nan = std::numeric_limits<double>::quiet_NaN();
    auto Arrival = [](Vec3 P, Vec3 V, Vec3 A = {}) { return ProjectileArrival(P, V, A, 50, 50, 0.5); };
    assert(std::abs(Arrival({1000, 0, 0}, {-4000, 0, 0}) - 0.2375) < 0.00001);
    assert(!std::isfinite(Arrival({1000, 0, 0}, {4000, 0, 0}))); // Receding.
    assert(!std::isfinite(Arrival({1000, 200, 0}, {-4000, 0, 0}))); // Passes beside us.
    assert(!std::isfinite(Arrival({1000, 0, 200}, {-4000, 0, 0}))); // Passes overhead.
    assert(std::isfinite(Arrival({0, 0, 600}, {0, 0, -2000}))); // Capsule top.
    assert(std::isfinite(Arrival({0, 0, -600}, {0, 0, 2000}))); // Capsule bottom.
    assert(std::isfinite(Arrival({1000, 0, 40}, {-100000, 0, 0}))); // Crosses in < one frame.
    assert(!std::isfinite(Arrival({1000, 0, 0}, {0, 0, 0})));
    assert(!std::isfinite(Arrival({1000, 0, 0}, {-100, 0, 0}))); // Outside horizon.
    assert(std::isfinite(Arrival({1000, 0, 150}, {-4000, 0, 0}, {0, 0, -3000})));
    assert(std::isfinite(Arrival({1000, 0, 0}, Vec3{-4000, 800, 0} - Vec3{0, 800, 0})));
    assert(!std::isfinite(Arrival({Nan, 0, 0}, {-4000, 0, 0})));
    assert(!std::isfinite(Arrival({1000, 0, 0}, {Inf, 0, 0})));
    assert(!std::isfinite(ProjectileArrival({1000, 0, 0}, {-4000, 0, 0}, {}, -1, 50, 0.5)));
    assert(!std::isfinite(ProjectileArrival({1000, 0, 0}, {-4000, 0, 0}, {}, 50, 50, Nan)));
    assert(Facing({1, 0, 0}, {100, 0, 0}, 0.17));
    assert(!Facing({1, 0, 0}, {-100, 0, 0}, 0.17));
    assert(!Facing({1, 0, 0}, {0, 100, 0}, 0.17));
    assert(Setting(Nan, 0.35, 0.1, 1) == 0.35);
    assert(Setting(500, 0.35, 0.1, 1) == 1);
    assert(MaxMeleeRangeMeters == 25);
    assert(Setting(25, 4.5, 1, MaxMeleeRangeMeters) == 25);
    assert(Setting(1000, 4.5, 1, MaxMeleeRangeMeters) == 25);

    assert(TargetLimit(-100) == 1 && TargetLimit(0) == 1 && TargetLimit(6) == 5);
    // 招架目标类型筛选：Boss 仅按「野生且类名特征判定为 Boss」认定，驯养生物永远走生物开关。
    assert(SpearTypeAllowed(true, false, false, true, true, true));    // 野生普通生物
    assert(SpearTypeAllowed(true, true, false, true, true, true));     // 野生 Boss
    assert(!SpearTypeAllowed(true, true, false, true, true, false));   // 关闭 Boss → 野生 Boss 被滤
    assert(SpearTypeAllowed(true, true, true, true, true, false));     // 驯养且带 Boss 标记 → 仍按生物
    assert(!SpearTypeAllowed(true, false, false, true, false, true));  // 关闭生物 → 野生普通生物被滤
    assert(!SpearTypeAllowed(false, false, false, false, true, true)); // 关闭玩家 → 玩家被滤
    assert(SpearTypeAllowed(false, false, false, true, true, true));   // 玩家通过
    // Boss 类名识别：Dragon_ 前缀与已知标记；Dragontopia 的 SnowDragon 不算 Boss。
    assert(IsBossClassName("Dragon_Character_BP_C"));
    assert(IsBossClassName("SpiderL_Character_BP_C"));
    assert(IsBossClassName("Gorilla_Character_BP_C"));
    assert(IsBossClassName("Manticore_Character_BP_C"));
    assert(IsBossClassName("Overseer_Character_BP_C"));
    assert(IsBossClassName("KingTitan_Character_BP_C"));
    assert(IsBossClassName("DodoRex_Character_BP_C"));
    assert(IsBossClassName("IceKaiju_Character_BP_C"));
    assert(IsBossClassName("Moeder_Character_BP_C"));
    assert(!IsBossClassName("SnowDragon_Character_BP_C"));
    assert(!IsBossClassName("Dodo_Character_BP_C"));
    assert(!IsBossClassName("Rex_Character_BP_C"));
    assert(!IsBossClassName(""));
    const std::vector<TargetRank> Ranks{{3, 900}, {2, 400, 1}, {1, 400, 0}, {4, 1600}, {5, 2500}, {6, 3600}};
    for (int Count = 1; Count <= 5; ++Count) {
        auto Sorted = Ranks;
        std::sort(Sorted.begin(), Sorted.end(), CloserTarget);
        Sorted.resize(TargetLimit(Count));
        assert(Sorted.size() == Count && Sorted.front().Key == 1 && Sorted.back().Key == Count);
    }
    assert(CloserTarget({2, 399, 1}, {1, 400, 0})); // Actual distance wins over retained order.

    ReturnView View;
    auto FacingView = View.Face({-10, 170, 0}, {-100, -10, 0});
    assert(View.Active && FacingView.Yaw < -170 && FacingView.Yaw > -180);
    auto Restored = View.Restore(FacingView);
    assert(!View.Active && std::abs(Restored.Pitch + 10) < .001 && std::abs(Restored.Yaw - 170) < .001);
    FacingView = View.Face({350, 359, 0}, {0, 100, 0}); // UE stores wrapped control angles too.
    FacingView.Pitch += 3;
    FacingView.Yaw += 8; // Preserve mouse movement while the automatic turn owns the view.
    FacingView = View.Face(FacingView, {-100, 0, 0});
    Restored = View.Restore(FacingView);
    assert(std::abs(Restored.Pitch + 7) < .001 && std::abs(Restored.Yaw - 7) < .001);
    FacingView = View.Face({0, 20, 0}, {-100, 0, 0});
    FacingView.Yaw = -179; // +1 degree input across the +/-180 boundary.
    Restored = View.Restore(FacingView);
    assert(std::abs(Restored.Yaw - 21) < .001);
    View.Face({0, 0, 0}, {Nan, 0, 0});
    assert(!View.Active);
    View.Face({0, 0, 0}, {});
    assert(!View.Active);
    assert(View.Restore({4, 30, 0}).Yaw == 30); // Repeated cleanup must not move the camera.

    PendingParries Queue;
    Queue.Add(101, 10.2, 100);
    Queue.Add(202, 10.1, 400);
    Queue.Add(303, 10.1, 200);
    Queue.Add(101, 10.3, 100); // Do not postpone an earlier queued hit.
    assert(Queue.Entries.size() == 3);
    assert(Queue.Take() == 303 && Queue.Take() == 202 && Queue.Take() == 101 && Queue.Take() == 0);
    Queue.Add(101, 10, 100);
    Queue.Add(202, 10.2, 200);
    Queue.Add(303, 10.2, 300);
    Queue.Prune(10.026, [](uint64_t Key) { return Key != 202; });
    assert(Queue.Take() == 303 && Queue.Take() == 0); // Expired and removed targets cannot trigger late taps.

    // A replicated or physics-driven projectile can have no movement-component velocity.
    ProjectileMotion Motion;
    assert(!MovingVelocity(Motion.Observe({1000, 0, 0}, 1.0)));
    const auto Observed = Motion.Observe({920, 0, 0}, 1.02);
    assert(std::abs(Observed.X + 4000) < .001);
    const auto SampledSpeed = ProjectileVelocity({}, Observed, {}, {}, {});
    assert(std::isfinite(Arrival({920, 0, 0}, SampledSpeed)));
    assert(ProjectileVelocity({-5000,0,0}, Observed, {-1000,0,0}, {}, {}).X == -5000);
    assert(ProjectileVelocity({}, {}, {}, {-3000,0,0}, {}).X == -3000);
    assert(ProjectileVelocity({}, {}, {Nan,0,0}, {}, {-2000,0,0}).X == -2000);
    assert(!MovingVelocity(ProjectileVelocity({}, {}, {}, {}, {})));
    assert(!MovingVelocity(Motion.Observe({900, 0, 0}, 2.0))); // Do not extrapolate across a stale observation/map change.
    assert(!MovingVelocity(Motion.Observe({Nan, 0, 0}, 2.02)));

    const double RockRadius = ProjectileThreatRadius(10, 5, 0, 300, 0, false);
    assert(RockRadius == 300);
    assert(!std::isfinite(ProjectileArrival({1000,300,0}, {-4000,0,0}, {}, 70, 50, .5)));
    assert(std::isfinite(ProjectileArrival({1000,300,0}, {-4000,0,0}, {}, 35 + RockRadius + 25, 50, .5)));
    const double PoisonRadius = ProjectileThreatRadius(5, 5, 0, 8, 350, true);
    assert(PoisonRadius == 350);
    assert(std::isfinite(ProjectileArrival({1000,200,0}, {-4000,0,0}, {}, 35 + PoisonRadius + 25, 50, .5)));
    assert(ProjectileThreatRadius(5, 5, 0, 8, 350, false) == 10); // No speculative splash on non-explosive objects.
    assert(ProjectileThreatRadius(Nan, 5, Nan, 20, Nan, true) == 20);
    // High arcs were rejected by the previous +/-80 degree camera-facing gate.
    assert(!Facing({1,0,0}, {100,0,650}, std::cos(80 * 3.141592653589793 / 180)));
    assert(std::isfinite(ProjectileArrival({100,0,650}, {-100,0,-600}, {0,0,-980}, 100, 50, 1)));
    assert(ProjectileArrival({10,0,0}, {1000,0,0}, {}, 50, 50, .5) == 0); // Still overlapping the danger volume.
    assert(!std::isfinite(ProjectileArrival({1000,0,0}, {4000,0,0}, {}, 100, 50, .5)));
    assert(ActiveProjectile(false, false, false));
    assert(!ActiveProjectile(true, false, true));
    assert(!ActiveProjectile(false, true, false));
    assert(ActiveProjectile(false, true, true)); // A bouncing rock remains a flight candidate.
    assert(!FriendlyProjectile(true, false, true, true, true, true)); // Enemy instigator beats a local ownership proxy.
    assert(FriendlyProjectile(true, true, false, false, false, false));
    assert(FriendlyProjectile(false, false, true, true, false, false));
    assert(FriendlyProjectile(false, false, false, false, true, false));
    assert(!FriendlyProjectile(false, false, false, false, false, false));

    assert(NotifyTime(0, 0.3, 2, 4, 0.01) == 0.31);
    assert(NotifyTime(1, 0.3, 2, 4, 0) == 2.3);
    assert(NotifyTime(2, 0.25, 2, 4, 0) == 3);
    assert(!std::isfinite(NotifyTime(3, 1, 0, 1, 0)));
    assert(!std::isfinite(NotifyTime(0, Nan, 0, 1, 0)));

    SingleTarget Target;
    assert(Target.Select(101) && Target.Key == 101);
    assert(!Target.Select(101)); // Same nearest unit preserves its attack history.
    assert(Target.Select(202) && Target.Key == 202); // A closer unit replaces a live target.
    assert(Target.Select(0) && Target.Key == 0); // Nothing eligible clears the line and label.
    Target = {}; // Explicit retarget.
    assert(Target.Select(303) && Target.Key == 303);

    // The target 3 m away off-center beats the target 50 m away at the crosshair.
    // A 4.5 m parry reach must not restrict the independent 300 m visual lock.
    const auto Circle = g_Aimbot::AimCircle::FromViewport(1920, 1080, 30);
    TargetSelection Selection{Circle, 30000};
    const Vec3 Camera{-200, 0, 0}, Player{}, Forward{1, 0, 0};
    auto Consider = [&](uint64_t Key, Vec3 Origin, double X, double Y) {
        return Selection.Consider(Key, Origin, Origin, Camera, Player, Forward, X, Y);
    };
    assert(Consider(202, {5000, 0, 0}, 961, 540));
    assert(Consider(101, {300, 0, 0}, 1000, 540));
    assert(!Consider(202, {5000, 0, 0}, 960, 540));
    assert(Selection.Ranking.Target == 101);
    assert(!InTargetRange({5000, 0, 0}, Player, 450));
    assert(!Consider(303, {30100, 0, 0}, 960, 540));
    assert(!Consider(303, {-50, 0, 0}, 960, 540)); // Behind player, ahead of third-person camera.
    assert(!Consider(303, {100, 0, 0}, 1200, 540)); // Outside circle.
    assert(!Consider(303, {100, 0, 0}, Nan, 540));
    Target = {};
    assert(Target.Select(Selection.Ranking.Target));
    Selection = {Circle, 30000, {Target.Key}};
    assert(Consider(202, {250, 0, 0}, 1080, 540)); // Now nearer, though farther from the reticle.
    assert(!Consider(101, {300, 0, 0}, 960, 540));
    assert(Target.Select(Selection.Ranking.Target) && Target.Key == 202);
    Selection = {Circle, 30000, {Target.Key}};
    assert(Consider(101, {250, 0, 0}, 960, 540));
    assert(Consider(202, {250, 0, 0}, 1080, 540)); // Exact distance tie retains previous unit.
    assert(!Target.Select(Selection.Ranking.Target));
    // Creature size/head position must not replace actor-to-player distance.
    assert(!Selection.Consider(303, {400, 0, 0}, {50, 0, 0}, Camera, Player, Forward, 960, 540));

    for (double Dpi : {1.0, 1.25, 1.5, 2.0}) {
        const auto Space = g_Aimbot::AimCanvasSpace::FromViewport(1920, 1080, 1920 / Dpi, 1080 / Dpi);
        const auto Center = Space.ToCanvas(Circle.X, Circle.Y);
        assert(std::abs(Center.X * Dpi - 960) < 0.001 && std::abs(Center.Y * Dpi - 540) < 0.001);
        const double Radius = Circle.Radius / Dpi;
        for (double Width : {100.0, 2000.0}) {
            const auto Layout = TargetLabelLayout(Center.X, Center.Y, Radius, Width, 90);
            assert(Layout.Scale > 0 && Layout.Scale <= 1);
            assert(Layout.Y + 90 * Layout.Scale < Center.Y); // No overlap with magic-bullet label below.
            for (double X : {Layout.X, Layout.X + Width * Layout.Scale})
                for (double Y : {Layout.Y, Layout.Y + 90 * Layout.Scale})
                    assert((X - Center.X) * (X - Center.X) + (Y - Center.Y) * (Y - Center.Y) <= Radius * Radius);
        }
    }
    assert(StateField("Is Blocking") == SpearField::Blocking);
    assert(StateField("IS_Deflecting") == SpearField::Deflecting);
    assert(StateField("pullback") == SpearField::Pullback);
    assert(StateField("is idle ground melee pullback") == SpearField::Pullback);
    assert(StateField("is_midair_stab_charging") == SpearField::Charging);
    assert(StateField("is midair stab releasing") == SpearField::Releasing);
    assert(StateField("IsDoding") == SpearField::Dodging && StateField("IsDodging") == SpearField::Dodging);
    assert(StateField("CurrentState") == SpearField::Unknown && StateField("IsBlockingSomethingElse") == SpearField::Unknown);

    // Regression: the live weapon uses this combined animation output, which
    // is true while idle. It is not a dedicated "currently pulling back" flag.
    SpearState Spear;
    Spear.Set(StateField("is idle/ground melee/pullback"), true);
    assert(Spear.GroundAnimationGroup && Spear.PauseReason(false) == SpearPause::None);
    Spear.Set(StateField("IsBlocking"), true);
    Spear.Set(StateField("IsDeflecting"), true);
    assert(Spear.PauseReason(false) == SpearPause::None); // Our defense cannot disable itself.
    assert(Spear.PauseReason(true) == SpearPause::ManualRight);
    Spear.Set(StateField("is midair stab charging"), true);
    assert(Spear.PauseReason(false) == SpearPause::AirCharging);
    Spear.Set(StateField("is midair stab charging"), false);
    Spear.Set(StateField("is midair stab releasing"), true);
    assert(Spear.PauseReason(false) == SpearPause::AirReleasing);
    Spear.Set(StateField("is midair stab releasing"), false);
    Spear.Set(StateField("IsDoding"), true);
    assert(Spear.PauseReason(false) == SpearPause::Dodging);
    Spear.Set(StateField("IsDoding"), false);
    assert(Spear.PauseReason(false) == SpearPause::None); // Action completion resumes without a reset.
    for (int Mask = 0; Mask < 64; ++Mask) {
        Spear = {};
        Spear.Set(SpearField::Pullback, (Mask & 1) != 0);
        Spear.Set(SpearField::Blocking, (Mask & 2) != 0);
        Spear.Set(SpearField::Deflecting, (Mask & 4) != 0);
        Spear.Set(SpearField::Charging, (Mask & 8) != 0);
        Spear.Set(SpearField::Releasing, (Mask & 16) != 0);
        Spear.Set(SpearField::Dodging, (Mask & 32) != 0);
        assert((Spear.PauseReason(false) != SpearPause::None) == ((Mask & 56) != 0));
        assert(Spear.PauseReason(true) == SpearPause::ManualRight);
    }

    StrikeTracker Tracker;
    StrikeSample Sample;
    Sample.Source = 7;
    assert(!Tracker.Observe(Sample, 0.04));
    Sample.Active = true;
    Sample.Montage = 9;
    Sample.Rate = 1;
    Sample.HitTimes = {0.5, 1.0};
    Sample.Position = 0.1;
    assert(!Tracker.Observe(Sample, 0.04)); // Wind-up is not a hit.
    Sample.Position = 0.47;
    assert(Tracker.Observe(Sample, 0.04));
    for (double P : {0.47, 0.49, 0.5, 0.52, 0.6, 0.8}) {
        Sample.Position = P;
        assert(!Tracker.Observe(Sample, 0.04)); // Never repeat/extend the same hit.
    }
    Sample.Position = 0.97;
    assert(Tracker.Observe(Sample, 0.04)); // Second strike inside the same combo.
    Sample.Position = 1.01;
    assert(!Tracker.Observe(Sample, 0.04));
    Sample.Position = 0.1;
    assert(!Tracker.Observe(Sample, 0.04)); // Replay of the same montage.
    Sample.Rate = 2;
    Sample.Position = 0.43;
    assert(Tracker.Observe(Sample, 0.04)); // Speed changes use real time to impact.
    Sample.Position = 0.41;
    assert(!Tracker.Observe(Sample, 0.04)); // Small replication correction cannot rearm.
    Sample.Position = 0.96;
    Sample.Rate = 0;
    assert(!Tracker.Observe(Sample, 0.04)); // Paused animation.
    Sample.Rate = 1;
    Sample.Position = 1.5;
    assert(!Tracker.Observe(Sample, 0.04)); // Late packet/frame jump skips expired hit.

    Tracker = {};
    Sample.Position = 0.8;
    assert(!Tracker.Observe(Sample, 0.04)); // Acquisition halfway through a swing.
    Sample.Position = 0.97;
    assert(Tracker.Observe(Sample, 0.04));
    Sample.Source = 8;
    Sample.Position = 1.1;
    assert(!Tracker.Observe(Sample, 0.04)); // Weapon/object identity change drops old hit.

    Tracker = {};
    Sample = {};
    Sample.Source = 10;
    assert(!Tracker.Observe(Sample, 0.04));
    Sample.Active = true;
    Sample.DamageActive = true;
    Sample.DamageStamp = 100;
    assert(Tracker.Observe(Sample, 0.04)); // Native damage-window onset.
    for (int I = 0; I < 100; ++I) assert(!Tracker.Observe(Sample, 0.04));
    Sample.DamageStamp = 101;
    assert(Tracker.Observe(Sample, 0.04)); // Back-to-back native windows without an idle frame.
    Sample.DamageActive = false;
    assert(!Tracker.Observe(Sample, 0.04));
    Sample.DamageActive = true;
    assert(Tracker.Observe(Sample, 0.04));
    Tracker = {};
    assert(!Tracker.Observe(Sample, 0.04)); // Resume while a hit is already active.

    for (double Lead : {0.20, 0.25, 0.30}) {
        Tracker = {};
        Sample = {};
        Sample.Source = 10;
        Sample.Montage = 20;
        Sample.Active = true;
        Sample.Rate = 1;
        Sample.HitTimes = {1.0};
        Sample.Position = 1.0 - Lead - 0.001;
        assert(!Tracker.Observe(Sample, Lead));
        Sample.Position += 0.002;
        assert(Tracker.Observe(Sample, Lead)); // Runtime accepts values above the old 0.15 cap.
        assert(!Tracker.Observe(Sample, Lead));
    }
    Tracker = {};
    Sample.Position = 0.65;
    assert(!Tracker.Observe(Sample, 10)); // Invalid configuration still clamps at 0.3.
    Sample.Position = 0.701;
    assert(Tracker.Observe(Sample, 10));
    StrikeHistory History;
    Sample.Position = 0.701;
    History.BeginFrame();
    assert(History.Observe(101, Sample, .3));
    assert(History.Observe(202, Sample, .3));
    History.Deactivate();
    History.BeginFrame();
    assert(!History.Observe(101, Sample, .3)); // Switching back does not replay the same hit.
    Sample.Position = 0.1;
    History.BeginFrame();
    assert(!History.Observe(101, Sample, .3));
    Sample.Position = 0.701;
    History.BeginFrame();
    assert(History.Observe(101, Sample, .3)); // Next animation is eligible normally.
    History = {};
    Sample = {};
    Sample.Source = 10;
    History.BeginFrame();
    assert(!History.Observe(101, Sample, .3));
    History.BeginFrame();
    assert(!History.Observe(202, Sample, .3));
    Sample.DamageActive = true;
    Sample.DamageStamp = 100;
    History.BeginFrame();
    assert(!History.Observe(101, Sample, .3)); // Rebase a window already active on reacquisition.
    Sample.DamageStamp = 101;
    History.BeginFrame();
    assert(History.Observe(101, Sample, .3));

    // Five targets retain separate damage-window edges in the same frame.
    History = {};
    Sample = {};
    Sample.Source = 10;
    History.BeginFrame();
    for (uint64_t Key = 1; Key <= 5; ++Key) assert(!History.Observe(Key, Sample, .3));
    History.BeginFrame();
    Sample.DamageActive = true;
    Sample.DamageStamp = 100;
    for (uint64_t Key = 5; Key >= 1; --Key) assert(History.Observe(Key, Sample, .3));
    History.BeginFrame();
    for (uint64_t Key = 1; Key <= 5; ++Key) assert(!History.Observe(Key, Sample, .3));

    using Event = std::pair<Key, bool>;
    std::vector<Event> Events;
    auto Send = [&](Key Which, bool Down) { Events.emplace_back(Which, Down); return true; };
    InputState Inputs;
    assert(Inputs.Update(Defense::Block, 0, false, false, Send, true));
    assert((Events == std::vector<Event>{{Key::RightMouse, true}}));
    // A sustained attack flag must produce just one bounded tap, not a hold.
    for (int I = 1; I < 1000; ++I) Inputs.Update(Defense::Block, I * 0.02, false, false, Send);
    assert((Events == std::vector<Event>{{Key::RightMouse, true}, {Key::RightMouse, false}}));
    assert(!Inputs.OwnRight && Inputs.Mode == Defense::None);
    Events.clear();
    Inputs.Update(Defense::Block, 20, false, false, Send, true);
    Inputs.Update(Defense::Block, 20.04, false, false, Send);
    Inputs.Update(Defense::None, 20.08, false, false, Send);
    Inputs.Update(Defense::Block, 20.5, false, false, Send, true);
    Inputs.Update(Defense::None, 20.59, false, false, Send);
    assert((Events == std::vector<Event>{{Key::RightMouse, true}, {Key::RightMouse, false},
        {Key::RightMouse, true}, {Key::RightMouse, false}}));
    // A distinct strike requires a fresh press even if it arrives during a tap.
    Inputs.Update(Defense::Block, 20.7, false, false, Send, true);
    Events.clear();
    Inputs.Update(Defense::Block, 20.74, false, false, Send, true);
    assert((Events == std::vector<Event>{{Key::RightMouse, false}}));

    Inputs.Update(Defense::Block, 20.76, false, false, Send);
    assert(Events.back() == Event(Key::RightMouse, true));
    Inputs.Update(Defense::None, 20.9, false, false, Send);
    Inputs.Update(Defense::Block, 20.99, false, false, Send, true);
    Events.clear();
    Inputs.Update(Defense::Deflect, 21, false, false, Send);
    assert(Events.empty() && Inputs.OwnRight);
    Inputs.Update(Defense::Deflect, 21.02, false, false, Send);
    assert((Events == std::vector<Event>{{Key::Shift, true}}));
    Events.clear();
    Inputs.Update(Defense::Block, 22, false, false, Send, true);
    assert((Events == std::vector<Event>{{Key::RightMouse, false}, {Key::Shift, false}}));
    Inputs.Update(Defense::Block, 22.02, false, false, Send);
    assert(Events.back() == Event(Key::RightMouse, true));
    Inputs.Update(Defense::None, 23, false, false, Send);
    assert(!Inputs.OwnRight && !Inputs.OwnShift && Inputs.Mode == Defense::None);

    // Disabling, losing focus, changing weapon/map, death and unload all use Release.
    Inputs.Update(Defense::Deflect, 24, false, false, Send);
    Inputs.Update(Defense::Deflect, 24.02, false, false, Send);
    assert(!Inputs.OwnShift && Inputs.OwnRight);
    Inputs.Update(Defense::Deflect, 24.04, false, false, Send);
    assert(Inputs.OwnShift);
    Events.clear();
    assert(Inputs.Release(false, false, Send));
    assert((Events == std::vector<Event>{{Key::RightMouse, false}, {Key::Shift, false}}));
    Events.clear();
    assert(Inputs.Release(false, false, Send) && Events.empty());
    Inputs.Update(Defense::Block, 25, false, true, Send, true);
    Inputs.Update(Defense::Block, 25.02, false, true, Send);
    assert(Events.empty()); // Never release a real Shift to force a melee block.
    Inputs.Update(Defense::Deflect, 26, false, true, Send);
    Inputs.Update(Defense::Deflect, 26.02, false, true, Send);
    assert((Events == std::vector<Event>{{Key::RightMouse, true}}));
    Events.clear();
    Inputs.Release(true, true, Send); // A physical right press takes over.
    assert(Events.empty() && !Inputs.OwnRight && !Inputs.OwnShift);
    Inputs.Update(Defense::Deflect, 27, true, false, Send);
    assert(Events.empty());

    // Failure after right-down retains ownership until cleanup succeeds.
    Inputs.Update(Defense::Deflect, 28, false, false, Send);
    Inputs.Update(Defense::Deflect, 28.02, false, false, Send);
    auto Fail = [](Key, bool) { return false; };
    assert(!Inputs.Update(Defense::Deflect, 28.04, false, false, Fail));
    assert(!Inputs.OwnShift && Inputs.OwnRight);
    assert(!Inputs.Release(false, false, Fail) && Inputs.OwnRight);
    Events.clear();
    assert(Inputs.Release(false, false, Send));
    assert((Events == std::vector<Event>{{Key::RightMouse, false}}));

    Inputs = {};
    Events.clear();
    Inputs.Update(Defense::Block, 29, false, false, Send, true);
    assert(Inputs.ReleaseForTargetChange(29.01, false, false, Send));
    Inputs.Update(Defense::Block, 29.01, false, false, Send, true);
    assert(Events.size() == 2 && !Inputs.OwnRight); // Distinct targets require a fresh input edge.
    Inputs.Update(Defense::Block, 29.03, false, false, Send);
    assert(Events.size() == 3 && Inputs.OwnRight);
    Inputs.Update(Defense::None, 29.12, false, false, Send);
    assert(Events.size() == 4 && !Inputs.OwnRight);

    // The requested extended tap stays down for 0.5 s; oversized values clamp.
    for (double Tap : {0.5, 9.0}) {
        Inputs={};Events.clear();
        Inputs.Update(Defense::Block,30,false,false,Send,true,Tap);
        Inputs.Update(Defense::Block,30.49,false,false,Send,false,Tap);
        assert(Inputs.OwnRight && Events.size()==1);
        Inputs.Update(Defense::Block,30.51,false,false,Send,false,Tap);
        assert(!Inputs.OwnRight && Events.size()==2);
    }
    // Exercise the runtime's arbitration through a full two-hit animation.
    Tracker = {};
    Inputs = {};
    Sample = {};
    Sample.Source = 70;
    Sample.Montage = 90;
    Sample.Active = true;
    Sample.Rate = 1;
    Sample.HitTimes = {0.5, 1.0};
    std::vector<std::pair<double, bool>> TimedInputs;
    Spear = {};
    Spear.Set(StateField("is idle/ground melee/pullback"), true);
    for (int Frame = 0; Frame <= 150; ++Frame) {
        const double Now = Frame / 100.0;
        Sample.Position = Now;
        const bool Due = Tracker.Observe(Sample, 0.04);
        const Defense Desired = Due || Inputs.Mode == Defense::Block ? Defense::Block : Defense::None;
        auto Record = [&](Key Which, bool Down) {
            assert(Which == Key::RightMouse);
            TimedInputs.emplace_back(Now, Down);
            return true;
        };
        Spear.Blocking = Inputs.OwnRight;
        if (Spear.PauseReason(false) == SpearPause::None)
            assert(Inputs.Update(Desired, Now, false, false, Record, Due, 0.08));
        else assert(Inputs.Release(false, false, Record));
    }
    assert(TimedInputs.size() == 4);
    assert(TimedInputs[0].second && !TimedInputs[1].second && TimedInputs[2].second && !TimedInputs[3].second);
    assert(TimedInputs[0].first >= 0.46 && TimedInputs[0].first <= 0.48);
    assert(TimedInputs[2].first >= 0.96 && TimedInputs[2].first <= 0.98);
    assert(TimedInputs[1].first - TimedInputs[0].first <= 0.091);
    assert(TimedInputs[3].first - TimedInputs[2].first <= 0.091);
    assert(!Inputs.OwnRight);

    // Exercise the combined scheduler: five attackers, bounded input pulses,
    // and one independent return-to-view operation per completed defense.
    History = {};
    Queue = {};
    Inputs = {};
    View = {};
    ViewRotation CameraView{0, 30, 0};
    uint64_t CurrentAttacker = 0;
    std::vector<uint64_t> Defended;
    int Restores = 0;
    for (int Frame = 0; Frame <= 120; ++Frame) {
        const double Now = Frame * .01;
        History.BeginFrame();
        for (uint64_t Key = 1; Key <= 5; ++Key) {
            StrikeSample S;
            S.Source = Key;
            S.Montage = Key + 10;
            S.Active = true;
            S.Position = Now;
            S.Rate = 1;
            S.HitTimes = {.3 + (Key - 1) * .15};
            if (History.Observe(Key, S, .04)) Queue.Add(Key, Now + History.DueIn, Key * 100.0);
        }
        Queue.Prune(Now, [](uint64_t) { return true; });
        bool NewStrike = false;
        Defense Desired = Defense::None;
        if (Inputs.Mode == Defense::Block) Desired = Defense::Block;
        else if (Now >= Inputs.ReadyAt && (CurrentAttacker = Queue.Take())) { Desired = Defense::Block; NewStrike = true; }
        if (Desired == Defense::Block) CameraView = View.Face(CameraView, {100, static_cast<double>(CurrentAttacker * 100), 0});
        assert(Inputs.Update(Desired, Now, false, false, [&](Key Which, bool Down) {
            if (Which == Key::RightMouse && Down) Defended.push_back(CurrentAttacker);
            return true;
        }, NewStrike, .08));
        if (Inputs.Mode != Defense::Block && View.Active) {
            CameraView = View.Restore(CameraView);
            ++Restores;
            assert(std::abs(CameraView.Yaw - 30) < .001);
        }
    }
    assert((Defended == std::vector<uint64_t>{1, 2, 3, 4, 5}));
    assert(Restores == 5 && !Inputs.OwnRight && !View.Active && Queue.Entries.empty());

    assert(!g_Config::bTekSpearAutoBlock && !g_Config::bTekSpearAutoDeflect);
    assert(g_Config::TekSpearTargetRangeMeters == 300);
    assert(g_Config::TekSpearTargetCount == 1 && g_Config::bTekSpearAutoFace);
    assert(g_Config::bDrawTekSpearCircle && g_Config::bDrawTekSpearTargetLine && g_Config::bDrawTekSpearTargetInfo);
    auto& Manager = ConfigManager::Get();
    const auto Directory = fs::absolute("release/tests/tek-spear-config");
    Manager.Initialize(Directory.string());
    for (int Count = 1; Count <= 5; ++Count) for (bool Block : {false, true}) for (bool Deflect : {false, true}) {
        g_Config::bTekSpearAutoBlock = Block;
        g_Config::bTekSpearAutoDeflect = Deflect;
        g_Config::TekSpearTargetRangeMeters = 400;
        g_Config::TekSpearTargetCount = Count;
        g_Config::bTekSpearAutoFace = Block;
        g_Config::bDrawTekSpearCircle = Block;
        g_Config::bDrawTekSpearTargetLine = Deflect;
        g_Config::bDrawTekSpearTargetInfo = !Block;
        g_Config::TekSpearTargetColor[0] = 0.25f;
        g_Config::TekSpearTargetColor[3] = 0.75f;
        g_Config::bAimbotEnabled = false;
        g_Config::bDrawAimCircle = !Block;
        g_Config::bDrawAimTargetLine = !Deflect;
        g_Config::bDrawAimTargetInfo = Block;
        g_Config::TekSpearMeleeRangeMeters = 25;
        g_Config::TekSpearParryLeadSeconds = 0.30f;
        g_Config::TekSpearParryTapSeconds = 0.09f;
        g_Config::TekSpearProjectileRangeMeters = 80;
        g_Config::TekSpearLookAheadSeconds = 0.5;
        assert(Manager.SaveConfig("defense.ini"));
        g_Config::bTekSpearAutoBlock = !Block;
        g_Config::bTekSpearAutoDeflect = !Deflect;
        g_Config::TekSpearTargetRangeMeters = 5;
        g_Config::TekSpearTargetCount = 99;
        g_Config::bTekSpearAutoFace = !Block;
        g_Config::bDrawTekSpearCircle = !Block;
        g_Config::bDrawTekSpearTargetLine = !Deflect;
        g_Config::bDrawTekSpearTargetInfo = Block;
        g_Config::TekSpearTargetColor[0] = g_Config::TekSpearTargetColor[3] = 0;
        g_Config::TekSpearMeleeRangeMeters = 1;
        g_Config::TekSpearParryLeadSeconds = 0;
        g_Config::TekSpearParryTapSeconds = 0.03f;
        g_Config::TekSpearProjectileRangeMeters = 5;
        g_Config::TekSpearLookAheadSeconds = 0.1;
        assert(Manager.LoadConfig("defense.ini"));
        assert(g_Config::bTekSpearAutoBlock == Block && g_Config::bTekSpearAutoDeflect == Deflect);
        assert(g_Config::TekSpearTargetRangeMeters == 400);
        assert(g_Config::TekSpearTargetCount == Count && g_Config::bTekSpearAutoFace == Block);
        assert(g_Config::bDrawTekSpearCircle == Block && g_Config::bDrawTekSpearTargetLine == Deflect && g_Config::bDrawTekSpearTargetInfo == !Block);
        assert(g_Config::TekSpearTargetColor[0] == 0.25f && g_Config::TekSpearTargetColor[3] == 0.75f);
        assert(!g_Config::bAimbotEnabled && g_Config::bDrawAimCircle == !Block && g_Config::bDrawAimTargetLine == !Deflect && g_Config::bDrawAimTargetInfo == Block);
        assert(g_Config::TekSpearMeleeRangeMeters == 25 && g_Config::TekSpearProjectileRangeMeters == 80);
        assert(g_Config::TekSpearParryLeadSeconds == 0.30f && g_Config::TekSpearParryTapSeconds == 0.09f);
        assert(g_Config::TekSpearLookAheadSeconds == 0.5);
    }
    std::cout << "Tek spear 1-5 targets, independent strike histories, ordered parries, turn/restore with mouse input, projectiles, bounded taps and configuration checks passed\n";
}
