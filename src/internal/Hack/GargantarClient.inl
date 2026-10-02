// Included in FeatureExpansion's anonymous namespace after ExpansionPatch.
// No RPC, authority writes, CDO changes, stamina/skill bypass or server plugin.
std::string GargantarStatus = "吞丘龙客户端实验未启用";
ExpansionPatch GargantarPatches[4], GargantarTransitPatches[8];
uint64_t GargantarNextStatus = 0;
std::string GargantarLastActive;
ExtensionObject GargantarLastMount;
#include "GargantarAttackPatch.inl"
GargantarAttackOverrides GargantarAttacks;
void ResetGargantarClient() {
    for (auto& P:GargantarPatches) P.Restore();
    for (auto& P:GargantarTransitPatches) P.Restore();
    GargantarAttacks.Restore(); GargantarNextStatus=0;
    GargantarLastActive.clear(); GargantarLastMount={};
}
void TickGargantarClient(SDK::APrimalDinoCharacter* Mount,bool Ready) {
    const bool Matches=IsUsableObject(Mount) && !Mount->IsDead() && Mount->Class->GetName()=="Gargantar_Character_BP_C";
    const bool Client=Matches && Mount->Role==SDK::ENetRole::ROLE_AutonomousProxy;
    const bool Apply=g_Config::bGargantarClient && Ready && Client;
    if (GargantarLastMount.Get()!=Mount) { GargantarLastActive.clear(); GargantarLastMount=ExtensionObject(Mount); }
    for (auto& P:GargantarPatches) P.Seen=false;
    for (auto& P:GargantarTransitPatches) P.Seen=false;
    if (Apply) {
        if (g_Config::bGargantarReach) GargantarPatches[0].Set(Mount,"AbsorbMaxDistance",ExtensionLimit(g_Config::GargantarReachMeters,5,50,20)*100);
        if (g_Config::bGargantarFastHold) GargantarPatches[1].Set(Mount,"AttackHoldThreshold",ExtensionLimit(g_Config::GargantarHoldSeconds,.02f,.25f,.05f));
        auto* Move=ReadReflectedObjectValue(Mount,"DracoFlightMovement");
        if (g_Config::bGargantarFlightSpeed && IsUsableObject(Move)) {
            const auto Scale=ExtensionLimit(g_Config::GargantarFlightMultiplier,.5f,3,1.5f);
            GargantarPatches[2].Set(Move,"HoverMode",Scale,true,"MaxFlySpeed");
            GargantarPatches[3].Set(Move,"SprintMode",Scale,true,"MaxFlySpeed");
        }
        if (g_Config::bGargantarTransitVisual) {
            auto Visit=[&](SDK::UObject* Buff) {
                if (!IsUsableObject(Buff) || !ExpansionHasClass(Buff,"buff_gargantar_creaturetransit") ||
                    !Buff->IsA(SDK::AActor::StaticClass()) || static_cast<SDK::AActor*>(Buff)->Role==SDK::ENetRole::ROLE_Authority ||
                    ReadReflectedObjectValue(Buff,"TransitSource")!=Mount) return;
                for (auto& P:GargantarTransitPatches) if (P.Owner.Get()==Buff) {
                    P.Set(Buff,"TransitDuration",ExtensionLimit(g_Config::GargantarTransitVisualSeconds,.1f,1.2f,.4f)); return;
                }
                for (auto& P:GargantarTransitPatches) if (!P.Seen) {
                    P.Set(Buff,"TransitDuration",ExtensionLimit(g_Config::GargantarTransitVisualSeconds,.1f,1.2f,.4f)); return;
                }
            };
            Visit(ReadReflectedObjectValue(Mount,"ActiveCreatureAbsorb"));
            auto* Victim=ReadReflectedObjectValue(Mount,"LastAbsorbedTarget");
            if (IsUsableObject(Victim) && Victim->IsA(SDK::APrimalCharacter::StaticClass())) {
                SDK::TArray<SDK::APrimalBuff*> Buffs{};
                static_cast<SDK::APrimalCharacter*>(Victim)->GetAllBuffs(&Buffs);
                if (Buffs.Num()>=0 && Buffs.Num()<=128) for (int I=0;I<Buffs.Num();++I) Visit(Buffs[I]);
            }
        }
    }
    const bool AttackApplied=Apply && g_Config::bGargantarAttackInterval && GargantarAttacks.Set(Mount,ExtensionLimit(g_Config::GargantarAttackIntervalMultiplier,.1f,1,.5f));
    if (!AttackApplied) GargantarAttacks.Restore();
    for (auto& P:GargantarPatches) if (!P.Seen) P.Restore();
    for (auto& P:GargantarTransitPatches) if (!P.Seen) P.Restore();
    const auto Now=GetTickCount64(); if (Now<GargantarNextStatus) return;
    GargantarNextStatus=Now+250;
    if (!g_Config::bGargantarClient) { ExtensionStatus(GargantarStatus,"吞丘龙客户端实验未启用"); return; }
    if (!Matches) { ExtensionStatus(GargantarStatus,"请骑乘吞丘龙，离骑后实验参数已恢复"); return; }
    if (!Client) { ExtensionStatus(GargantarStatus,"仅支持联机客户端骑乘实例，单机或主机权威对象不修改"); return; }
    double Energy=0; ReadReflectedNumberValue(Mount,"DracoEnergyPercent",&Energy);
    int Count=AttackApplied?9:0; for (auto& P:GargantarPatches) if (P.Offset>=0 && P.Seen) ++Count;
    for (auto& P:GargantarTransitPatches) if (P.Offset>=0 && P.Seen) ++Count;
    if (Apply) {
        double Reach=0,Hold=0,Hover=0,Sprint=0;
        ReadReflectedNumberValue(Mount,"AbsorbMaxDistance",&Reach);
        ReadReflectedNumberValue(Mount,"AttackHoldThreshold",&Hold);
        for (int I=2;I<4;++I) if (GargantarPatches[I].Offset>=0) {
            auto* Owner=GargantarPatches[I].Owner.Get();
            if (Owner) (I==2?Hover:Sprint)=GargantarPatches[I].Read(Owner);
        }
        char Previous[512]{};
        const auto Format=LanguageManager::Get().TranslateCopy("上次运行：字段 %d | 生物距离 %.1f m | 长按 %.3f s | 飞行 %.0f/%.0f cm/s");
        snprintf(Previous,sizeof(Previous),Format.c_str(),Count,Reach/100,Hold,Hover,Sprint);
        GargantarLastActive=Previous;
    }
    char Values[128]{}; snprintf(Values,sizeof(Values)," | %.1f%% | %d",std::clamp(Energy,0.0,1.0)*100,Count);
    const bool Wanted=g_Config::bGargantarReach || g_Config::bGargantarFastHold || g_Config::bGargantarFlightSpeed ||
        g_Config::bGargantarAttackInterval || g_Config::bGargantarTransitVisual;
    const std::string State=!Ready ? "菜单或输入暂停，实验参数已恢复" : Count>0 ? "已应用客户端参数；服务器是否接受待实测" :
        Wanted ? "当前无可应用字段或吞吐过程尚未开始" : "已读取吞丘龙状态，请选择一个实验开关";
    const auto Previous=GargantarLastActive.empty() ? LanguageManager::Get().TranslateCopy("尚未记录游戏运行值；关闭菜单并保持游戏前台后再查看") : GargantarLastActive;
    ExtensionStatus(GargantarStatus,LanguageManager::Get().TranslateCopy(State)+Values+"\n20261001.2 | "+Previous);
}
