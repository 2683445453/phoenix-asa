// Production planner/selection functions are extracted immediately before this
// fixture is compiled. Native traces/navigation are deterministic, no game runs.
#include "../src/internal/Hack/HarvestRules.h"
#include "../src/internal/Config/InterfaceRules.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
namespace SDK {
    struct UObject{bool Live=true;};
    struct FVector {double X=0,Y=0,Z=0;FVector operator+(FVector B)const{return {X+B.X,Y+B.Y,Z+B.Z};}
        FVector operator-(FVector B)const{return {X-B.X,Y-B.Y,Z-B.Z};}FVector operator*(double V)const{return {X*V,Y*V,Z*V};}};
    struct AActor:UObject{};struct APrimalDinoCharacter:AActor{};struct UWorld:UObject{};
    struct UNavigationQueryFilter{};
    template<class T>struct TSubclassOf{explicit TSubclassOf(T*){}};
    template<class T>struct TArray:std::vector<T>{using std::vector<T>::vector;int Num()const{return int(this->size());}};
    struct UNavigationPath:UObject{TArray<FVector> PathPoints;bool Partial=false;bool IsValid(){return Live;}bool IsPartial(){return Partial;}};
    inline int PathCalls=0,ProjectionCalls=0,PathMode=1;inline bool NavigationAvailable=true,ProjectionAvailable=true;
    struct UNavigationSystemV1:UObject {
        UObject* MainNavData=nullptr;TArray<int> NavDataSet;
        static UNavigationSystemV1* GetNavigationSystem(UWorld*);
        static bool K2_ProjectPointToNavigation(UWorld*,FVector P,FVector* Out,UObject*,TSubclassOf<UNavigationQueryFilter>,FVector){
            ++ProjectionCalls;*Out=P;return ProjectionAvailable;}
        static UNavigationPath* FindPathToLocationSynchronously(UWorld*,FVector Start,FVector End,APrimalDinoCharacter*,TSubclassOf<UNavigationQueryFilter>);
    };
    inline UNavigationSystemV1 Navigation;inline UObject NavData;inline UNavigationPath Path;
    UNavigationSystemV1* UNavigationSystemV1::GetNavigationSystem(UWorld*){Navigation.MainNavData=&NavData;return NavigationAvailable?&Navigation:nullptr;}
    UNavigationPath* UNavigationSystemV1::FindPathToLocationSynchronously(UWorld*,FVector Start,FVector End,APrimalDinoCharacter*,TSubclassOf<UNavigationQueryFilter>){
        ++PathCalls;if(PathMode==0)return nullptr;Path.PathPoints.clear();Path.PathPoints.push_back(Start);
        if(PathMode==2)Path.PathPoints.push_back({Start.X,Start.Y+2000,Start.Z});
        if(PathMode==3)Path.PathPoints.push_back({NAN,0,0});Path.PathPoints.push_back(End);return &Path;
    }
}
namespace g_Config{
    inline bool bAutoHarvestExperimentalNavigation=true,bDinoHarvestRange=false;
    inline float AutoHarvestMaxSafeDropMeters=2.5,DinoHarvestRangeMultiplier=2;
}
namespace g_DrawESP {
    struct ResourceTargetCandidate{SDK::FVector Location;float DistanceMeters=0;std::string TypeName;};
    inline bool MarkersAvailable=true;
    bool FindResourceTargetNear(const SDK::FVector& P,float,SDK::FVector* Out,std::string* Type){*Out=P;*Type="Metal";return MarkersAvailable;}
}
namespace g_Hack {
    using namespace std::chrono_literals;
    template<class T>bool IsUsableObject(T* O){return O&&O->Live;}
    inline int Distances=0,GroundCalls=0,CollisionCalls=0,BeamCalls=0;
    inline bool BeamClear=true,GroundSafe=true,CollisionBlocked=false,OnlyWideGround=false;
    double AutoHarvestDistanceSquared(const SDK::FVector& A,const SDK::FVector& B){++Distances;auto D=A-B;return D.X*D.X+D.Y*D.Y+D.Z*D.Z;}
    bool IsFiniteAutoHarvestVector(const SDK::FVector& P){return std::isfinite(P.X)&&std::isfinite(P.Y)&&std::isfinite(P.Z);}
    bool MakeHorizontalAutoHarvestDirection(const SDK::FVector& A,const SDK::FVector& B,SDK::FVector* D){auto V=B-A;double L=std::hypot(V.X,V.Y);if(!std::isfinite(L)||L<1)return false;*D={V.X/L,V.Y/L,0};return true;}
    SDK::FVector RotateAutoHarvestDirection(const SDK::FVector& D,float Deg){double R=Deg*3.141592653589793/180;return {D.X*std::cos(R)-D.Y*std::sin(R),D.X*std::sin(R)+D.Y*std::cos(R),0};}
    struct AutoHarvestProbeFrame{SDK::FVector CollisionCenter{0,0,100},FootLocation{};float CapsuleRadius=100,CapsuleHalfHeight=100,MaxStepHeight=45;};
    struct State {
        bool MeleeReachReady=false,CachedRangeEnabled=false;
        float NativeMeleeReachMeters=0,CachedRangeMultiplier=0;
        Harvest::Time NextMeleeReach{};
        bool NavigationPathAvailable=false,NavigationPathPartial=false,SelectionHasBest=false;
        std::vector<SDK::FVector> NavigationPoints,SelectionBestRoute;
        std::vector<double> NavigationRemaining;
        size_t NavigationPointIndex=0,NavigationCandidateCursor=0,SelectionCursor=0,SteeringPairCursor=0;
        SDK::FVector AvoidDirection{};Harvest::Time AvoidUntil{};
        Harvest::Time UnsafeSince{},StationarySince{},LayerMismatchSince{},NextPathSmoothing{},NextHarvestProgressSample{},LastHarvestProgress{};
        std::vector<g_DrawESP::ResourceTargetCandidate> SelectionCandidates;
        Harvest::RouteCost SelectionBestCost{false,INFINITY,INFINITY};
        g_DrawESP::ResourceTargetCandidate SelectionBest;
        Harvest::InventoryProgress InventoryProgress;
    }s_autoHarvestState;
    bool AutoHarvestHasClearBeamPath(SDK::UWorld*,const SDK::FVector&,const SDK::FVector&,double,const SDK::TArray<SDK::AActor*>&){++BeamCalls;return BeamClear;}
    bool AutoHarvestHasSafeGround(SDK::UWorld*,SDK::APrimalDinoCharacter*,const AutoHarvestProbeFrame& F,
        const SDK::FVector& Direction,float,const SDK::TArray<SDK::AActor*>&,SDK::FVector* Next=nullptr){++GroundCalls;if(Next)*Next=F.FootLocation+Direction*250.;return GroundSafe&&(!OnlyWideGround||Direction.X<.6);}
    bool AutoHarvestDirectionIsBlocked(SDK::UWorld*,SDK::APrimalDinoCharacter*,const AutoHarvestProbeFrame&,
        const SDK::FVector&,const SDK::FVector&,const SDK::TArray<SDK::AActor*>&){++CollisionCalls;return CollisionBlocked;}
    bool TryGetAutoHarvestInventorySnapshot(SDK::APrimalDinoCharacter*,Harvest::InventoryProgress::Snapshot*){return false;}
    inline int RangeUpdates=0,ReachReads=0,RangeResets=0;
    inline float LastMultiplier=0;
    uint64_t GetTickCount64(){return 1;}
    void UpdateDinoHarvestRange(SDK::APrimalDinoCharacter*,bool,float Multiplier,uint64_t){++RangeUpdates;LastMultiplier=Multiplier;}
    bool GetDinoHarvestReachMeters(SDK::APrimalDinoCharacter*,float& Out){++ReachReads;Out=3;return true;}
    void ResetDinoHarvestRange(){++RangeResets;}
#include "../release/harvest-performance/reach-reset-runtime.inc"
    void RefreshReach(SDK::APrimalDinoCharacter* Dino,bool TekStriderMode,Harvest::Time Now){
#include "../release/harvest-performance/reach-cache-runtime.inc"
    }
#include "../release/harvest-performance/navigation-runtime.inc"
#include "../release/harvest-performance/path-distance-runtime.inc"
#include "../release/harvest-performance/steering-runtime.inc"
#include "../src/internal/Hack/HarvestEfficiency.inl"
}
int main(){
    using namespace g_Hack;SDK::UWorld World;SDK::APrimalDinoCharacter Dino;AutoHarvestProbeFrame Frame;
    SDK::TArray<SDK::AActor*> Ignore;const SDK::FVector Start{},Target{2000,0,0};
    assert(RefreshAutoHarvestNavigationPath(&World,&Dino,Frame,Start,Target,4.5,true,Ignore));
    assert(SDK::PathCalls==1); // Stop at a direct verified standing position.
    const auto Saved=s_autoHarvestState.NavigationPoints;
    SDK::PathMode=0;SDK::PathCalls=SDK::ProjectionCalls=0;
    assert(RefreshAutoHarvestNavigationPath(&World,&Dino,Frame,Start,Target,4.5,true,Ignore));
    assert(SDK::PathCalls<=int(Harvest::NavigationQueriesPerRefresh)&&SDK::ProjectionCalls<=4);
    assert(s_autoHarvestState.NavigationPoints.size()==Saved.size());
    SDK::PathMode=2;s_autoHarvestState.UnsafeSince=Harvest::Clock::now();SDK::PathCalls=0;
    assert(RefreshAutoHarvestNavigationPath(&World,&Dino,Frame,Start,Target,4.5,false,Ignore));
    assert(SDK::PathCalls<=2&&s_autoHarvestState.NavigationPoints.size()==3); // Longer detour replaces a blocked short route.
    SDK::PathMode=3;const auto Last=s_autoHarvestState.NavigationPoints;
    assert(RefreshAutoHarvestNavigationPath(&World,&Dino,Frame,Start,Target,4.5,false,Ignore));
    assert(s_autoHarvestState.NavigationPoints.size()==Last.size()); // Malformed candidate cannot erase a valid route.
    SDK::NavigationAvailable=false;assert(RefreshAutoHarvestNavigationPath(&World,&Dino,Frame,Start,Target,4.5,false,Ignore));

    std::vector<SDK::FVector> Route;GroundCalls=CollisionCalls=0;BeamClear=false;
    EstimateAutoHarvestRoute(&World,&Dino,nullptr,Frame,Start,Target,4.5,Ignore,Route);
    assert(GroundCalls==int(Harvest::GroundSelectionSegments)&&CollisionCalls==int(Harvest::GroundSelectionSegments));
    GroundSafe=false;GroundCalls=CollisionCalls=0;
    auto Cost=EstimateAutoHarvestRoute(&World,&Dino,nullptr,Frame,Start,Target,4.5,Ignore,Route);
    assert(!Cost.ReachesHarvestRange&&GroundCalls==1&&CollisionCalls==0); // No collision/movement follows a missing floor.
    GroundSafe=true;BeamClear=true;SDK::NavigationAvailable=true;SDK::PathMode=2;
    ResetAutoHarvestSelection();s_autoHarvestState.SelectionCandidates={{{2000,0,0},20,"Metal"},{{2100,0,0},21,"Metal"},{{2200,0,0},22,"Metal"}};
    int Slice=0;bool Complete=false;
    do{SDK::PathCalls=0;Complete=AdvanceAutoHarvestRouteSelection(&World,&Dino,Frame,Start,4.5,Ignore);
        assert(SDK::PathCalls<=1);++Slice;}while(!Complete);
    assert(Slice==3&&s_autoHarvestState.SelectionHasBest);
    ResetAutoHarvestSelection();g_DrawESP::MarkersAvailable=false;
    s_autoHarvestState.SelectionCandidates={{{2000,0,0},20,"Metal"}};SDK::PathCalls=0;
    assert(AdvanceAutoHarvestRouteSelection(&World,&Dino,Frame,Start,4.5,Ignore));
    assert(!s_autoHarvestState.SelectionHasBest&&SDK::PathCalls==0);

    OnlyWideGround=true;GroundCalls=CollisionCalls=0;s_autoHarvestState.SteeringPairCursor=0;
    auto Direction=FindAutoHarvestSafeDirection(&World,&Dino,Frame,{1,0,0},Target,2.5,Harvest::Clock::now(),Ignore);
    assert(!Direction.Found&&GroundCalls==3&&CollisionCalls==0);
    GroundCalls=CollisionCalls=0;
    Direction=FindAutoHarvestSafeDirection(&World,&Dino,Frame,{1,0,0},Target,2.5,Harvest::Clock::now(),Ignore);
    assert(Direction.Found&&Direction.Avoiding&&GroundCalls<=5&&CollisionCalls<=4); // Wider pair on the next update.

    s_autoHarvestState.NavigationPoints.clear();s_autoHarvestState.NavigationRemaining.clear();
    for(int I=0;I<512;++I)s_autoHarvestState.NavigationPoints.push_back({double(I*100),0,0});
    s_autoHarvestState.NavigationPathAvailable=true;s_autoHarvestState.NavigationPointIndex=1;
    const SDK::FVector FarTarget{51200,0,0};
    assert(GetAutoHarvestRemainingPathDistance(Start,FarTarget)==51200);
    Distances=0;for(int I=0;I<1000;++I)assert(GetAutoHarvestRemainingPathDistance(Start,FarTarget)==51200);
    assert(Distances==2000); // Was 512,000 segment square roots for these updates.

    // Execute the production cache gate: malformed configuration must not turn
    // a bounded reflection refresh into an every-update scan.
    const Harvest::Time Epoch{10s};g_Config::DinoHarvestRangeMultiplier=NAN;
    RefreshReach(&Dino,false,Epoch);assert(RangeUpdates==1&&ReachReads==1&&LastMultiplier==2);
    for(int Ms=30;Ms<250;Ms+=30)RefreshReach(&Dino,false,Epoch+std::chrono::milliseconds(Ms));
    assert(RangeUpdates==1&&ReachReads==1);
    g_Config::DinoHarvestRangeMultiplier=INFINITY;RefreshReach(&Dino,false,Epoch+249ms);
    assert(RangeUpdates==1); // Same normalized value shares the cache.
    RefreshReach(&Dino,false,Epoch+250ms);assert(RangeUpdates==2&&ReachReads==2);
    g_Config::DinoHarvestRangeMultiplier=99;RefreshReach(&Dino,false,Epoch+251ms);
    assert(RangeUpdates==3&&LastMultiplier==PhoenixInterface::MaxDinoHarvestRangeMultiplier);
    g_Config::DinoHarvestRangeMultiplier=100;RefreshReach(&Dino,false,Epoch+252ms);assert(RangeUpdates==3);
    g_Config::bDinoHarvestRange=true;RefreshReach(&Dino,false,Epoch+253ms);assert(RangeUpdates==4);
    ResetAutoHarvestMeleeReach();assert(RangeResets==1&&!s_autoHarvestState.MeleeReachReady);
    RefreshReach(&Dino,false,Epoch+254ms);assert(RangeUpdates==5);
    RefreshReach(&Dino,true,Epoch+255ms);assert(RangeResets==2&&!s_autoHarvestState.MeleeReachReady);
    RefreshReach(&Dino,true,Epoch+256ms);assert(RangeResets==2&&RangeUpdates==5);
    std::cout<<"Production harvest planner passed: <=2 nav queries/refresh, <=1 shortlist route/update, retained routes, blocked-route replacement, bounded ground and steering probes, stale marker rejection, NaN-safe 250ms melee cache with reset; cached path distances: 2000 vs 512000 calculations.\n";
}
