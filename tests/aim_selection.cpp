#include "../src/internal/Legit/Aimbot/AimSelection.h"
#include "../src/internal/Legit/Aimbot/AimViewArguments.h"
#include "../src/internal/Legit/Aimbot/AimTargetRules.h"
#include "../src/internal/Legit/Aimbot/AimHitValidation.h"
#include "../src/internal/Legit/Aimbot/AimCollisionGeometry.h"
#include "../src/internal/Config/ConfigManager.h"
#include "../src/internal/Config/Configs.h"
#include <cassert>
#include <array>
#include <iostream>
#include <limits>

int main() {
    // Projection may yield a screen coordinate for a rear point. Positive depth
    // is required both at the camera and in front of the third-person player.
    using g_Aimbot::InFrontOfAimView;
    const SDK::FVector Camera{-300, 0, 0}, Player{0, 0, 0}, Forward{1, 0, 0};
    assert(InFrontOfAimView(SDK::FVector{500, 0, 0}, Camera, Player, Forward));
    assert(!InFrontOfAimView(SDK::FVector{-500, 0, 0}, Camera, Player, Forward));
    assert(!InFrontOfAimView(SDK::FVector{-100, 0, 0}, Camera, Player, Forward));
    assert(!InFrontOfAimView(SDK::FVector{0, 100, 0}, Camera, Player, Forward));
    assert(InFrontOfAimView(SDK::FVector{-500, 0, 0}, SDK::FVector{300, 0, 0}, Player, SDK::FVector{-1, 0, 0}));
    assert(!InFrontOfAimView(SDK::FVector{INFINITY, 0, 0}, Camera, Player, Forward));
    // Acquisition and shot-time selection share the same preparation order,
    // but only shot-time selection requires collision confirmation.
    // A stale point outside the circle must not prevent it moving into selection;
    // an old valid hit must not bypass preparation on the next shot either.
    using g_Aimbot::AimTraceState;
    using g_Aimbot::SelectPreparedAimPoint;
    for (int Shot = 0; Shot < 3; ++Shot) {
        int Checks = 0, Refreshes = 0;
        double OldPosition = Shot == 0 ? 100 : 10;
        const double CurrentPosition = 20 + Shot;
        const auto Selected = SelectPreparedAimPoint([&] {
            ++Refreshes;
            OldPosition = CurrentPosition;
            return true;
        }, [&] { return std::vector<double>{OldPosition, 40}; },
            [](double X) -> double { return X < 50 ? X * X : INFINITY; },
            [&](double X) { ++Checks; return X == CurrentPosition ? AimTraceState::Hit : AimTraceState::Unavailable; });
        assert(Selected && Selected.Value == CurrentPosition);
        assert(Checks == 1 && Refreshes == 1);
    }
    int Reads = 0;
    assert(!SelectPreparedAimPoint([] { return false; }, [&] { ++Reads; return std::vector<int>{1}; },
        [](int P) { return P; }, [](int) { return AimTraceState::Hit; }));
    assert(Reads == 0); // Missing refresh API cannot publish a stale point.
    // A visible target must not disappear from selection when native collision
    // preparation is unavailable. It still cannot authorize a shot above.
    using g_Aimbot::SelectScreenAimPoint;
    const auto ScreenOnly = SelectScreenAimPoint([] { return false; },
        [&] { ++Reads; return std::vector<int>{3, 1, 2}; }, [](int X) { return X * X; });
    assert(ScreenOnly && ScreenOnly.Value == 1 && Reads == 1);

    struct SceneTarget {
        uint64_t Key;
        std::vector<g_Aimbot::AimScreenPoint> Points;
        AimTraceState Path;
        bool Enabled = true;
    };
    const auto SelectionCircle = g_Aimbot::AimCircle::FromViewport(1920, 1080, 30);
    std::vector<SceneTarget> Scene{
        {101, {{980, 540}, {1000, 540}}, AimTraceState::Hit},
        {202, {{1040, 540}}, AimTraceState::Hit},
        {303, {{960, 710}}, AimTraceState::Hit}, // Outside the circle.
        {404, {{961, 540}}, AimTraceState::Hit, false} // Excluded type/part.
    };
    uint64_t PreviousTarget = 0;
    const auto SelectScene = [&] {
        g_Aimbot::AimScreenRanking Ranking{PreviousTarget};
        for (auto& Target : Scene) {
            if (!Target.Enabled) continue;
            bool Refreshed = false;
            const auto Point = SelectScreenAimPoint([&] { Refreshed = true; return true; },
                [&] { assert(Refreshed); return Target.Points; },
                [&](const auto& P) { return SelectionCircle.Score(P.X, P.Y); });
            if (Point) Ranking.Consider(Target.Key, Point.Score);
        }
        PreviousTarget = Ranking.Target;
        return Ranking.Target;
    };
    for (const auto Path : {AimTraceState::Hit, AimTraceState::Unavailable, AimTraceState::Blocked, AimTraceState::Hit}) {
        auto& Nearest = *std::find_if(Scene.begin(), Scene.end(), [](const auto& T) { return T.Key == 101; });
        Nearest.Path = Path;
        assert(SelectScene() == 101); // A transient camera/weapon hit failure cannot select 202.
        const auto Shot = SelectPreparedAimPoint([] { return true; }, [&] { return Nearest.Points; },
            [&](const auto& P) { return SelectionCircle.Score(P.X, P.Y); },
            [&](const auto&) { return Nearest.Path; });
        assert(static_cast<bool>(Shot) == (Path == AimTraceState::Hit));
        assert(SelectScene() == 101); // Firing never changes actor identity.
        std::reverse(Scene.begin(), Scene.end()); // Level/actor iteration order is immaterial.
        assert(SelectScene() == 101);
    }
    // No hidden world-distance, previous-target bonus, or switching delay.
    Scene[1].Points = {{965, 540}};
    assert(SelectScene() == 202); // A newly closer target wins immediately.
    Scene[1].Points = {{980, 540}};
    assert(SelectScene() == 202); // Exact tie retains the previous target.
    std::reverse(Scene.begin(), Scene.end());
    assert(SelectScene() == 202);
    PreviousTarget = 0;
    assert(SelectScene() == 101); // Without a previous target, stable object key breaks ties.
    std::reverse(Scene.begin(), Scene.end());
    PreviousTarget = 0;
    assert(SelectScene() == 101);
    Scene[0].Points = {{960, 800}}; // 101 moved entirely outside the circle.
    assert(SelectScene() == 202);
    Scene[1].Enabled = false; // Death or changed type/part filters invalidate it immediately.
    assert(SelectScene() == 0);
    g_Aimbot::AimScreenRanking InvalidRanking;
    assert(!InvalidRanking.Consider(0, 0));
    assert(!InvalidRanking.Consider(1, INFINITY));
    assert(!InvalidRanking.Consider(1, std::numeric_limits<double>::quiet_NaN()));
    assert(!InvalidRanking.Consider(1, -1));
    std::vector<int> TraceOrder;
    const auto Visible = SelectPreparedAimPoint([] { return true; }, [] { return std::vector<int>{3, 1, 4, 2}; },
        [](int P) { return P; }, [&](int P) {
            TraceOrder.push_back(P);
            return P == 1 ? AimTraceState::Blocked : P == 2 ? AimTraceState::Unavailable : AimTraceState::Hit;
        });
    assert(Visible && Visible.Value == 3 && (TraceOrder == std::vector<int>{1, 2, 3}));
    for (const auto Failed : {AimTraceState::Blocked, AimTraceState::Unavailable})
        assert(!SelectPreparedAimPoint([] { return true; }, [] { return std::vector<int>{1, 2}; },
            [](int P) { return P; }, [Failed](int) { return Failed; }));

    // Actual SDK collision shapes: a flat asymmetric convex hull has an AABB
    // center outside its volume. Every sampled point must be inside its tetrahedron
    // after applying the shape's local transform, not at the actor capsule pivot.
    SDK::FVector Vertices[]{{0, 0, 0}, {12, 0, 0}, {0, 4, 0}, {0, 0, 2}};
    SDK::FKConvexElem Hull{};
    Hull.VertexData = {Vertices, 4, 4};
    Hull.Transform.Translation = {100, 200, 300};
    Hull.Transform.Scale3D = {2, 3, 4};
    SDK::FKAggregateGeom Geometry{};
    Geometry.ConvexElems = {&Hull, 1, 1};
    const auto Rotate = [](const SDK::FVector& V, const SDK::FRotator&) { return SDK::FVector{V.Z, V.Y, -V.X}; };
    const auto Transform = [](const SDK::FTransform& T, const SDK::FVector& V) {
        return SDK::FVector{-V.Y * T.Scale3D.Y, V.X * T.Scale3D.X, V.Z * T.Scale3D.Z} + T.Translation;
    };
    auto Samples = g_Aimbot::AimGeometryPoints(Geometry, Rotate, Transform);
    assert(Samples.size() == 7);
    for (const auto& P : Samples) {
        const double X = (P.Y - 200) / 2, Y = (100 - P.X) / 3, Z = (P.Z - 300) / 4;
        assert(X > 0 && Y > 0 && Z > 0 && X / 12 + Y / 4 + Z / 2 < 1);
    }
    Vertices[0].X = std::numeric_limits<double>::quiet_NaN();
    assert(g_Aimbot::AimGeometryPoints(Geometry, Rotate, Transform).empty());
    Geometry = {};
    SDK::FKTaperedCapsuleElem Taper{};
    Taper.Radius0 = 10; Taper.Radius1 = 20; Taper.Length = 400;
    Taper.Center = {1000, 20, 30};
    Geometry.TaperedCapsuleElems = {&Taper, 1, 1};
    Samples = g_Aimbot::AimGeometryPoints(Geometry, Rotate, Transform);
    assert(Samples.size() == 3);
    assert(Samples[0].X == 1000 && Samples[1].X == 1100 && Samples[2].X == 900);
    // A long body's center can lie outside the circle while an interior segment is inside.
    const auto SmallCircle = g_Aimbot::AimCircle::FromViewport(1920, 1080, 10);
    assert(!std::isfinite(SmallCircle.Score(Samples[1].X, 540)));
    assert(std::isfinite(SmallCircle.Score(Samples[0].X, 540)));
    assert(SmallCircle.OverlapsBounds(1000, 520, 1400, 560));
    assert(!SmallCircle.OverlapsBounds(1100, 520, 1400, 560));
    assert(SmallCircle.OverlapsBounds(-50, -50, 2000, 1200)); // Bounds containing the viewport.
    assert(SmallCircle.OverlapsBounds(INFINITY, 0, 2, 2)); // Uncertain broad phase.
    SDK::FKSphereElem Spheres[4]{};
    for (int I = 0; I < 4; ++I) { Spheres[I].Center = {double(I), 0, 0}; Spheres[I].Radius = I + 1; }
    Geometry.SphereElems = {Spheres, 4, 4};
    Samples = g_Aimbot::AimGeometryPoints(Geometry, Rotate, Transform);
    assert(Samples.size() == 7 && Samples[0].X == 0 && Samples[3].X == 3); // Retain all shapes, not only the largest.
    Geometry = {};
    Geometry.SphereElems = {nullptr, 1, 1};
    assert(g_Aimbot::AimGeometryPoints(Geometry, Rotate, Transform).empty());

    // 2026-09-10 shotgun crash: the no-hit FHitResult had both weak refs
    // equal to (0,0), and SDK Get() returned registry slot zero. It must never
    // reach object/type queries, even if a malformed result says it blocked.
    struct HitRef { int ObjectIndex = 0, ObjectSerialNumber = 0; };
    struct HitObject { bool IsComponent; } Component{true}, Unrelated{false};
    int Resolutions = 0, TypeQueries = 0;
    const auto Resolve = [&](const HitRef& Ref) -> HitObject* {
        ++Resolutions;
        if (Ref.ObjectIndex == 0) return &Unrelated;
        if (Ref.ObjectIndex == 10 && Ref.ObjectSerialNumber == 7) return &Component;
        return nullptr; // Dead object or recycled slot with a different serial.
    };
    const auto IsComponent = [&](HitObject* Object) { ++TypeQueries; return Object->IsComponent; };
    using g_Aimbot::ResolveAimHitReference;
    assert(!ResolveAimHitReference(false, HitRef{}, Resolve, IsComponent));
    assert(!ResolveAimHitReference(true, HitRef{}, Resolve, IsComponent));
    assert(!ResolveAimHitReference(true, HitRef{-1, 7}, Resolve, IsComponent));
    assert(!ResolveAimHitReference(false, HitRef{10, 7}, Resolve, IsComponent));
    assert(Resolutions == 0 && TypeQueries == 0);
    assert(!ResolveAimHitReference(true, HitRef{10, 8}, Resolve, IsComponent));
    assert(TypeQueries == 0);
    assert(!ResolveAimHitReference(true, HitRef{0, 7}, Resolve, IsComponent)); // Wrong type rejected.
    assert(ResolveAimHitReference(true, HitRef{10, 7}, Resolve, IsComponent) == &Component);
    int TargetActor = 0, OtherActor = 0, Mesh = 0, Capsule = 0, OtherMesh = 0;
    using g_Aimbot::AimHitMatchesTarget;
    assert(AimHitMatchesTarget(&TargetActor, &OtherMesh, &TargetActor, &Mesh, &Capsule));
    assert(AimHitMatchesTarget(nullptr, &Mesh, &TargetActor, &Mesh, &Capsule));
    assert(AimHitMatchesTarget(nullptr, &Capsule, &TargetActor, &Mesh, &Capsule));
    assert(!AimHitMatchesTarget(&OtherActor, &OtherMesh, &TargetActor, &Mesh, &Capsule));
    assert(!AimHitMatchesTarget(&TargetActor, nullptr, &TargetActor, &Mesh, &Capsule));
    assert(!AimHitMatchesTarget(nullptr, nullptr, nullptr, nullptr, nullptr));

    assert(g_Aimbot::IsShotgunAssetName("WeapShotgun_C"));
    assert(g_Aimbot::IsShotgunAssetName("WeapMachinedShotgun_C"));
    assert(!g_Aimbot::IsShotgunAssetName("WeapRifle_C"));
    assert(!g_Aimbot::IsShotgunAssetName("WeapTekPistol_C"));
    std::array<uint8_t, 48> SpreadWrapper{};
    constexpr uint8_t FloatReturn[] = {0xf3,0x0f,0x11,0x03,0x48,0x83,0xc4,0x20,0x5b,0xc3};
    SpreadWrapper[0x1e] = 0xe8;
    std::memcpy(SpreadWrapper.data() + 0x23, FloatReturn, sizeof(FloatReturn));
    int32_t Relative = 0x103b04d;
    std::memcpy(SpreadWrapper.data() + 0x1f, &Relative, sizeof(Relative));
    assert(g_Aimbot::ResolveSpreadGetterTarget(SpreadWrapper, 0x6050c70) == 0x708bce0);
    Relative = -500;
    std::memcpy(SpreadWrapper.data() + 0x1f, &Relative, sizeof(Relative));
    assert(g_Aimbot::ResolveSpreadGetterTarget(SpreadWrapper, 0x1000) == 0x1000 + 0x23 - 500);
    assert(!g_Aimbot::ResolveSpreadGetterTarget(std::span(SpreadWrapper).first(0x2c), 0x1000));
    SpreadWrapper[0x23] = 0; // Different return ABI must not install the hook.
    assert(!g_Aimbot::ResolveSpreadGetterTarget(SpreadWrapper, 0x1000));

    using g_Aimbot::AimBodyPart;
    using g_Aimbot::AimPartEnabled;
    using g_Aimbot::AcceptAimHit;
    const auto Head = AimBodyPart::Head, Body = AimBodyPart::Body, None = AimBodyPart::None;
    for (const bool Heads : {false, true}) for (const bool Bodies : {false, true}) {
        assert(AimPartEnabled(Head, Heads, Bodies) == Heads);
        assert(AimPartEnabled(Body, Heads, Bodies) == Bodies);
        assert(!AimPartEnabled(None, Heads, Bodies));
        assert(AcceptAimHit(true, true, Head, Head, true, Heads, Bodies) == Heads);
        assert(AcceptAimHit(true, true, Body, Body, true, Heads, Bodies) == Bodies);
        assert(!AcceptAimHit(false, true, Head, Head, true, Heads, Bodies)); // Empty ray.
        assert(!AcceptAimHit(true, false, Body, Body, true, Heads, Bodies)); // Muzzle obstruction.
        assert(AcceptAimHit(true, true, Head, Body, true, Heads, Bodies) == (Heads && Bodies));
        assert(AcceptAimHit(true, true, Body, Head, true, Heads, Bodies) == (Heads && Bodies));
        assert(!AcceptAimHit(true, true, Body, None, true, Heads, Bodies)); // Tail/wing hit.
        assert(!AcceptAimHit(true, true, Head, None, false, Heads, Bodies)); // No head fallback.
        assert(AcceptAimHit(true, true, Body, None, false, Heads, Bodies) == Bodies); // Capsule fallback.
    }
    using g_Aimbot::WithinAimRange;
    assert(WithinAimRange(29390, 29400, 100000, 29400));
    assert(!WithinAimRange(29390, 29400.01, 100000, 29400)); // Camera inside, muzzle outside range.
    assert(!WithinAimRange(10001, 9900, 10000, 29400)); // Configured limit still applies.
    assert(!WithinAimRange(100, 100, 10000, 0));
    assert(!WithinAimRange(100, 100, 10000, std::numeric_limits<double>::quiet_NaN()));
    assert(!WithinAimRange(100, INFINITY, 10000, INFINITY));
    assert(WithinAimRange(100, 100, 10000, INFINITY)); // Unknown projectile range isn't invented.
    for (const auto Name : {"Head", "b_Head", "Skull", "HEAD_01", "Cephalon"})
        assert(g_Aimbot::ClassifyAimBone(Name) == AimBodyPart::Head);
    for (const auto Name : {"Spine1", "spine_03", "Pelvis", "Bip01 Spine2", "c_Chest", "Body", "abdomen",
        "c_back1", "c_back_004", "Cnt_Spine_002_JNT_SKL", "hip_exp", "Carapace", "shell", "Spine_final"})
        assert(g_Aimbot::ClassifyAimBone(Name) == AimBodyPart::Body);
    for (const auto Name : {"Tail", "Tail_01", "tail_spine_2", "Wing", "Head_Horn", "Head_Jaw",
        "Spine_Neck", "HeadSocket", "LeftLeg", "root", "unknown", "",
        "Lft_Back_001Linear000_JNT_SKL", "Rht_Back_AIk004_JNT_SKL", "l_HindLeg_Hip_exp", "c_backwards",
        "DorsalFin", "Body_Fin_01", "BodyFlipper", "TailShell", "ShellLeg"})
        assert(g_Aimbot::ClassifyAimBone(Name) == AimBodyPart::None);
    for (const auto Name : {"root", "Root_JNT", "main"}) {
        assert(g_Aimbot::ClassifyAimPhysicsBone(Name, true) == AimBodyPart::Body);
        assert(g_Aimbot::ClassifyAimPhysicsBone(Name, false) == AimBodyPart::None);
    }
    for (const auto Name : {"tail", "fin", "flipper", "neck", "Lft_Back_001_JNT", "None", ""})
        assert(g_Aimbot::ClassifyAimPhysicsBone(Name, true) == AimBodyPart::None);
    g_Aimbot::TargetHoldState Hold;
    assert(!Hold.Update(true, false, 10));
    assert(Hold.Allows(101) && Hold.Allows(202));
    assert(Hold.Update(true, true, 10));
    Hold.Capture(101);
    assert(!Hold.Update(true, true, 10));
    Hold.Capture(202); // A different dinosaur moving nearer the circle cannot replace it.
    assert(Hold.Target == 101 && Hold.Allows(101) && !Hold.Allows(202));
    Hold.Capture(0); // Lost/dead/occluded targets don't auto-switch during this press.
    assert(Hold.Target == 101 && !Hold.Allows(202));
    assert(!Hold.Update(true, false, 10));
    assert(Hold.Allows(202));
    assert(Hold.Update(true, true, 10));
    Hold.Capture(202);
    assert(Hold.Target == 202);
    assert(Hold.Update(true, true, 11)); // Weapon instance change resets the old identity.
    assert(!Hold.Target);
    Hold.Capture(303);
    assert(!Hold.Update(false, true, 11) && !Hold.Active && !Hold.Target);

    // Tek bow's instant trace reads two local FVector output references from
    // GetCharacterViewLocationAndDirection. Match the captured expression form.
    std::array<uint8_t, 32> ViewCall{};
    ViewCall[0] = 0x1c; // EX_FinalFunction; eight-byte UFunction reference follows.
    const uintptr_t LocationProperty = 0x12345000, DirectionProperty = 0x12346000;
    std::memcpy(ViewCall.data() + 10, &LocationProperty, sizeof(uintptr_t));
    std::memcpy(ViewCall.data() + 19, &DirectionProperty, sizeof(uintptr_t));
    g_Aimbot::ViewArgumentRefs Refs{};
    assert(g_Aimbot::ReadViewArgumentRefs(ViewCall, 9, Refs));
    assert(Refs.Location == LocationProperty && Refs.Direction == DirectionProperty);
    assert(!g_Aimbot::ReadViewArgumentRefs(ViewCall, 0, Refs)); // Not an argument expression.
    assert(!g_Aimbot::ReadViewArgumentRefs(ViewCall, ViewCall.size() + 1, Refs));
    assert(!g_Aimbot::ReadViewArgumentRefs(std::span(ViewCall).first(26), 9, Refs)); // Truncated second reference.
    ViewCall[18] = 0x01; // EX_InstanceVariable must not write to the weapon object.
    assert(!g_Aimbot::ReadViewArgumentRefs(ViewCall, 9, Refs));
    ViewCall[18] = 0x00;
    std::memcpy(ViewCall.data() + 19, &LocationProperty, sizeof(uintptr_t));
    assert(!g_Aimbot::ReadViewArgumentRefs(ViewCall, 9, Refs)); // No aliased origin/direction.
    assert(g_Aimbot::ValidViewVectorOffset(0x1f0, 24, 1, 0x390));
    assert(g_Aimbot::ValidViewVectorOffset(0x208, 24, 1, 0x390));
    assert(!g_Aimbot::ValidViewVectorOffset(0x389, 24, 1, 0x390));
    assert(!g_Aimbot::ValidViewVectorOffset(-8, 24, 1, 0x390));
    assert(!g_Aimbot::ValidViewVectorOffset(0x1f0, 12, 1, 0x390));
    assert(!g_Aimbot::ValidViewVectorOffset(0x1f0, 24, 2, 0x390));
    // Use the instant getter's origin, which can differ from projectile spawn.
    struct ViewVector { double X, Y, Z; };
    const ViewVector Target{1000, 200, 100}, NativeOrigin{100, 200, 100};
    ViewVector Direction{0, 1, 0};
    assert(g_Aimbot::RedirectViewFromOrigin(Target, NativeOrigin, Direction));
    assert(Direction.X == 1 && Direction.Y == 0 && Direction.Z == 0);
    assert(!g_Aimbot::RedirectViewFromOrigin(Target, Target, Direction));
    assert(!g_Aimbot::RedirectViewFromOrigin(Target,
        ViewVector{std::numeric_limits<double>::quiet_NaN(), 0, 0}, Direction));
    assert(Direction.X == 1 && Direction.Y == 0 && Direction.Z == 0); // Failure preserves original output.

    using g_Aimbot::AimCircle;
    const auto Circle = AimCircle::FromViewport(2560, 1600, 30);
    assert(Circle.X == 1280 && Circle.Y == 800 && Circle.Radius == 240);
    assert(Circle.Score(1280, 800) == 0);
    assert(std::isfinite(Circle.Score(1520, 800))); // Inclusive circle edge.
    assert(!std::isfinite(Circle.Score(1520.001, 800)));
    assert(!std::isfinite(Circle.Score(2560, 800)));
    assert(!std::isfinite(Circle.Score(-1, 800)));
    assert(!std::isfinite(Circle.Score(std::numeric_limits<double>::quiet_NaN(), 800)));
    assert(!std::isfinite(AimCircle::FromViewport(0, 1600, 30).Score(0, 0)));
    // The ranking is radial, not nearest on one axis or a rectangle test.
    assert(Circle.Score(1280 + 40, 800 + 40) < Circle.Score(1280 + 60, 800));
    assert(!std::isfinite(Circle.Score(1280 + 200, 800 + 200)));
    // Resizing and aspect changes preserve the configured fraction of short edge.
    const auto Small = AimCircle::FromViewport(1280, 800, 30);
    assert(Small.Radius == Circle.Radius / 2);
    assert(Small.Score(660, 420) * 4 == Circle.Score(1320, 840));
    assert(AimCircle::FromViewport(3440, 1440, 100).Radius == 720);
    assert(AimCircle::FromViewport(800, 1280, 100).Radius == 400);
    assert(AimCircle::FromViewport(2560, 1600, -1).Radius == 8);
    assert(AimCircle::FromViewport(2560, 1600, std::numeric_limits<float>::quiet_NaN()).Radius == 240);

    // DebugCanvas keeps SizeX/Y at 2560x1600 but ClipX/Y become
    // 1706.6666x1066.6666 at 150% DPI. The engine applies DPI after drawing.
    // Circle, projected target and connecting line must land on physical pixels.
    for (double Dpi : {1.0, 1.25, 1.5, 2.0}) {
        const auto Space = g_Aimbot::AimCanvasSpace::FromViewport(2560, 1600,
            static_cast<float>(2560 / Dpi), static_cast<float>(1600 / Dpi));
        for (const auto Point : {g_Aimbot::AimScreenPoint{Circle.X, Circle.Y},
            {Circle.X + Circle.Radius, Circle.Y}, {Circle.X, Circle.Y - Circle.Radius}, {1320, 840}}) {
            const auto DrawPoint = Space.ToCanvas(Point.X, Point.Y);
            assert(std::abs(DrawPoint.X * Dpi - Point.X) < 0.001);
            assert(std::abs(DrawPoint.Y * Dpi - Point.Y) < 0.001);
        }
    }
    assert(g_Aimbot::AimCanvasSpace::FromViewport(0, 1600, 2560, 1600).ScaleX == 0);
    assert(g_Aimbot::AimCanvasSpace::FromViewport(2560, 1600, 0, 0).ScaleY == 0);
    assert(g_Aimbot::AimCanvasSpace::FromViewport(2560, 1600,
        std::numeric_limits<double>::quiet_NaN(), 1600).ScaleX == 0);
    // The full two-line label stays inside the circle and below its center,
    // including long names, small circles and large user-selected fonts.
    for (double Radius : {8.0, 160.0, 240.0}) {
        for (double Width : {40.0, 300.0, 10000.0}) {
            for (double Height : {24.0, 120.0}) {
                const auto Layout = g_Aimbot::AimLabelLayout::WithinCircle(1280, 800, Radius, Width, Height);
                assert(Layout.Scale > 0 && Layout.Scale <= 1 && Layout.Y > 800);
                assert(std::abs(Layout.X + Width * Layout.Scale / 2 - 1280) < 0.001);
                for (double X : {Layout.X, Layout.X + Width * Layout.Scale})
                    for (double Y : {Layout.Y, Layout.Y + Height * Layout.Scale})
                        assert(std::hypot(X - 1280, Y - 800) < Radius);
            }
        }
    }

    // Exercise actual configuration persistence, including independent toggles.
    const auto Directory = fs::absolute("release/tests/aim-config");
    auto& Manager = ConfigManager::Get();
    Manager.Initialize(Directory.string());
    // An old creature-only/friendly setup migrates into shared feature controls.
    std::ofstream(Directory / "old-visuals.ini") <<
        "g_Config::bESPEnabled=0\ng_Config::bTeamDinoModelHighlight=1\n"
        "g_Config::bDinoDrawBox=1\ng_Config::DinoBoxColor=0.2,0.3,0.4,0.8\n"
        "g_Config::bDrawName=0\ng_Config::bDrawNameTeam=1\ng_Config::NameColorTeam=0.1,0.2,0.3,0.9\n"
        "g_Config::DinoModelHighlightMaxDistance=700\ng_Config::bTekHelmetNightVision=1\n"
        "g_Config::bTeamDrawStructures=1\ng_Config::TeamStructureMaxDistance=900\n";
    assert(Manager.LoadConfig("old-visuals.ini"));
    assert(g_Config::bModelHighlightEnabled && g_Config::bDrawBox && g_Config::bDrawName);
    assert(g_Config::BoxColor[0] == 0.2f && g_Config::NameColorTeam[0] == 0.1f);
    assert(g_Config::ModelHighlightMaxDistance == 700 && g_Config::bDrawStructures && g_Config::StructureMaxDistance == 900);
    for (bool Players : {false, true}) for (bool Creatures : {false, true}) {
        g_Config::bESPPlayers = Players;
        g_Config::bESPCreatures = Creatures;
        g_Config::bModelHighlightEnabled = false;
        g_Config::bDrawName = true; // Information works with the model disabled.
        g_Config::bDrawBox = false;
        g_Config::bPumpShotgunNoReload = true;
        g_Config::BoxColor[0] = 0.8f;
        g_Config::BoxColorTeam[0] = 0.2f;
        assert(Manager.SaveConfig("shared-visuals.ini"));
        // Stale old settings must not re-enable a deliberately disabled feature.
        std::ofstream(Directory / "shared-visuals.ini", std::ios::app) <<
            "g_Config::bDinoDrawBox=1\ng_Config::bTeamDinoModelHighlight=1\ng_Config::bESPEnabled=1\n";
        g_Config::bESPPlayers = !Players; g_Config::bESPCreatures = !Creatures;
        g_Config::bDrawBox = true; g_Config::bDrawName = false;
        g_Config::bModelHighlightEnabled = true;
        g_Config::bPumpShotgunNoReload = false;
        g_Config::BoxColor[0] = g_Config::BoxColorTeam[0] = 0.0f;
        assert(Manager.LoadConfig("shared-visuals.ini"));
        assert(g_Config::ESPCharacterEnabled(false) == Players);
        assert(g_Config::ESPCharacterEnabled(true) == Creatures);
        assert(!g_Config::bModelHighlightEnabled && !g_Config::bDrawBox && g_Config::bDrawName);
        assert(g_Config::bPumpShotgunNoReload);
        assert(g_Config::BoxColor[0] == 0.8f && g_Config::BoxColorTeam[0] == 0.2f);
    }
    assert(Manager.SaveConfig("clean-visuals.ini"));
    std::ifstream SavedVisuals(Directory / "clean-visuals.ini");
    const std::string SavedText((std::istreambuf_iterator<char>(SavedVisuals)), {});
    for (const auto Removed : {"bDinoDrawBox=", "bESPEnabled=", "bESPTeamEnabled=", "bTeamDinoModelHighlight=", "bTekHelmetNightVision=", "bNightVisionPreferGoggles="})
        assert(SavedText.find(Removed) == std::string::npos);
    assert(g_Config::bAimHead && g_Config::bAimBody && g_Config::bAimRemoveSpread);
    for (const bool Heads : {false, true}) for (const bool Bodies : {false, true}) {
        g_Config::bAimHead = Heads;
        g_Config::bAimBody = Bodies;
        assert(Manager.SaveConfig("parts.ini"));
        g_Config::bAimHead = !Heads;
        g_Config::bAimBody = !Bodies;
        assert(Manager.LoadConfig("parts.ini"));
        assert(g_Config::bAimHead == Heads && g_Config::bAimBody == Bodies);
    }
    g_Config::bAimbotEnabled = true;
    g_Config::bAimRemoveSpread = false;
    g_Config::bTekPistolHoldTarget = false;
    g_Config::bShotgunConverge = false;
    g_Config::AimbotCircleRadiusPercent = 47;
    g_Config::bDrawAimCircle = false;
    g_Config::bDrawAimTargetLine = true;
    g_Config::bDrawAimTargetInfo = false;
    g_Config::AimTargetInfoScale = 1.5f;
    g_Config::AimCircleThickness = 2.5f;
    g_Config::AimTargetLineThickness = 3.5f;
    g_Config::AimTargetMarkerSize = 9;
    g_Config::AimCircleColor[0] = 0.25f;
    g_Config::AimCircleColor[3] = 0.5f;
    g_Config::AimTargetLineColor[1] = 0.75f;
    g_Config::AimTargetLineColor[3] = 0.25f;
    assert(Manager.SaveConfig("circle.ini"));
    g_Config::bAimbotEnabled = false;
    g_Config::bAimRemoveSpread = true;
    g_Config::bTekPistolHoldTarget = true;
    g_Config::bShotgunConverge = true;
    g_Config::AimbotCircleRadiusPercent = 1;
    g_Config::bDrawAimCircle = true;
    g_Config::bDrawAimTargetLine = false;
    g_Config::bDrawAimTargetInfo = true;
    g_Config::AimTargetInfoScale = 1.0f;
    g_Config::AimCircleThickness = g_Config::AimTargetLineThickness = 1;
    g_Config::AimTargetMarkerSize = 0;
    g_Config::AimCircleColor[0] = g_Config::AimCircleColor[3] = 1;
    g_Config::AimTargetLineColor[1] = g_Config::AimTargetLineColor[3] = 1;
    assert(Manager.LoadConfig("circle.ini"));
    assert(g_Config::bAimbotEnabled && !g_Config::bDrawAimCircle && g_Config::bDrawAimTargetLine);
    assert(!g_Config::bTekPistolHoldTarget);
    assert(!g_Config::bShotgunConverge);
    assert(!g_Config::bAimRemoveSpread);
    assert(!g_Config::bDrawAimTargetInfo && g_Config::AimTargetInfoScale == 1.5f);
    assert(g_Config::AimbotCircleRadiusPercent == 47);
    assert(g_Config::AimCircleThickness == 2.5f && g_Config::AimTargetLineThickness == 3.5f);
    assert(g_Config::AimTargetMarkerSize == 9);
    assert(g_Config::AimCircleColor[0] == 0.25f && g_Config::AimCircleColor[3] == 0.5f);
    assert(g_Config::AimTargetLineColor[1] == 0.75f && g_Config::AimTargetLineColor[3] == 0.25f);
    g_Config::bDrawAimCircle = true;
    g_Config::bDrawAimTargetLine = false;
    g_Config::bDrawAimTargetInfo = true;
    assert(Manager.SaveConfig("circle.ini"));
    g_Config::bDrawAimCircle = false;
    g_Config::bDrawAimTargetLine = true;
    g_Config::bDrawAimTargetInfo = false;
    assert(Manager.LoadConfig("circle.ini"));
    assert(g_Config::bDrawAimCircle && !g_Config::bDrawAimTargetLine);
    assert(g_Config::bDrawAimTargetInfo);
    // Old camera-steering keys are ignored; the existing master switch survives.
    std::ofstream(Directory / "legacy.ini") << "g_Config::bAimbotEnabled=true\ng_Config::bMagicBullet=false\ng_Config::AimbotFOV=180\n";
    g_Config::bAimbotEnabled = false;
    assert(Manager.LoadConfig("legacy.ini") && g_Config::bAimbotEnabled);
    assert(g_Config::AimbotCircleRadiusPercent == 47);
    assert(g_Config::bAimHead && g_Config::bAimBody); // Legacy files retain new defaults.
    std::cout << "Aim parts, collision acceptance, weapon range, held targets, spread, DPI, configuration and Tek bow arguments passed\n";
}
