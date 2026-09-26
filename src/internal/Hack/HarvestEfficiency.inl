// Included in the private harvest namespace after the navigation/probe helpers.
void SampleAutoHarvestProgress(SDK::APrimalDinoCharacter* Dino, Harvest::Time Now) {
    if (Now < s_autoHarvestState.NextHarvestProgressSample) return;
    Harvest::InventoryProgress::Snapshot Inventory;
    if (TryGetAutoHarvestInventorySnapshot(Dino, &Inventory) &&
        s_autoHarvestState.InventoryProgress.Observe(std::move(Inventory)))
        s_autoHarvestState.LastHarvestProgress = Now;
    s_autoHarvestState.NextHarvestProgressSample = Now + std::chrono::milliseconds(500);
}

Harvest::RouteCost EstimateAutoHarvestRoute(SDK::UWorld* World,
    SDK::APrimalDinoCharacter* Dino, SDK::UNavigationSystemV1* Navigation,
    const AutoHarvestProbeFrame& Frame, const SDK::FVector& Start,
    const SDK::FVector& Target, float RangeMeters,
    const SDK::TArray<SDK::AActor*>& IgnoreActors, std::vector<SDK::FVector>& OutPoints) {
    OutPoints.clear();
    const double Direct = std::sqrt(AutoHarvestDistanceSquared(Start, Target));
    Harvest::RouteCost Cost{false, Direct, Direct};
    const double Range = RangeMeters * 100.0;
    const double Tolerance = std::clamp(double(Frame.CapsuleRadius) * 0.35, 125.0, 300.0);
    if (Direct <= Range && AutoHarvestHasClearBeamPath(World, Frame.CollisionCenter,
        Target, Tolerance, IgnoreActors)) return {true, 0, Direct};
    if (!std::isfinite(Direct)) return Cost;

    if (!Navigation) {
        // ASA clients may have no navigation system at all. Prove a short
        // direct approach using consecutive ground/collision probes, following
        // sampled ground height on slopes. Unknown corridors remain fallbacks.
        auto Probe = Frame;
        SDK::FVector Direction{};
        if (!MakeHorizontalAutoHarvestDirection(Start, Target, &Direction)) return Cost;
        double Travel = 0;
        for (unsigned Step = 0; Step <= Harvest::GroundSelectionSegments; ++Step) {
            const auto Position = Start + (Probe.FootLocation - Frame.FootLocation);
            if (AutoHarvestDistanceSquared(Position, Target) <= Range * Range &&
                AutoHarvestHasClearBeamPath(World, Probe.CollisionCenter, Target, Tolerance, IgnoreActors))
                return {true, Travel, Direct};
            if (Step == Harvest::GroundSelectionSegments) break;
            SDK::FVector NextFoot{};
            if (!AutoHarvestHasSafeGround(World, Dino, Probe, Direction,
                g_Config::AutoHarvestMaxSafeDropMeters, IgnoreActors, &NextFoot) ||
                AutoHarvestDirectionIsBlocked(World, Dino, Probe, Direction, Target, IgnoreActors)) break;
            const auto Advance = NextFoot - Probe.FootLocation;
            Travel += std::sqrt(AutoHarvestDistanceSquared(NextFoot, Probe.FootLocation));
            Probe.CollisionCenter = Probe.CollisionCenter + Advance;
            Probe.FootLocation = NextFoot;
        }
        return Cost;
    }

    // Query a harvest standing position instead of the solid resource center.
    // Only one route per shortlisted resource; the full planner handles detours
    // after commitment. Missing/partial navigation remains a distance fallback.
    SDK::FVector TowardStart{};
    if (!MakeHorizontalAutoHarvestDirection(Target, Start, &TowardStart)) return Cost;
    const double StandOff = std::max(100.0, Range * 0.65);
    const auto Candidate = Target + TowardStart * StandOff;
    SDK::FVector Projected{};
    const SDK::FVector QueryExtent{std::max(200.0, double(Frame.CapsuleRadius) * 1.75),
        std::max(200.0, double(Frame.CapsuleRadius) * 1.75),
        std::max(300.0, double(Frame.CapsuleHalfHeight) * 1.5)};
    if (!SDK::UNavigationSystemV1::K2_ProjectPointToNavigation(World, Candidate, &Projected,
        Navigation->MainNavData, SDK::TSubclassOf<SDK::UNavigationQueryFilter>(nullptr), QueryExtent) ||
        !IsFiniteAutoHarvestVector(Projected)) return Cost;
    auto* Path = SDK::UNavigationSystemV1::FindPathToLocationSynchronously(World, Start,
        Projected, Dino, SDK::TSubclassOf<SDK::UNavigationQueryFilter>(nullptr));
    if (!IsUsableObject(Path) || !Path->IsValid() || Path->PathPoints.Num() < 2 || Path->PathPoints.Num() > 512) return Cost;
    std::vector<SDK::FVector> Points;
    double Length = 0;
    auto Previous = Start;
    for (const auto& Point : Path->PathPoints) {
        if (!IsFiniteAutoHarvestVector(Point)) return Cost;
        Length += std::sqrt(AutoHarvestDistanceSquared(Previous, Point));
        Points.push_back(Point); Previous = Point;
    }
    const auto FinalActor = Points.back() + (Start - Frame.FootLocation);
    if (!std::isfinite(Length) || AutoHarvestDistanceSquared(FinalActor, Target) > Range * Range ||
        !AutoHarvestHasClearBeamPath(World, FinalActor, Target, Tolerance, IgnoreActors)) return Cost;
    OutPoints = std::move(Points);
    return {true, Length, Direct};
}

void ResetAutoHarvestSelection() {
    auto& S=s_autoHarvestState;
    S.SelectionCandidates.clear();S.SelectionCursor=0;S.SelectionHasBest=false;S.SelectionBestRoute.clear();
    S.SelectionBestCost={false,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()};
}

bool AdvanceAutoHarvestRouteSelection(SDK::UWorld* World, SDK::APrimalDinoCharacter* Dino,
    const AutoHarvestProbeFrame& Frame, const SDK::FVector& Start, float RangeMeters,
    const SDK::TArray<SDK::AActor*>& IgnoreActors) {
    auto& S=s_autoHarvestState;
    auto* Navigation=SDK::UNavigationSystemV1::GetNavigationSystem(World);
    if(!IsUsableObject(Navigation)||(!IsUsableObject(Navigation->MainNavData)&&
        (Navigation->NavDataSet.Num()<=0||Navigation->NavDataSet.Num()>128))) Navigation=nullptr;
    for(unsigned Work=0;Work<Harvest::CandidateRoutesPerUpdate&&S.SelectionCursor<S.SelectionCandidates.size();++Work) {
        auto Candidate=S.SelectionCandidates[S.SelectionCursor++];
        SDK::FVector Updated{};std::string Type;
        // Filters/markers may change between slices. Do not commit a stale or
        // newly deselected resource merely because it was in the old shortlist.
        if(!g_DrawESP::FindResourceTargetNear(Candidate.Location,.8f,&Updated,&Type))continue;
        Candidate.Location=Updated;Candidate.TypeName=std::move(Type);
        Candidate.DistanceMeters=float(std::sqrt(AutoHarvestDistanceSquared(Start,Updated))*.01);
        std::vector<SDK::FVector> Route;
        const auto Cost=EstimateAutoHarvestRoute(World,Dino,Navigation,Frame,Start,
            Candidate.Location,RangeMeters,IgnoreActors,Route);
        if(Harvest::PreferRoute(Cost,S.SelectionBestCost)) {
            S.SelectionHasBest=true;S.SelectionBestCost=Cost;S.SelectionBest=std::move(Candidate);
            S.SelectionBestRoute=std::move(Route);
        }
        if(S.SelectionHasBest&&S.SelectionBestCost.ReachesHarvestRange) {
            // Straight-line distance minus attack reach is a lower bound on any
            // remaining route. Stop only when none can improve the verified one.
            bool CanImprove=false;
            for(size_t I=S.SelectionCursor;I<S.SelectionCandidates.size();++I) {
                const double Direct=std::sqrt(AutoHarvestDistanceSquared(Start,S.SelectionCandidates[I].Location));
                CanImprove|=std::max(0.,Direct-RangeMeters*100.)<S.SelectionBestCost.Travel;
            }
            if(!CanImprove)S.SelectionCursor=S.SelectionCandidates.size();
        }
    }
    return S.SelectionCursor>=S.SelectionCandidates.size();
}
